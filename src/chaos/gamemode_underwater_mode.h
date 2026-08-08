/*
 * Chaos Fork: Underwater multiplayer mode.
 * Provides the initial underwater visual layer and player bubble effect.
 */

#ifndef EP_CHAOS_GAMEMODE_UNDERWATER_MODE_H
#define EP_CHAOS_GAMEMODE_UNDERWATER_MODE_H

#include <cstdint>
#include <memory>
#include <vector>
#include "game_clock.h"

class Bitmap;
class Spriteset_Map;

namespace Chaos {

class UnderwaterOverlay;

class UnderwaterMode {
public:
	static UnderwaterMode& Instance();

	void Start();
	void Stop();
	void Update();
	void OnMapLoaded(Spriteset_Map* spriteset);
	void OnMapUnloaded();
	void HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len);

	bool IsActive() const { return active; }

	void DrawOverlay(Bitmap& dst);

private:
	UnderwaterMode() = default;

	struct Bubble {
		int x = 0;
		int y = 0;
		int size = 2;
		int life = 0;
		int max_life = 60;
		int drift = 0;
		int age = 0;
	};
	struct AirPocket {
		int x = 0;
		int y = 0;
	};

	bool active = false;
	Spriteset_Map* current_spriteset = nullptr;
	std::unique_ptr<UnderwaterOverlay> overlay;
	std::vector<Bubble> bubbles;
	std::vector<AirPocket> air_pockets;
	std::vector<AirPocket> pending_air_pockets;
	int pending_air_map_id = 0;
	int air_map_id = 0;
	int spawn_counter = 0;
	int oxygen_frames = 0;
	int oxygen_warning_count = 0;
	int last_game_frame = -1;
	Game_Clock::time_point oxygen_start_time{};
	Game_Clock::time_point oxygen_pause_start_time{};
	bool oxygen_paused = false;
	bool drowning = false;
	bool drowned = false;
	bool drowning_music_playing = false;
	bool air_refilling = false;
	int air_refill_duration_ms = 0;
	Game_Clock::time_point air_refill_start_time{};
	int tint_width = 0;
	int tint_height = 0;
	std::shared_ptr<Bitmap> tint_bitmap;
	std::shared_ptr<Bitmap> distortion_bitmap;

	void ResetOxygen();
	void PlayOxygenWarning();
	void StartDrowningMusic();
	void KillPlayer();
	void GenerateAirPockets();
	void BroadcastAirPockets();
};

} // namespace Chaos

#endif
