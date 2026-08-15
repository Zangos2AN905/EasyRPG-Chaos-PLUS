/*
 * Chaos Fork: Multiplayer boombox implementation.
 */

#include "chaos/multiplayer_boombox.h"
#include "chaos/multiplayer_radio.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "chaos/multiplayer_state.h"
#include "audio.h"
#include "filefinder.h"
#include "game_map.h"
#include "game_player.h"
#include "main_data.h"
#include "game_system.h"
#include "lcf/rpg/music.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <filesystem>
#include <fstream>
#include <cctype>

namespace Chaos {

namespace {
constexpr int kMaxDistance = 12;
constexpr int kMaxChannels = 16;
constexpr size_t kMaxCustomMusicSize = 15 * 1024 * 1024;
constexpr size_t kChunkSize = 16 * 1024;

std::string MusicLookupName(const std::string& path) {
	const auto dot = path.rfind('.');
	return dot == std::string::npos ? path : path.substr(0, dot);
}

bool SupportedExtension(const std::string& ext) {
	std::string lower = ext;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return lower == ".mp3" || lower == ".ogg" || lower == ".wav" || lower == ".mid" || lower == ".midi";
}

} // namespace

MultiplayerBoombox& MultiplayerBoombox::Instance() {
	static MultiplayerBoombox instance;
	return instance;
}

void MultiplayerBoombox::Start() {
	if (active) return;
	active = true;
	states.clear();
	slots.clear();
	playing_paths.clear();
	pending_uploads.clear();
	incoming_uploads.clear();
	custom_paths.clear();
	restoring_game_music = false;
	saved_bgm_valid = false;
}

void MultiplayerBoombox::Stop() {
	if (!active) return;
	for (const auto& [peer_id, slot] : slots) Audio().BGS_StopChannel(slot);
	if (HasActiveSource()) RestoreGameMusic();
	active = false;
	states.clear();
	slots.clear();
	playing_paths.clear();
	pending_uploads.clear();
	incoming_uploads.clear();
	custom_paths.clear();
	restoring_game_music = false;
	saved_bgm_valid = false;
}

bool MultiplayerBoombox::HasActiveSource() const {
	for (const auto& [peer_id, state] : states) {
		if (state.active) return true;
	}
	return false;
}

int MultiplayerBoombox::GetSlot(uint16_t peer_id) {
	auto it = slots.find(peer_id);
	if (it != slots.end()) return it->second;
	std::set<int> used;
	for (const auto& [id, slot] : slots) used.insert(slot);
	for (int slot = 0; slot < kMaxChannels; ++slot) {
		if (used.count(slot) == 0) return slots[peer_id] = slot;
	}
	return -1;
}

bool MultiplayerBoombox::SubmitTrack(size_t index) {
	const auto& tracks = MultiplayerRadio::Instance().GetAvailableTracks();
	if (index >= tracks.size()) return false;
	const auto path = MusicLookupName(tracks[index].path);
	auto& net = NetManager::Instance();
	if (net.IsHost()) SetState(net.GetLocalPeerId(), true, path, true);
	else SendState(net.GetLocalPeerId(), true, path, true);
	return true;
}

bool MultiplayerBoombox::SubmitCustomMusic(const std::string& filename) {
	const auto path = std::filesystem::u8path(filename);
	const auto extension = path.extension().u8string();
	if (!SupportedExtension(extension)) return false;
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec || size == 0 || size > kMaxCustomMusicSize) return false;
	std::vector<uint8_t> data(static_cast<size_t>(size));
	std::ifstream input(path, std::ios::binary);
	if (!input || !input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) return false;
	auto& net = NetManager::Instance();
	const uint32_t token = static_cast<uint32_t>(std::hash<std::string>{}(filename));
	if (net.IsHost()) {
		AcceptCustom(net.GetLocalPeerId(), token, path.filename().u8string(), extension, std::move(data));
		return true;
	}
	PacketWriter begin(PacketType::BoomboxCustomBegin);
	begin.write(net.GetLocalPeerId()); begin.write(token); begin.write(path.filename().u8string());
	begin.write(extension); begin.write(static_cast<uint32_t>(data.size()));
	net.SendToServer(begin, true);
	for (size_t offset = 0; offset < data.size();) {
		const auto size_now = static_cast<uint16_t>(std::min(kChunkSize, data.size() - offset));
		PacketWriter chunk(PacketType::BoomboxCustomChunk);
		chunk.write(net.GetLocalPeerId()); chunk.write(token); chunk.write(static_cast<uint32_t>(offset)); chunk.write(size_now);
		chunk.writeBytes(data.data() + offset, size_now); net.SendToServer(chunk, true); offset += size_now;
	}
	PacketWriter complete(PacketType::BoomboxCustomComplete);
	complete.write(net.GetLocalPeerId()); complete.write(token); net.SendToServer(complete, true);
	return true;
}

void MultiplayerBoombox::StopLocal() {
	auto& net = NetManager::Instance();
	if (net.IsHost()) SetState(net.GetLocalPeerId(), false, {}, true);
	else SendState(net.GetLocalPeerId(), false, {}, true);
}

bool MultiplayerBoombox::WriteCustomFile(const std::string& path, const std::vector<uint8_t>& data) const {
	auto fs = FileFinder::Save();
	if (!fs || !fs.MakeDirectory("Radio", true)) return false;
	auto out = fs.OpenOutputStream(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out) return false;
	out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	out.Close(); fs.ClearCache(); return true;
}

void MultiplayerBoombox::AcceptCustom(uint16_t sender_id, uint32_t token, const std::string& name,
		const std::string& extension, std::vector<uint8_t> data) {
	if (data.empty() || data.size() > kMaxCustomMusicSize || !SupportedExtension(extension)) return;
	const auto path = "Radio/Boombox_" + std::to_string(sender_id) + "_" + std::to_string(token) + extension;
	if (!WriteCustomFile(path, data)) return;
	custom_paths.insert(path);
	PacketWriter begin(PacketType::BoomboxCustomBegin); begin.write(path); begin.write(static_cast<uint32_t>(data.size()));
	NetManager::Instance().Broadcast(begin, true);
	for (size_t offset = 0; offset < data.size();) {
		const auto size_now = static_cast<uint16_t>(std::min(kChunkSize, data.size() - offset));
		PacketWriter chunk(PacketType::BoomboxCustomChunk); chunk.write(path); chunk.write(static_cast<uint32_t>(offset)); chunk.write(size_now);
		chunk.writeBytes(data.data() + offset, size_now); NetManager::Instance().Broadcast(chunk, true); offset += size_now;
	}
	PacketWriter complete(PacketType::BoomboxCustomComplete); complete.write(path); NetManager::Instance().Broadcast(complete, true);
	SetState(sender_id, true, path, true);
	(void)name;
}

void MultiplayerBoombox::SendState(uint16_t peer_id, bool is_active, const std::string& path, bool to_server) {
	PacketWriter packet(PacketType::BoomboxState);
	packet.write(peer_id);
	packet.write(static_cast<uint8_t>(is_active ? 1 : 0));
	packet.write(path);
	if (to_server) NetManager::Instance().SendToServer(packet, true);
	else NetManager::Instance().Broadcast(packet, true);
}

void MultiplayerBoombox::SetState(uint16_t peer_id, bool is_active, const std::string& path, bool broadcast) {
	const bool had_active_source = HasActiveSource();
	if (is_active) {
		bool valid = false;
		for (const auto& track : MultiplayerRadio::Instance().GetAvailableTracks()) {
			if (MusicLookupName(track.path) == path) {
				valid = true;
				break;
			}
		}
		if (!valid && custom_paths.count(path) == 0) return;
	}
	states[peer_id] = {is_active, path};
	if (!had_active_source && is_active && Main_Data::game_system && !saved_bgm_valid) {
		const auto& music = Main_Data::game_system->GetCurrentBGM();
		saved_bgm.name = music.name;
		saved_bgm.volume = music.volume;
		saved_bgm.tempo = music.tempo;
		saved_bgm.fadein = music.fadein;
		saved_bgm.balance = music.balance;
		saved_bgm_valid = true;
		Audio().BGM_Stop();
	}
	if (broadcast) SendState(peer_id, is_active, path, false);
	if (had_active_source && !HasActiveSource()) RestoreGameMusic();
}

void MultiplayerBoombox::RestoreGameMusic() {
	restoring_game_music = true;
	Audio().BGM_Stop();
	Audio().BGM_SetLooping(true);
	if (saved_bgm_valid && Main_Data::game_system) {
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
	restoring_game_music = false;
}

void MultiplayerBoombox::HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	const auto type = reader.readType();
	if (type == PacketType::BoomboxCustomBegin) {
		if (NetManager::Instance().IsHost()) {
			const auto issuer = reader.readU16();
			const auto token = reader.readU32();
			const auto name = reader.readString();
			const auto extension = reader.readString();
			const auto total = reader.readU32();
			if (!reader.ok() || issuer != sender_id || total == 0 || total > kMaxCustomMusicSize || !SupportedExtension(extension)) return;
			pending_uploads[sender_id] = {token, name, extension, {}};
			pending_uploads[sender_id].data.reserve(total);
		} else {
			const auto path = reader.readString();
			const auto total = reader.readU32();
			if (!reader.ok() || total == 0 || total > kMaxCustomMusicSize) return;
			incoming_uploads[path] = {std::vector<uint8_t>(total), 0};
		}
		return;
	}
	if (type == PacketType::BoomboxCustomChunk) {
		if (NetManager::Instance().IsHost()) {
			const auto issuer = reader.readU16();
			const auto token = reader.readU32();
			const auto offset = reader.readU32();
			const auto size = reader.readU16();
			const auto bytes = reader.readBytes(size);
			auto it = pending_uploads.find(sender_id);
			if (!reader.ok() || !bytes || it == pending_uploads.end() || issuer != sender_id || it->second.token != token ||
				offset != it->second.data.size() || it->second.data.size() + size > kMaxCustomMusicSize) return;
			it->second.data.insert(it->second.data.end(), bytes, bytes + size);
		} else {
			const auto path = reader.readString();
			const auto offset = reader.readU32();
			const auto size = reader.readU16();
			const auto bytes = reader.readBytes(size);
			auto it = incoming_uploads.find(path);
			if (!reader.ok() || !bytes || it == incoming_uploads.end() || offset != it->second.received || offset + size > it->second.data.size()) return;
			std::copy(bytes, bytes + size, it->second.data.begin() + offset);
			it->second.received += size;
		}
		return;
	}
	if (type == PacketType::BoomboxCustomComplete) {
		if (NetManager::Instance().IsHost()) {
			const auto issuer = reader.readU16();
			const auto token = reader.readU32();
			auto it = pending_uploads.find(sender_id);
			if (!reader.ok() || issuer != sender_id || it == pending_uploads.end() || it->second.token != token) return;
			AcceptCustom(sender_id, token, it->second.name, it->second.extension, std::move(it->second.data));
			pending_uploads.erase(it);
		} else {
			const auto path = reader.readString();
			auto it = incoming_uploads.find(path);
			if (!reader.ok() || it == incoming_uploads.end() || it->second.received != it->second.data.size()) return;
			if (WriteCustomFile(path, it->second.data)) custom_paths.insert(path);
			incoming_uploads.erase(it);
		}
		return;
	}
	reader = PacketReader(data, len);
	reader.readType();
	const auto peer_id = reader.readU16();
	const bool is_active = reader.readU8() != 0;
	const auto path = reader.readString();
	if (!reader.ok()) return;

	auto& net = NetManager::Instance();
	if (net.IsHost()) {
		if (sender_id != peer_id) return;
		SetState(peer_id, is_active, path, true);
	} else {
		SetState(peer_id, is_active, path, false);
	}
}

void MultiplayerBoombox::SendStatesTo(uint16_t peer_id) {
	if (!NetManager::Instance().IsHost()) return;
	for (const auto& [source_id, state] : states) {
		PacketWriter packet(PacketType::BoomboxState);
		packet.write(source_id);
		packet.write(static_cast<uint8_t>(state.active ? 1 : 0));
		packet.write(state.path);
		NetManager::Instance().SendTo(peer_id, packet, true);
	}
}

void MultiplayerBoombox::Update() {
	if (!active || !Main_Data::game_player) return;
	const int local_x = Main_Data::game_player->GetX();
	const int local_y = Main_Data::game_player->GetY();
	const int local_map = Game_Map::GetMapId();

	for (const auto& [peer_id, state] : states) {
		const int slot = GetSlot(peer_id);
		if (slot < 0) continue;
		Game_Character* source = nullptr;
		if (peer_id == NetManager::Instance().GetLocalPeerId()) source = Main_Data::game_player.get();
		else source = MultiplayerState::Instance().GetRemotePlayer(peer_id);
		if (!source || !state.active || source->GetMapId() != local_map) {
			if (!state.active) {
				Audio().BGS_StopChannel(slot);
				playing_paths.erase(peer_id);
			} else {
				Audio().BGS_ChannelVolume(slot, 0);
			}
			continue;
		}

		const int dx = source->GetX() - local_x;
		const int dy = source->GetY() - local_y;
		const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
		const int volume = std::clamp(static_cast<int>((1.0f - distance / kMaxDistance) * 100.0f), 0, 100);
		const int balance = std::clamp(50 + dx * 6, 0, 100);
		if (playing_paths[peer_id] != state.path) {
			auto stream = custom_paths.count(state.path) != 0
				? FileFinder::Save().OpenFile(state.path)
				: FileFinder::Game().OpenFile("Music", state.path, FileFinder::MUSIC_TYPES);
			if (!stream) continue;
			Audio().BGS_PlayChannel(slot, std::move(stream), volume, 100, balance);
			playing_paths[peer_id] = state.path;
		} else {
			Audio().BGS_ChannelVolume(slot, volume);
			Audio().BGS_ChannelBalance(slot, balance);
		}
	}
}

} // namespace Chaos
