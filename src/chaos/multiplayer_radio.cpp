/*
 * Chaos Fork: Multiplayer radio implementation.
 */

#include "chaos/multiplayer_radio.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "audio.h"
#include "filefinder.h"
#include "game_system.h"
#include "main_data.h"
#include "output.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <lcf/rpg/music.h>

namespace Chaos {

namespace {

constexpr size_t kMaxCustomMusicSize = 15 * 1024 * 1024;
constexpr size_t kRadioChunkSize = 16 * 1024;

std::string Lower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

bool IsSupportedCustomExtension(const std::string& extension) {
	const auto ext = Lower(extension);
	return ext == ".mp3" || ext == ".ogg" || ext == ".wav" ||
		ext == ".mid" || ext == ".midi";
}

std::string Stem(const std::string& filename) {
	const auto name = std::filesystem::u8path(filename).filename().u8string();
	const auto dot = name.rfind('.');
	return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

MultiplayerRadio& MultiplayerRadio::Instance() {
	static MultiplayerRadio instance;
	return instance;
}

void MultiplayerRadio::Start() {
	if (active) return;
	active = true;
	next_track_id = 1;
	next_upload_token = 1;
	queue_revision = 0;
	playing_track_id = 0;
	playback_grace_frames = 0;
	queue.clear();
	custom_data.clear();
	pending_uploads.clear();
	incoming_downloads.clear();
	saved_bgm_valid = false;
	RefreshAvailableTracks();
}

void MultiplayerRadio::Stop() {
	if (!active) return;
	if (!queue.empty()) RestoreGameMusic();
	active = false;
	queue.clear();
	custom_data.clear();
	pending_uploads.clear();
	incoming_downloads.clear();
	playing_track_id = 0;
	saved_bgm_valid = false;
	Audio().BGM_SetLooping(true);
}

void MultiplayerRadio::OnMapLoaded() {
	if (!active || queue.empty() || !Main_Data::game_system) return;
	const auto& music = Main_Data::game_system->GetCurrentBGM();
	saved_bgm.name = music.name;
	saved_bgm.volume = music.volume;
	saved_bgm.tempo = music.tempo;
	saved_bgm.fadein = music.fadein;
	saved_bgm.balance = music.balance;
	saved_bgm_valid = true;
	Audio().BGM_Stop();
	playing_track_id = 0;
}

void MultiplayerRadio::RefreshAvailableTracks() {
	available_tracks.clear();
	auto fs = FileFinder::Game();
	if (!fs) return;

	auto* entries = fs.ListDirectory("Music");
	if (!entries) return;
	for (auto& [key, entry] : *entries) {
		if (entry.type != DirectoryTree::FileType::Regular) continue;
		const auto dot = entry.name.rfind('.');
		if (dot == std::string::npos) continue;
		const auto extension = Lower(entry.name.substr(dot));
		if (extension != ".mp3" && extension != ".ogg" && extension != ".wav" &&
			extension != ".mid" && extension != ".midi" && extension != ".opus") {
			continue;
		}
		RadioTrack track;
		track.name = Stem(entry.name);
		track.path = entry.name;
		available_tracks.push_back(std::move(track));
	}
	std::sort(available_tracks.begin(), available_tracks.end(), [](const auto& lhs, const auto& rhs) {
		return Lower(lhs.name) < Lower(rhs.name);
	});
}

bool MultiplayerRadio::SubmitGameTrack(size_t index) {
	if (index >= available_tracks.size()) return false;
	auto& net = NetManager::Instance();
	if (net.IsHost()) {
		QueueGameTrack(available_tracks[index].path);
	} else {
		PacketWriter packet(PacketType::RadioAddRequest);
		packet.write(net.GetLocalPeerId());
		packet.write(available_tracks[index].path);
		net.SendToServer(packet, true);
	}
	return true;
}

bool MultiplayerRadio::SubmitCustomMusic(const std::string& filename) {
	const auto path = std::filesystem::u8path(filename);
	const auto extension = Lower(path.extension().u8string());
	if (!IsSupportedCustomExtension(extension)) return false;

	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec || size == 0 || size > kMaxCustomMusicSize) return false;

	std::vector<uint8_t> data(static_cast<size_t>(size));
	std::ifstream input(path, std::ios::binary);
	if (!input || !input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
		return false;
	}

	const auto name = path.filename().u8string();
	auto& net = NetManager::Instance();
	const uint32_t token = next_upload_token++;
	if (net.IsHost()) {
		AcceptCustomUpload(net.GetLocalPeerId(), token, name, extension, std::move(data));
		return true;
	}

	PacketWriter begin(PacketType::RadioCustomBegin);
	begin.write(net.GetLocalPeerId());
	begin.write(token);
	begin.write(name);
	begin.write(extension);
	begin.write(static_cast<uint32_t>(data.size()));
	net.SendToServer(begin, true);

	for (size_t offset = 0; offset < data.size();) {
		const auto chunk_size = static_cast<uint16_t>(std::min(kRadioChunkSize, data.size() - offset));
		PacketWriter chunk(PacketType::RadioCustomChunk);
		chunk.write(net.GetLocalPeerId());
		chunk.write(token);
		chunk.write(static_cast<uint32_t>(offset));
		chunk.write(chunk_size);
		chunk.writeBytes(data.data() + offset, chunk_size);
		net.SendToServer(chunk, true);
		offset += chunk_size;
	}

	PacketWriter complete(PacketType::RadioCustomComplete);
	complete.write(net.GetLocalPeerId());
	complete.write(token);
	net.SendToServer(complete, true);
	return true;
}

void MultiplayerRadio::QueueGameTrack(const std::string& path) {
	const auto dot = path.rfind('.');
	const auto lookup_name = dot == std::string::npos ? path : path.substr(0, dot);
	if (!FileFinder::Game().OpenFile("Music", lookup_name, FileFinder::MUSIC_TYPES)) return;
	RadioTrack track;
	track.id = next_track_id++;
	track.name = Stem(path);
	track.path = lookup_name;
	AddTrack(std::move(track));
}

void MultiplayerRadio::AcceptCustomUpload(uint16_t sender_id, uint32_t token, std::string name,
		std::string extension, std::vector<uint8_t> data) {
	if (data.empty() || data.size() > kMaxCustomMusicSize || !IsSupportedCustomExtension(extension)) return;
	const uint32_t track_id = next_track_id++;
	if (!WriteCustomFile(track_id, extension, data)) return;

	custom_data[track_id] = data;
	BroadcastCustomTrack(track_id, name, extension, data);

	RadioTrack track;
	track.id = track_id;
	track.custom = true;
	track.name = Stem(name);
	track.path = "Radio/" + std::to_string(track_id) + extension;
	AddTrack(std::move(track));
	Output::Debug("Radio: accepted custom track {} from peer {}", track_id, sender_id);
}

void MultiplayerRadio::AddTrack(RadioTrack track) {
	if (queue.empty() && Main_Data::game_system && !saved_bgm_valid) {
		const auto& music = Main_Data::game_system->GetCurrentBGM();
		saved_bgm.name = music.name;
		saved_bgm.volume = music.volume;
		saved_bgm.tempo = music.tempo;
		saved_bgm.fadein = music.fadein;
		saved_bgm.balance = music.balance;
		saved_bgm_valid = true;
	}
	queue.push_back(std::move(track));
	BroadcastQueue();
}

void MultiplayerRadio::BroadcastQueue() {
	++queue_revision;
	PacketWriter packet(PacketType::RadioQueueSync);
	packet.write(queue_revision);
	packet.write(static_cast<uint16_t>(std::min<size_t>(queue.size(), UINT16_MAX)));
	for (size_t i = 0; i < queue.size() && i < UINT16_MAX; ++i) {
		const auto& track = queue[i];
		packet.write(track.id);
		packet.write(static_cast<uint8_t>(track.custom ? 1 : 0));
		packet.write(track.name);
		packet.write(track.path);
	}
	NetManager::Instance().Broadcast(packet, true);
}

void MultiplayerRadio::ApplyQueue(const std::deque<RadioTrack>& new_queue) {
	const uint32_t old_front = queue.empty() ? 0 : queue.front().id;
	const uint32_t new_front = new_queue.empty() ? 0 : new_queue.front().id;
	if (queue.empty() && !new_queue.empty() && Main_Data::game_system && !saved_bgm_valid) {
		const auto& music = Main_Data::game_system->GetCurrentBGM();
		saved_bgm.name = music.name;
		saved_bgm.volume = music.volume;
		saved_bgm.tempo = music.tempo;
		saved_bgm.fadein = music.fadein;
		saved_bgm.balance = music.balance;
		saved_bgm_valid = true;
	}
	queue = new_queue;
	if (new_queue.empty()) {
		playing_track_id = 0;
		if (old_front != 0 || saved_bgm_valid) RestoreGameMusic();
		else Audio().BGM_SetLooping(true);
	} else if (old_front != new_front) {
		playing_track_id = 0;
	}
}

void MultiplayerRadio::StartTrack(const RadioTrack& track) {
	Filesystem_Stream::InputStream stream;
	if (track.custom) {
		stream = FileFinder::Save().OpenFile(track.path);
	} else {
		stream = FileFinder::Game().OpenFile("Music", track.path, FileFinder::MUSIC_TYPES);
	}
	if (!stream) {
		Output::Warning("Radio: unable to open {}", track.path);
		playing_track_id = track.id;
		playback_grace_frames = 0;
		return;
	}
	Audio().BGM_Play(std::move(stream), 100, 100, 0, 50);
	Audio().BGM_SetLooping(false);
	playing_track_id = track.id;
	playback_grace_frames = 60;
}

void MultiplayerRadio::AdvanceTrack() {
	if (!NetManager::Instance().IsHost() || queue.empty()) return;
	Audio().BGM_Stop();
	playing_track_id = 0;
	queue.pop_front();
	BroadcastQueue();
}

void MultiplayerRadio::RestoreGameMusic() {
	Audio().BGM_Stop();
	Audio().BGM_SetLooping(true);
	if (!saved_bgm_valid || !Main_Data::game_system) return;
	const auto music = saved_bgm;
	saved_bgm_valid = false;
	Main_Data::game_system->BgmStop();
	if (!music.name.empty() && music.name != "(OFF)") {
		lcf::rpg::Music restored;
		restored.name = music.name;
		restored.volume = music.volume;
		restored.tempo = music.tempo;
		restored.fadein = music.fadein;
		restored.balance = music.balance;
		Main_Data::game_system->BgmPlay(restored);
	}
}

void MultiplayerRadio::Update() {
	if (!active || queue.empty()) return;
	if (playing_track_id != queue.front().id) {
		StartTrack(queue.front());
		return;
	}
	if (!NetManager::Instance().IsHost()) return;
	if (playback_grace_frames > 0) {
		--playback_grace_frames;
	} else if (!Audio().BGM_IsPlaying()) {
		AdvanceTrack();
	}
}

bool MultiplayerRadio::WriteCustomFile(uint32_t track_id, const std::string& extension,
		const std::vector<uint8_t>& data) const {
	auto fs = FileFinder::Save();
	if (!fs || !fs.MakeDirectory("Radio", true)) return false;
	const auto filename = "Radio/" + std::to_string(track_id) + extension;
	auto output = fs.OpenOutputStream(filename, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!output) return false;
	output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	output.Close();
	fs.ClearCache();
	return static_cast<bool>(output);
}

void MultiplayerRadio::BroadcastCustomTrack(uint32_t track_id, const std::string& name,
		const std::string& extension, const std::vector<uint8_t>& data) {
	PacketWriter begin(PacketType::RadioCustomBegin);
	begin.write(track_id);
	begin.write(name);
	begin.write(extension);
	begin.write(static_cast<uint32_t>(data.size()));
	NetManager::Instance().Broadcast(begin, true);

	for (size_t offset = 0; offset < data.size();) {
		const auto chunk_size = static_cast<uint16_t>(std::min(kRadioChunkSize, data.size() - offset));
		PacketWriter chunk(PacketType::RadioCustomChunk);
		chunk.write(track_id);
		chunk.write(static_cast<uint32_t>(offset));
		chunk.write(chunk_size);
		chunk.writeBytes(data.data() + offset, chunk_size);
		NetManager::Instance().Broadcast(chunk, true);
		offset += chunk_size;
	}
	PacketWriter complete(PacketType::RadioCustomComplete);
	complete.write(track_id);
	NetManager::Instance().Broadcast(complete, true);
}

void MultiplayerRadio::SendCustomTrackTo(uint16_t peer_id, uint32_t track_id,
		const RadioTrack& track) {
	auto it = custom_data.find(track_id);
	if (it == custom_data.end()) return;
	const auto& data = it->second;
	const auto dot = track.path.rfind('.');
	const auto extension = dot == std::string::npos ? std::string() : track.path.substr(dot);
	PacketWriter begin(PacketType::RadioCustomBegin);
	begin.write(track_id);
	begin.write(track.name);
	begin.write(extension);
	begin.write(static_cast<uint32_t>(data.size()));
	NetManager::Instance().SendTo(peer_id, begin, true);
	for (size_t offset = 0; offset < data.size();) {
		const auto chunk_size = static_cast<uint16_t>(std::min(kRadioChunkSize, data.size() - offset));
		PacketWriter chunk(PacketType::RadioCustomChunk);
		chunk.write(track_id);
		chunk.write(static_cast<uint32_t>(offset));
		chunk.write(chunk_size);
		chunk.writeBytes(data.data() + offset, chunk_size);
		NetManager::Instance().SendTo(peer_id, chunk, true);
		offset += chunk_size;
	}
	PacketWriter complete(PacketType::RadioCustomComplete);
	complete.write(track_id);
	NetManager::Instance().SendTo(peer_id, complete, true);
}

void MultiplayerRadio::SendQueueTo(uint16_t peer_id) {
	if (!NetManager::Instance().IsHost()) return;
	for (const auto& track : queue) {
		if (track.custom) SendCustomTrackTo(peer_id, track.id, track);
	}
	PacketWriter packet(PacketType::RadioQueueSync);
	packet.write(queue_revision);
	packet.write(static_cast<uint16_t>(std::min<size_t>(queue.size(), UINT16_MAX)));
	for (size_t i = 0; i < queue.size() && i < UINT16_MAX; ++i) {
		const auto& track = queue[i];
		packet.write(track.id);
		packet.write(static_cast<uint8_t>(track.custom ? 1 : 0));
		packet.write(track.name);
		packet.write(track.path);
	}
	NetManager::Instance().SendTo(peer_id, packet, true);
}

void MultiplayerRadio::HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	const auto type = reader.readType();
	auto& net = NetManager::Instance();

	if (type == PacketType::RadioQueueSync) {
		if (net.IsHost()) return;
		reader.readU32();
		const auto count = reader.readU16();
		std::deque<RadioTrack> received;
		for (uint16_t i = 0; i < count; ++i) {
			RadioTrack track;
			track.id = reader.readU32();
			track.custom = reader.readU8() != 0;
			track.name = reader.readString();
			track.path = reader.readString();
			received.push_back(std::move(track));
		}
		if (reader.ok()) ApplyQueue(received);
		return;
	}

	if (type == PacketType::RadioAddRequest) {
		if (!net.IsHost()) return;
		const auto issuer = reader.readU16();
		const auto path = reader.readString();
		if (!reader.ok() || issuer != sender_id || path.size() > 512) return;
		for (const auto& track : available_tracks) {
			if (track.path == path) {
				QueueGameTrack(path);
				return;
			}
		}
		return;
	}

	if (type == PacketType::RadioCustomBegin) {
		if (net.IsHost()) {
			const auto issuer = reader.readU16();
			const auto token = reader.readU32();
			const auto name = reader.readString();
			const auto extension = Lower(reader.readString());
			const auto total = reader.readU32();
			if (!reader.ok() || issuer != sender_id || total == 0 || total > kMaxCustomMusicSize ||
				!IsSupportedCustomExtension(extension)) return;
			pending_uploads[sender_id] = {token, name, extension, {}};
			pending_uploads[sender_id].data.reserve(total);
		} else {
			const auto track_id = reader.readU32();
			IncomingDownload download;
			download.name = reader.readString();
			download.extension = Lower(reader.readString());
			const auto total = reader.readU32();
			if (!reader.ok() || total == 0 || total > kMaxCustomMusicSize ||
				!IsSupportedCustomExtension(download.extension)) return;
			download.data.resize(total);
			incoming_downloads[track_id] = std::move(download);
		}
		return;
	}

	if (type == PacketType::RadioCustomChunk) {
		if (net.IsHost()) {
			const auto issuer = reader.readU16();
			const auto token = reader.readU32();
			const auto offset = reader.readU32();
			const auto size = reader.readU16();
			const auto bytes = reader.readBytes(size);
			auto it = pending_uploads.find(sender_id);
			if (!reader.ok() || !bytes || it == pending_uploads.end() || issuer != sender_id ||
				it->second.token != token || offset != it->second.data.size() ||
				it->second.data.size() + size > kMaxCustomMusicSize) return;
			it->second.data.insert(it->second.data.end(), bytes, bytes + size);
		} else {
			const auto track_id = reader.readU32();
			const auto offset = reader.readU32();
			const auto size = reader.readU16();
			const auto bytes = reader.readBytes(size);
			auto it = incoming_downloads.find(track_id);
			if (!reader.ok() || !bytes || it == incoming_downloads.end() || offset != it->second.received ||
				offset + size > it->second.data.size()) return;
			std::copy(bytes, bytes + size, it->second.data.begin() + offset);
			it->second.received += size;
		}
		return;
	}

	if (type == PacketType::RadioCustomComplete) {
		if (net.IsHost()) {
			const auto issuer = reader.readU16();
			const auto token = reader.readU32();
			auto it = pending_uploads.find(sender_id);
			if (!reader.ok() || issuer != sender_id || it == pending_uploads.end() || it->second.token != token) return;
			if (it->second.data.empty() || it->second.data.size() > kMaxCustomMusicSize) return;
			AcceptCustomUpload(sender_id, token, it->second.name, it->second.extension, std::move(it->second.data));
			pending_uploads.erase(it);
		} else {
			const auto track_id = reader.readU32();
			auto it = incoming_downloads.find(track_id);
			if (!reader.ok() || it == incoming_downloads.end() || it->second.received != it->second.data.size()) return;
			WriteCustomFile(track_id, it->second.extension, it->second.data);
			incoming_downloads.erase(it);
		}
	}
}

} // namespace Chaos
