/*
 * Chaos Fork: Game File Transfer Implementation
 */

#include "chaos/game_file_transfer.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "output.h"

#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace Chaos {

// ============================================================
// GameFileTransferHost
// ============================================================

GameFileTransferHost::GameFileTransferHost() = default;

GameFileTransferHost::~GameFileTransferHost() {
	if (enum_thread.joinable()) {
		enum_thread.join();
	}
}

void GameFileTransferHost::Start(const std::string& game_path) {
	enum_thread = std::thread(&GameFileTransferHost::EnumerateThread, this, game_path);
}

void GameFileTransferHost::EnumerateThread(std::string game_path) {
	EnumerateDirectory(game_path, "");

	std::lock_guard<std::mutex> lock(files_mutex);
	total_files = static_cast<uint16_t>(std::min<size_t>(files.size(), 65535));
	int64_t sum = 0;
	for (auto& f : files) {
		sum += f.size;
	}
	total_bytes = static_cast<int32_t>(std::min<int64_t>(sum, INT32_MAX));
	info_ready = true;

	Output::Debug("GameFileTransfer: Enumerated {} files, {} bytes total",
		files.size(), sum);
}

void GameFileTransferHost::EnumerateDirectory(const std::string& base_path, const std::string& rel_prefix) {
	std::error_code ec;
	for (auto& entry : fs::directory_iterator(base_path, ec)) {
		if (ec) break;

		std::string name = entry.path().filename().u8string();

		// Skip hidden files, Save directories, and build artifacts
		if (name.empty() || name[0] == '.') continue;
		if (name == "Save" || name == "save") continue;

		if (entry.is_directory(ec)) {
			std::string sub_rel = rel_prefix.empty() ? name : rel_prefix + "/" + name;
			EnumerateDirectory(entry.path().u8string(), sub_rel);
		} else if (entry.is_regular_file(ec)) {
			FileEntry fe;
			fe.relative_path = rel_prefix.empty() ? name : rel_prefix + "/" + name;
			fe.absolute_path = entry.path().u8string();
			fe.size = static_cast<int64_t>(entry.file_size(ec));
			if (fe.size < 0) fe.size = 0;

			std::lock_guard<std::mutex> lock(files_mutex);
			files.push_back(std::move(fe));
		}
	}
}

bool GameFileTransferHost::SendChunks(NetManager& net, uint16_t target_peer_id, int max_chunks_per_frame) {
	if (!info_ready) return true; // Still enumerating

	// Send info packet once
	if (!info_sent) {
		PacketWriter pw(PacketType::GameFileInfo);
		pw.write(total_files.load());
		pw.write(total_bytes.load());
		net.SendTo(target_peer_id, pw, true);
		info_sent = true;
		Output::Debug("GameFileTransfer: Sent file info to peer {} ({} files, {} bytes)",
			target_peer_id, total_files.load(), total_bytes.load());
		return true;
	}

	std::lock_guard<std::mutex> lock(files_mutex);

	int chunks_sent = 0;
	while (current_file_idx < files.size() && chunks_sent < max_chunks_per_frame) {
		auto& fe = files[current_file_idx];

		// Open file and read a chunk
		std::ifstream ifs(fe.absolute_path, std::ios::binary);
		if (!ifs.is_open()) {
			// Skip unreadable files - send empty last chunk
			PacketWriter pw(PacketType::GameFileData);
			pw.write(fe.relative_path);
			pw.write(static_cast<uint8_t>(1)); // is_last_chunk
			pw.write(static_cast<uint16_t>(0)); // data size
			net.SendTo(target_peer_id, pw, true);
			current_file_idx++;
			current_file_offset = 0;
			chunks_sent++;
			continue;
		}

		ifs.seekg(current_file_offset);
		std::vector<char> buf(CHUNK_SIZE);
		ifs.read(buf.data(), CHUNK_SIZE);
		auto bytes_read = ifs.gcount();

		bool is_last = (current_file_offset + bytes_read >= fe.size) || ifs.eof();

		PacketWriter pw(PacketType::GameFileData);
		pw.write(fe.relative_path);
		pw.write(static_cast<uint8_t>(is_last ? 1 : 0));
		pw.write(static_cast<uint16_t>(static_cast<size_t>(bytes_read)));
		// Write raw bytes
		for (int64_t i = 0; i < bytes_read; ++i) {
			pw.write(static_cast<uint8_t>(buf[i]));
		}
		net.SendTo(target_peer_id, pw, true);

		current_file_offset += bytes_read;
		chunks_sent++;

		if (is_last) {
			current_file_idx++;
			current_file_offset = 0;
		}
	}

	// Check if all files sent
	if (current_file_idx >= files.size()) {
		PacketWriter pw(PacketType::GameFileDone);
		net.SendTo(target_peer_id, pw, true);
		done = true;
		Output::Debug("GameFileTransfer: All files sent to peer {}", target_peer_id);
		return false;
	}

	return true; // More data to send
}

// ============================================================
// GameFileTransferClient
// ============================================================

GameFileTransferClient::GameFileTransferClient() = default;

GameFileTransferClient::~GameFileTransferClient() {
	writer_stop = true;
	if (writer_thread.joinable()) {
		writer_thread.join();
	}
}

void GameFileTransferClient::SetDestination(const std::string& path) {
	dest_path = path;
}

void GameFileTransferClient::OnFileInfo(uint16_t file_count, int32_t total) {
	total_files = file_count;
	total_bytes = total;
	info_received = true;

	// Start writer thread
	if (!writer_running) {
		writer_running = true;
		writer_thread = std::thread(&GameFileTransferClient::WriterThread, this);
	}

	Output::Debug("GameFileTransfer: Expecting {} files, {} bytes", file_count, total);
}

void GameFileTransferClient::OnFileData(const std::string& relative_path,
	const uint8_t* data, size_t len, bool is_last_chunk) {
	FileWriteChunk chunk;
	chunk.relative_path = relative_path;
	chunk.data.assign(data, data + len);
	chunk.is_last_chunk = is_last_chunk;

	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		write_queue.push_back(std::move(chunk));
	}

	received_bytes += static_cast<int32_t>(len);
	if (is_last_chunk) {
		received_files++;
	}
}

void GameFileTransferClient::OnTransferDone() {
	// Signal writer to finish remaining queue then stop
	transfer_complete = true;
	Output::Debug("GameFileTransfer: Transfer done signal received ({} files, {} bytes)",
		received_files.load(), received_bytes.load());
}

void GameFileTransferClient::Update() {
	// Nothing needed on main thread - writer thread handles everything
}

std::string GameFileTransferClient::GetError() const {
	std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(error_mutex));
	return error_msg;
}

int GameFileTransferClient::GetProgressPercent() const {
	if (total_bytes <= 0) return 0;
	return static_cast<int>((static_cast<int64_t>(received_bytes.load()) * 100) / total_bytes);
}

void GameFileTransferClient::WriterThread() {
	Output::Debug("GameFileTransfer: Writer thread started, dest={}", dest_path);

	// Create destination directory
	std::error_code ec;
	fs::create_directories(dest_path, ec);
	if (ec) {
		std::lock_guard<std::mutex> lock(error_mutex);
		error_msg = "Failed to create directory: " + dest_path;
		has_error = true;
		writer_running = false;
		return;
	}

	while (!writer_stop) {
		FileWriteChunk chunk;
		bool has_chunk = false;

		{
			std::lock_guard<std::mutex> lock(queue_mutex);
			if (!write_queue.empty()) {
				chunk = std::move(write_queue.front());
				write_queue.pop_front();
				has_chunk = true;
			}
		}

		if (has_chunk) {
			// Sanitize the relative path to prevent directory traversal
			std::string safe_path = chunk.relative_path;
			// Remove any leading slashes
			while (!safe_path.empty() && (safe_path[0] == '/' || safe_path[0] == '\\')) {
				safe_path.erase(0, 1);
			}
			// Reject paths containing Windows drive letters or NTFS streams
			if (safe_path.find(':') != std::string::npos) {
				Output::Warning("GameFileTransfer: Rejected path containing colon: {}", chunk.relative_path);
				continue;
			}
			// Reject paths with ".." as a full path component
			if (safe_path == ".." ||
				safe_path.find("../") == 0 || safe_path.find("..\\") == 0 ||
				safe_path.find("/../") != std::string::npos || safe_path.find("\\..\\") != std::string::npos ||
				safe_path.find("/..") == safe_path.size() - 3 || safe_path.find("\\..") == safe_path.size() - 3) {
				Output::Warning("GameFileTransfer: Rejected unsafe path: {}", chunk.relative_path);
				continue;
			}

			std::string full_path = dest_path + "/" + safe_path;

			// Double-check: resolve and verify containment within dest_path
			fs::path normalized = fs::path(full_path).lexically_normal();
			fs::path dest = fs::path(dest_path).lexically_normal();
			auto [dest_end, _] = std::mismatch(dest.begin(), dest.end(), normalized.begin(), normalized.end());
			if (dest_end != dest.end()) {
				Output::Warning("GameFileTransfer: Path escapes destination: {}", chunk.relative_path);
				continue;
			}

			// Create parent directories
			auto parent = fs::path(full_path).parent_path();
			fs::create_directories(parent, ec);

			// Append data to file
			std::ofstream ofs(full_path, std::ios::binary | std::ios::app);
			if (!ofs.is_open()) {
				std::lock_guard<std::mutex> lock(error_mutex);
				error_msg = "Failed to write: " + safe_path;
				has_error = true;
				writer_running = false;
				return;
			}

			if (!chunk.data.empty()) {
				ofs.write(reinterpret_cast<const char*>(chunk.data.data()),
					static_cast<std::streamsize>(chunk.data.size()));
			}
			ofs.close();
		} else {
			// No chunks available
			if (transfer_complete) {
				// Check if queue is truly empty
				std::lock_guard<std::mutex> lock(queue_mutex);
				if (write_queue.empty()) {
					break; // All done
				}
			}
			// Sleep briefly to avoid busy-waiting
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

	Output::Debug("GameFileTransfer: Writer thread finished");
	writer_running = false;
}

} // namespace Chaos
