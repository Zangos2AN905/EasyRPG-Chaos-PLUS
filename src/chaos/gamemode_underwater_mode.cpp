/*
 * Chaos Fork: Underwater multiplayer mode implementation.
 */

#include "chaos/gamemode_underwater_mode.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"

#include "bitmap.h"
#include "audio.h"
#include "audio_secache.h"
#include "drawable.h"
#include "drawable_mgr.h"
#include "filefinder.h"
#include "font.h"
#include "game_clock.h"
#include "game_battle.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "game_system.h"
#include "main_data.h"
#include "map_data.h"
#include "output.h"
#include "rand.h"
#include "scene.h"
#include "scene_gameover.h"
#include "spriteset_map.h"
#include "text.h"

#include <algorithm>

namespace Chaos {

class UnderwaterOverlay final : public Drawable {
public:
	explicit UnderwaterOverlay(UnderwaterMode& mode)
		: Drawable(Priority_Weather + (1ULL << 48)), mode(mode) {
		DrawableMgr::Register(this);
		SetVisible(true);
	}

	void Draw(Bitmap& dst) override {
		if (mode.IsActive()) {
			mode.DrawOverlay(dst);
		}
	}

private:
	UnderwaterMode& mode;
};

UnderwaterMode& UnderwaterMode::Instance() {
	static UnderwaterMode instance;
	return instance;
}

void UnderwaterMode::Start() {
	if (active) return;
	active = true;
	current_spriteset = nullptr;
	bubbles.clear();
	spawn_counter = 0;
	last_game_frame = -1;
	ResetOxygen();
	tint_bitmap.reset();
	distortion_bitmap.reset();
	tint_width = 0;
	tint_height = 0;
}

void UnderwaterMode::Stop() {
	if (!active) return;
	active = false;
	current_spriteset = nullptr;
	bubbles.clear();
	last_game_frame = -1;
	overlay.reset();
	if (drowning_music_playing) {
		Audio().BGS_Stop();
	}
	drowning_music_playing = false;
	tint_bitmap.reset();
	distortion_bitmap.reset();
	tint_width = 0;
	tint_height = 0;
}

void UnderwaterMode::OnMapLoaded(Spriteset_Map* spriteset) {
	if (current_spriteset == spriteset) return;
	current_spriteset = spriteset;
	bubbles.clear();
	spawn_counter = 0;
	last_game_frame = -1;
	ResetOxygen();
	if (!overlay) {
		overlay = std::make_unique<UnderwaterOverlay>(*this);
	}
	air_map_id = Game_Map::GetMapId();
	if (NetManager::Instance().IsHost()) {
		GenerateAirPockets();
		BroadcastAirPockets();
	} else if (pending_air_map_id == air_map_id) {
		air_pockets = pending_air_pockets;
		pending_air_pockets.clear();
		pending_air_map_id = 0;
	}
}

void UnderwaterMode::OnMapUnloaded() {
	current_spriteset = nullptr;
	last_game_frame = -1;
	overlay.reset();
	bubbles.clear();
	air_pockets.clear();
	air_map_id = 0;
	if (drowning_music_playing) {
		Audio().BGS_Stop();
	}
	drowning_music_playing = false;
}

void UnderwaterMode::Update() {
	if (!active || !overlay || !Main_Data::game_player) return;

	const auto now = Game_Clock::now();
	if (Game_Battle::IsBattleRunning()) {
		if (!oxygen_paused) {
			oxygen_paused = true;
			oxygen_pause_start_time = now;
		}
		last_game_frame = Game_Clock::GetFrame();
		return;
	}
	if (oxygen_paused) {
		oxygen_start_time += now - oxygen_pause_start_time;
		oxygen_paused = false;
	}

	const int game_frame = Game_Clock::GetFrame();
	if (last_game_frame < 0) {
		last_game_frame = game_frame;
	}
	const int frame_delta = std::max(0, game_frame - last_game_frame);
	last_game_frame = game_frame;

	bool at_air_pocket = false;
	for (const auto& pocket : air_pockets) {
		if (std::abs(Main_Data::game_player->GetX() - pocket.x) <= 1 &&
			std::abs(Main_Data::game_player->GetY() - pocket.y) <= 1) {
			at_air_pocket = true;
			break;
		}
	}
	if (at_air_pocket) {
		if (!air_refilling) {
			air_refilling = true;
			air_refill_duration_ms = Rand::GetRandomNumber(1000, 4000);
			air_refill_start_time = Game_Clock::now();
		} else if (std::chrono::duration_cast<std::chrono::milliseconds>(
			Game_Clock::now() - air_refill_start_time).count() >= air_refill_duration_ms) {
			ResetOxygen();
			air_refilling = false;
		}
	} else {
		air_refilling = false;
	}

	const int fps = std::max(1, Game_Clock::GetTargetGameFps());
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		Game_Clock::now() - oxygen_start_time).count();
	oxygen_frames = std::clamp(
		static_cast<int>(elapsed * fps / 1000), 0, fps * 30);
	if (!drowning) {
		const int warning_count = std::min(3, oxygen_frames / (fps * 5));
		while (oxygen_warning_count < warning_count) {
			++oxygen_warning_count;
			PlayOxygenWarning();
		}
		if (oxygen_frames >= fps * 18) {
			drowning = true;
			StartDrowningMusic();
		}
	} else if (!drowned && oxygen_frames >= fps * 30) {
		KillPlayer();
	}

	if (frame_delta <= 0) return;

	for (auto it = bubbles.begin(); it != bubbles.end();) {
		const int old_age = it->age;
		it->age += frame_delta;
		it->life -= frame_delta;
		it->y -= it->age / 3 - old_age / 3;
		it->x += it->drift * (it->age / 6 - old_age / 6);
		if (it->life <= 0) {
			it = bubbles.erase(it);
		} else {
			++it;
		}
	}

	spawn_counter += frame_delta;
	if (spawn_counter >= 30) {
		spawn_counter -= 30;
		if (bubbles.size() < 4) {
			Bubble bubble;
			bubble.x = Main_Data::game_player->GetScreenX() + Rand::GetRandomNumber(-5, 5);
			bubble.y = Main_Data::game_player->GetScreenY() - Rand::GetRandomNumber(2, 12);
			bubble.size = Rand::GetRandomNumber(2, 3);
			bubble.max_life = Rand::GetRandomNumber(90, 140);
			bubble.life = bubble.max_life;
			bubble.age = 0;
			bubble.drift = Rand::GetRandomNumber(-1, 1);
			bubbles.push_back(bubble);
		}
	}

}

void UnderwaterMode::DrawOverlay(Bitmap& dst) {
	if (!active) return;

	const int width = dst.GetWidth();
	const int height = dst.GetHeight();
	if (!tint_bitmap || tint_width != width || tint_height != height) {
		tint_bitmap = Bitmap::Create(width, height, Color(20, 105, 220, 255));
		distortion_bitmap = Bitmap::Create(width, height, true);
		tint_width = width;
		tint_height = height;
	}

	// Copy and wave the map frame before tinting it. This drawable is below
	// windows, so dialogue and HUD elements remain undistorted.
	distortion_bitmap->Blit(0, 0, dst, dst.GetRect(), Opacity::Opaque());
	const double phase = static_cast<double>(Game_Clock::GetFrame()) * 0.045;
	dst.WaverBlit(0, 0, 1.0, 1.0, *distortion_bitmap,
		distortion_bitmap->GetRect(), 1, phase, Opacity::Opaque());

	// This drawable is below the window layer, so only the map presentation is tinted.
	dst.Blit(0, 0, *tint_bitmap, tint_bitmap->GetRect(), Opacity(85), Bitmap::BlendMode::Normal);

	for (const auto& bubble : bubbles) {
		const Color color(205, 240, 255, 210);
		const int radius = bubble.size;
		const int outer = radius * radius;
		const int inner = std::max(0, radius - 1) * std::max(0, radius - 1);
		// Plot a circular ring using the pixels whose squared distance falls
		// between the inner and outer radii.
		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dx = -radius; dx <= radius; ++dx) {
				const int distance = dx * dx + dy * dy;
				if (distance <= outer && distance >= inner) {
					dst.FillRect(Rect(bubble.x + dx, bubble.y + dy, 1, 1), color);
				}
			}
		}
	}

	for (const auto& pocket : air_pockets) {
		const int screen_x = pocket.x * TILE_SIZE - Game_Map::GetDisplayX() / TILE_SIZE + TILE_SIZE / 2;
		const int screen_y = pocket.y * TILE_SIZE - Game_Map::GetDisplayY() / TILE_SIZE + TILE_SIZE - 4;
		for (int i = 0; i < 3; ++i) {
			const int cx = screen_x + (i - 1) * 6;
			const int cy = screen_y - (i % 2) * 4;
			const int radius = 2;
			const Color color(220, 250, 255, 220);
			for (int dy = -radius; dy <= radius; ++dy) {
				for (int dx = -radius; dx <= radius; ++dx) {
					const int distance = dx * dx + dy * dy;
					if (distance <= radius * radius && distance >= 1) {
						dst.FillRect(Rect(cx + dx, cy + dy, 1, 1), color);
					}
				}
			}
		}
	}

	if (drowning && !drowned && Main_Data::game_player) {
		const int fps = std::max(1, Game_Clock::GetTargetGameFps());
		const int drowning_frame = oxygen_frames - fps * 18;
		const int countdown = std::clamp(5 - drowning_frame / std::max(1, (fps * 12) / 5), 1, 5);
		dst.TextDraw(Main_Data::game_player->GetScreenX() - 4,
			Main_Data::game_player->GetScreenY() - 42,
			Font::ColorCritical, std::to_string(countdown));
	}
}

void UnderwaterMode::ResetOxygen() {
	oxygen_frames = 0;
	oxygen_warning_count = 0;
	oxygen_start_time = Game_Clock::now();
	oxygen_pause_start_time = {};
	oxygen_paused = false;
	drowning = false;
	drowned = false;
	air_refilling = false;
	air_refill_duration_ms = 0;
	air_refill_start_time = {};
	if (drowning_music_playing) {
		Audio().BGS_Stop();
		if (Main_Data::game_system) {
			const auto& map_music = Main_Data::game_system->GetCurrentBGM();
			if (!map_music.name.empty() && map_music.name != "(OFF)") {
				Main_Data::game_system->BgmStop();
				Main_Data::game_system->BgmPlay(map_music);
			}
		}
	}
	drowning_music_playing = false;
}

void UnderwaterMode::PlayOxygenWarning() {
	auto stream = FileFinder::OpenSound("Oxygen");
	if (!stream) {
		Output::Debug("Underwater: Oxygen.wav was not found");
		return;
	}
	auto se_cache = AudioSeCache::Create(std::move(stream), "Oxygen");
	if (se_cache) {
		Audio().SE_Play(std::move(se_cache), 100, 100, 50);
	}
}

void UnderwaterMode::StartDrowningMusic() {
	if (drowning_music_playing) return;
	auto stream = FileFinder::OpenMusic("Drowning");
	if (!stream) {
		Output::Debug("Underwater: Drowning.mid was not found");
		return;
	}
	Audio().BGM_Stop();
	Audio().BGS_Play(std::move(stream), 100, 100, 50);
	drowning_music_playing = true;
	Output::Debug("Underwater: Started drowning music");
}

void UnderwaterMode::KillPlayer() {
	drowned = true;
	if (drowning_music_playing) {
		Audio().BGS_Stop();
		drowning_music_playing = false;
	}
	if (!Main_Data::game_party) return;

	const auto actors = Main_Data::game_party->GetActors();
	if (!actors.empty() && actors[0]) {
		actors[0]->ChangeHp(-actors[0]->GetHp(), true);
	}

	if (Scene::instance) {
		Scene::instance->SetRequestedScene(std::make_shared<Scene_Gameover>());
	}
}

void UnderwaterMode::GenerateAirPockets() {
	air_pockets.clear();
	const int width = Game_Map::GetTilesX();
	const int height = Game_Map::GetTilesY();
	if (width <= 0 || height <= 0) return;

	const int passable_mask = Passable::Down | Passable::Right | Passable::Left | Passable::Up;
	for (int attempt = 0; attempt < width * height * 2 && air_pockets.size() < 5; ++attempt) {
		const int x = Rand::GetRandomNumber(0, width - 1);
		const int y = Rand::GetRandomNumber(0, height - 1);
		if (!Game_Map::IsPassableTile(nullptr, passable_mask, x, y, false, true)) continue;
		bool duplicate = false;
		for (const auto& pocket : air_pockets) {
			if (pocket.x == x && pocket.y == y) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate) air_pockets.push_back({x, y});
	}
}

void UnderwaterMode::BroadcastAirPockets() {
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;
	PacketWriter packet(PacketType::UnderwaterPocketSync);
	packet.write(static_cast<int32_t>(Game_Map::GetMapId()));
	packet.write(static_cast<uint8_t>(air_pockets.size()));
	for (const auto& pocket : air_pockets) {
		packet.write(static_cast<int32_t>(pocket.x));
		packet.write(static_cast<int32_t>(pocket.y));
	}
	net.Broadcast(packet, true);
}

void UnderwaterMode::HandlePacket(uint16_t /* sender_id */, const uint8_t* data, size_t len) {
	if (!active || NetManager::Instance().IsHost()) return;
	PacketReader reader(data, len);
	reader.readType();
	const int map_id = reader.readI32();
	const uint8_t count = reader.readU8();
	if (!reader.ok() || count > 5) return;

	std::vector<AirPocket> pockets;
	pockets.reserve(count);
	for (uint8_t i = 0; i < count; ++i) {
		pockets.push_back({reader.readI32(), reader.readI32()});
	}
	if (!reader.ok()) return;

	if (map_id == Game_Map::GetMapId()) {
		air_pockets = std::move(pockets);
		air_map_id = map_id;
	} else {
		pending_air_map_id = map_id;
		pending_air_pockets = std::move(pockets);
	}
}

} // namespace Chaos
