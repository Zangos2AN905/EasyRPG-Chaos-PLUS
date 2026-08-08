/*
 * Chaos Fork: Rawberry - Horror Mode Enemy
 * A chase enemy that hunts the player and gets repelled by the flashlight.
 */

#ifndef EP_CHAOS_GAME_RAWBERRY_H
#define EP_CHAOS_GAME_RAWBERRY_H

#include "game_character.h"
#include <lcf/rpg/savemapevent.h>

namespace Chaos {

/**
 * Rawberry is a horror mode enemy that chases the player.
 * Uses Chara1 charset, index 7.
 * - Hunts the player and "eats" them on contact (game over).
 * - Gets teleported far away when touching the flashlight radius.
 * - Plays Rawberry.mp3 with volume based on distance (3D audio).
 */
class Game_Rawberry : public Game_CharacterDataStorage<lcf::rpg::SaveMapEvent> {
public:
	Game_Rawberry();

	/** Update AI, movement, collision, and audio each frame */
	void UpdateChase();

	/** Required override - drives movement each frame */
	void UpdateNextMovementAction() override;

	/** Override MakeWay to allow movement through walls when needed */
	bool MakeWay(int from_x, int from_y, int to_x, int to_y) override;

	/** Place Rawberry at a random position far from the player */
	void SpawnFarFromPlayer();

	/** Whether Rawberry has caught the player */
	bool HasCaughtPlayer() const { return caught_player; }

	/** Reset caught state (after handling game over) */
	void ResetCaught() { caught_player = false; }

	/** Whether Rawberry is currently active on the map */
	bool IsSpawned() const { return spawned; }

	/** Stop the BGS loop */
	void StopBGS();

	/** Client-side display update: animation, audio, and collision only (no AI) */
	void UpdateDisplay();

	/** Set position from network sync */
	void SetSyncPosition(int x, int y, int direction);

private:
	/** Move toward the player's position */
	void ChasePlayer();

	/** Check if Rawberry is inside the flashlight radius and teleport away */
	void CheckFlashlightCollision();

	/** Check if Rawberry touched the player */
	void CheckPlayerCollision();

	/** Play positional audio based on distance to player */
	void UpdatePositionalAudio();

	/** Start the BGS loop for Rawberry's music */
	void StartBGS();

	/** Teleport to a random distant position */
	void TeleportAway();

	bool spawned = false;
	bool caught_player = false;
	bool bgs_playing = false;

	// Chase AI
	int chase_cooldown = 0;
	static constexpr int CHASE_INTERVAL = 8;   // frames between moves (slower than player)
	static constexpr int SPAWN_MIN_DIST = 12;  // minimum spawn distance from player in tiles
	static constexpr int CATCH_DIST = 0;       // tiles - same tile = caught
	static constexpr int FLASHLIGHT_REPEL_DIST = 4; // tiles - flashlight repels within this range

	// Positional audio
	static constexpr int AUDIO_MAX_DIST = 20;       // beyond this distance, volume = 0
	static constexpr int AUDIO_FULL_DIST = 2;       // within this distance, volume = 100
};

} // namespace Chaos

#endif
