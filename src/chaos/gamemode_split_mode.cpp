/*
 * Chaos Fork: Split multiplayer mode implementation.
 */

#include "chaos/gamemode_split_mode.h"

#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "audio.h"
#include "drawable.h"
#include "drawable_mgr.h"
#include "cache.h"
#include "game_actor.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "input.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "rand.h"
#include "spriteset_map.h"
#include "sprite_character.h"
#include "bitmap.h"
#include <lcf/data.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace Chaos {

class SplitOverlay final : public Drawable {
public:
	SplitOverlay()
		: Drawable(Priority_Weather + (1ULL << 48)) {
		DrawableMgr::Register(this);
		SetVisible(false);
	}

	void Draw(Bitmap& dst) override {
		if (!IsVisible()) return;

		const int width = dst.GetWidth();
		const int height = dst.GetHeight();
		if (!white_filter || filter_width != width || filter_height != height) {
			white_filter = Bitmap::Create(width, height, Color(255, 255, 255, 255));
			red_filter = Bitmap::Create(width, height, Color(180, 0, 0, 255));
			filter_width = width;
			filter_height = height;
		}

		// Difference with white inverts the EXE-side map first.
		dst.Blit(0, 0, *white_filter, white_filter->GetRect(),
			Opacity::Opaque(), Bitmap::BlendMode::Difference);
		// Apply a strong red wash after inversion.
		dst.Blit(0, 0, *red_filter, red_filter->GetRect(),
			Opacity(210), Bitmap::BlendMode::Normal);

		for (auto& event : Game_Map::GetEvents()) {
			DrawShadow(dst, &event);
		}
		DrawShadow(dst, Main_Data::game_player.get());
	}

	static void DrawShadow(Bitmap& dst, Game_Character* character) {
		if (!character || !character->IsVisible() || character->GetSpriteName().empty()) return;

		const auto& sprite_name = character->GetSpriteName();
		auto charset = Cache::Charset(sprite_name);
		if (!charset) return;

		const Rect character_rect = Sprite_Character::GetCharacterRect(
			sprite_name, character->GetSpriteIndex(), charset->GetRect());
		const int frame_width = character_rect.width / 3;
		const int frame_height = character_rect.height / 4;
		int frame = character->GetAnimFrame();
		if (frame >= lcf::rpg::EventPage::Frame_middle2) {
			frame = lcf::rpg::EventPage::Frame_middle;
		}
		const int row = std::clamp(character->GetFacing(), 0, 3);
		const Rect source_rect(
			character_rect.x + frame * frame_width,
			character_rect.y + row * frame_height,
			frame_width, frame_height);
		auto shadow = Cache::SpriteEffect(
			charset, source_rect, false, false, Tone(0, 0, 0, 0), Color());
		if (shadow) {
			dst.Blit(character->GetScreenX() - frame_width / 2,
				character->GetScreenY() - frame_height,
				*shadow, shadow->GetRect(), Opacity::Opaque());
		}
	}

private:
	BitmapRef white_filter;
	BitmapRef red_filter;
	int filter_width = 0;
	int filter_height = 0;
};

SplitMode& SplitMode::Instance() {
	static SplitMode instance;
	return instance;
}

void SplitMode::Start() {
	if (active) return;

	active = true;
	exe_side = false;
	death_recovery_pending = false;
	current_spriteset = nullptr;
	overlay = nullptr;
	Audio().BGM_Reverb(0);

	auto& net = NetManager::Instance();
	if (net.IsHost()) {
		SetPeerSide(net.GetLocalPeerId(), Rand::GetRandomNumber(0, 1) != 0, false);
		for (const auto& peer : net.GetPeers()) {
			AssignPeer(peer.peer_id);
		}
	}
}

void SplitMode::Stop() {
	if (!active) return;
	active = false;
	exe_side = false;
	death_recovery_pending = false;
	current_spriteset = nullptr;
	delete overlay;
	overlay = nullptr;
	Audio().BGM_Pitch(100);
	Audio().BGM_Reverb(0);
}

void SplitMode::Update() {
	if (!active || !overlay) return;
	overlay->SetVisible(exe_side);
	if (exe_side) {
		const auto type = Audio().BGM_GetType();
		const bool midi = type.find("midi") != std::string::npos;
		Audio().BGM_Pitch(midi ? 55 : 72);
		Audio().BGM_Reverb(100);
	} else {
		Audio().BGM_Pitch(100);
		Audio().BGM_Reverb(0);
	}
}

void SplitMode::OnMapLoaded(Spriteset_Map* spriteset) {
	current_spriteset = spriteset;
	if (!overlay) {
		// This must be created after the map drawable list becomes active.
		overlay = new SplitOverlay();
	}
	if (death_recovery_pending) {
		death_recovery_pending = false;
	}
	Update();
}

void SplitMode::OnMapUnloaded() {
	current_spriteset = nullptr;
	delete overlay;
	overlay = nullptr;
}

SplitMapDestination SplitMode::GetRandomMapDestination(int excluded_map_id) const {
	std::vector<int> map_ids;
	map_ids.reserve(lcf::Data::treemap.maps.size());
	for (const auto& map : lcf::Data::treemap.maps) {
		if (map.ID > 0 && map.ID != excluded_map_id &&
			map.type == lcf::rpg::TreeMap::MapType_map) {
			map_ids.push_back(map.ID);
		}
	}

	if (map_ids.empty()) {
		return { excluded_map_id, 1, 1 };
	}

	const int index = Rand::GetRandomNumber(0, static_cast<int32_t>(map_ids.size()) - 1);
	const int map_id = map_ids[static_cast<size_t>(index)];
	int x = 1;
	int y = 1;
	if (auto map = Game_Map::LoadMapFile(map_id)) {
		x = std::max(0, map->width / 2);
		y = std::max(0, map->height / 2);
	}
	return { map_id, x, y };
}

void SplitMode::SetLocalExeSide(bool value) {
	auto& net = NetManager::Instance();
	exe_side = value;

	PacketWriter packet(PacketType::SplitSideAssign);
	packet.write(net.GetLocalPeerId());
	packet.write(static_cast<uint8_t>(value ? 1 : 0));
	if (net.IsHost()) {
		net.Broadcast(packet, true);
	} else {
		net.SendToServer(packet, true);
	}
}

bool SplitMode::HandlePlayerDeath() {
	if (!active || exe_side || death_recovery_pending || !Main_Data::game_player) {
		return false;
	}

	death_recovery_pending = true;
	SetLocalExeSide(true);

	// Revive the local party just enough to continue on the EXE side.
	if (Main_Data::game_party) {
		for (auto* actor : Main_Data::game_party->GetActors()) {
			if (actor) actor->SetHp(1);
		}
	}

	const auto destination = GetRandomMapDestination(Game_Map::GetMapId());
	Main_Data::game_player->ReserveTeleport(
		destination.map_id, destination.x, destination.y, -1,
		TeleportTarget::eParallelTeleport);
	Output::Debug("Split: Normal-side death moved player to EXE map {}", destination.map_id);
	return true;
}

void SplitMode::SetPeerSide(uint16_t peer_id, bool value, bool broadcast) {
	auto& net = NetManager::Instance();
	if (peer_id == net.GetLocalPeerId()) {
		exe_side = value;
	}
	if (!broadcast || !net.IsHost()) return;

	PacketWriter packet(PacketType::SplitSideAssign);
	packet.write(peer_id);
	packet.write(static_cast<uint8_t>(value ? 1 : 0));
	net.Broadcast(packet, true);
}

void SplitMode::AssignPeer(uint16_t peer_id) {
	if (!active || !NetManager::Instance().IsHost()) return;
	SetPeerSide(peer_id, Rand::GetRandomNumber(0, 1) != 0, true);
}

void SplitMode::HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len) {
	if (!active) return;

	auto& net = NetManager::Instance();
	PacketReader reader(data, len);
	reader.readType();
	const uint16_t peer_id = reader.readU16();
	const bool value = reader.readU8() != 0;
	if (!reader.ok() || peer_id == 0) return;

	if (net.IsHost()) {
		// Client packets are requests to change that client's side after death.
		if (sender_id == 0 || sender_id != peer_id) return;
		SetPeerSide(peer_id, value, true);
		return;
	}

	if (peer_id == net.GetLocalPeerId()) {
		exe_side = value;
		Update();
		Output::Debug("Split: Assigned local player to {} side", value ? "EXE" : "normal");
	}
}

} // namespace Chaos
