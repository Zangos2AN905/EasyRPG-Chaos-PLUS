/*
 * Chaos Fork: Game File Transfer
 * Transfers game files from host to client over the multiplayer network.
 * Host enumerates and sends files in chunks; client receives and writes to disk.
 */

#ifndef EP_CHAOS_GAME_FILE_TRANSFER_H
#define EP_CHAOS_GAME_FILE_TRANSFER_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <cstdint>

namespace Chaos {

class NetManager;

// A chunk of file data queued for writing
struct FileWriteChunk {
	std::string relative_path;
	std::vector<uint8_t> data;
	bool is_last_chunk; // Last chunk for this file (append + close)
};

/**
 * Manages sending game files from host to a requesting client.
 * Created per-client on the host side when they request files.
 */
class GameFileTransferHost {
public:
	GameFileTransferHost();
	~GameFileTransferHost();

	/** Begin enumerating files in the game directory (background thread). */
	void Start(const std::string& game_path);

	/** Send queued file chunks to the target peer. Call each frame.
	 *  Returns true while there is still data to send. */
	bool SendChunks(NetManager& net, uint16_t target_peer_id, int max_chunks_per_frame = 4);

	/** Whether enumeration is done and total sizes are known. */
	bool IsInfoReady() const { return info_ready; }

	uint16_t GetTotalFiles() const { return total_files; }
	int32_t GetTotalBytes() const { return total_bytes; }

	/** Whether all files have been sent. */
	bool IsDone() const { return done; }

private:
	struct FileEntry {
		std::string relative_path;
		std::string absolute_path;
		int64_t size = 0;
	};

	void EnumerateThread(std::string game_path);
	void EnumerateDirectory(const std::string& base_path, const std::string& rel_prefix);

	std::thread enum_thread;
	std::mutex files_mutex;
	std::vector<FileEntry> files;
	std::atomic<bool> info_ready{false};
	std::atomic<bool> done{false};
	std::atomic<uint16_t> total_files{0};
	std::atomic<int32_t> total_bytes{0};

	// Sending state (main thread only)
	size_t current_file_idx = 0;
	int64_t current_file_offset = 0;
	bool info_sent = false;

	static constexpr size_t CHUNK_SIZE = 16384; // 16KB per chunk
};

/**
 * Manages receiving game files on the client side.
 * Writes received data to disk in a background thread.
 */
class GameFileTransferClient {
public:
	GameFileTransferClient();
	~GameFileTransferClient();

	/** Set the destination directory where game files will be written. */
	void SetDestination(const std::string& dest_path);

	/** Called when GameFileInfo packet arrives. */
	void OnFileInfo(uint16_t file_count, int32_t total_bytes);

	/** Called when GameFileData packet arrives. */
	void OnFileData(const std::string& relative_path, const uint8_t* data, size_t len, bool is_last_chunk);

	/** Called when GameFileDone packet arrives. */
	void OnTransferDone();

	/** Process the write queue (call each frame for thread management). */
	void Update();

	// Progress
	bool IsInfoReceived() const { return info_received; }
	uint16_t GetTotalFiles() const { return total_files; }
	int32_t GetTotalBytes() const { return total_bytes; }
	int32_t GetReceivedBytes() const { return received_bytes.load(); }
	uint16_t GetReceivedFiles() const { return received_files.load(); }
	bool IsComplete() const { return transfer_complete.load(); }
	bool HasError() const { return has_error.load(); }
	std::string GetError() const;
	int GetProgressPercent() const;

private:
	void WriterThread();

	std::string dest_path;
	bool info_received = false;
	uint16_t total_files = 0;
	int32_t total_bytes = 0;
	std::atomic<int32_t> received_bytes{0};
	std::atomic<uint16_t> received_files{0};
	std::atomic<bool> transfer_complete{false};
	std::atomic<bool> has_error{false};
	std::atomic<bool> writer_running{false};
	std::atomic<bool> writer_stop{false};

	std::mutex error_mutex;
	std::string error_msg;

	std::mutex queue_mutex;
	std::deque<FileWriteChunk> write_queue;

	std::thread writer_thread;
};

} // namespace Chaos

#endif
