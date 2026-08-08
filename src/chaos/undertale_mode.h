/*
 * Chaos Fork: Undertale Mode
 * Manages pixel-based movement and Undertale-specific mechanics.
 * Works in both singleplayer and multiplayer Undertale mode.
 */

#ifndef EP_CHAOS_UNDERTALE_MODE_H
#define EP_CHAOS_UNDERTALE_MODE_H

#include "game_map.h"

class Game_Player;

namespace Chaos {

class UndertaleMode {
public:
	static UndertaleMode& Instance();

	/** Whether Undertale mode movement is currently active */
	bool IsActive() const;

	/** Initialize pixel position from the player's current tile position */
	void InitFromPlayer(Game_Player& player);

	/** Reset state (called on mode change or map change) */
	void Reset();

	/**
	 * Process pixel movement for the player this frame.
	 * Called from Game_Player::UpdateNextMovementAction when in Undertale mode.
	 * Returns true if the player moved (for step counting/event triggers).
	 */
	bool UpdatePixelMovement(Game_Player& player);

	/** Get the absolute pixel position in SCREEN_TILE_SIZE units */
	int GetAbsoluteX() const { return abs_x; }
	int GetAbsoluteY() const { return abs_y; }

	/** Check if the dance glitch animation should play */
	bool IsDancing() const { return dance_active; }

	/** Whether movement was blocked at a tile boundary this frame */
	bool WasBlocked() const { return last_was_blocked; }

	/** Whether the player entered a new tile this frame */
	bool EnteredNewTile() const { return last_entered_new_tile; }

	/** Movement speed in screen pixels per frame (default 2, like Undertale) */
	void SetMoveSpeed(int pixels_per_frame);
	int GetMoveSpeed() const { return move_speed_pixels; }

	/**
	 * Check if the player's pixel hitbox overlaps any event at the given tile.
	 * Uses the player's sub-tile position for precise collision.
	 * @param trigger_set Trigger types to match
	 * @return true if an event was triggered
	 */
	bool CheckEventHitbox(Game_Player& player) const;

private:
	UndertaleMode() = default;

	/** Check if the player can move to the given tile */
	bool CanMoveTo(Game_Player& player, int from_tile_x, int from_tile_y, int to_tile_x, int to_tile_y) const;

	/** Update the dance glitch state (called after movement, uses blocked flag) */
	void UpdateDanceGlitch(Game_Player& player, bool opposing_held);

	/** Sync tile position from absolute pixel position */
	void SyncTilePosition(Game_Player& player);

	/** Update map scroll to center camera on player pixel position */
	void UpdateScroll(Game_Player& player);

	// Absolute position in SCREEN_TILE_SIZE units (256 units = 1 tile)
	int abs_x = 0;
	int abs_y = 0;
	bool initialized = false;

	// Movement speed: pixels per frame (1 pixel = SCREEN_TILE_SIZE / TILE_SIZE = 16 units)
	int move_speed_pixels = 2;
	static constexpr int UNITS_PER_PIXEL = SCREEN_TILE_SIZE / 16; // 16 units per screen pixel

	// Dance glitch state: Frisk dance triggers when opposing keys held + blocked by wall
	bool dance_active = false;
	bool opposing_keys_held = false;  // True when opposing direction pair is held

	// Step counter for event triggering (accumulates sub-tile movement)
	int step_accumulator_x = 0;
	int step_accumulator_y = 0;

	// Per-frame result tracking for event triggers
	bool last_was_blocked = false;
	bool last_entered_new_tile = false;
};

} // namespace Chaos

#endif
