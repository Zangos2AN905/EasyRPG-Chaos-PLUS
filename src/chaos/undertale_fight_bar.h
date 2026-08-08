/*
 * Chaos Fork: Undertale Fight Bar
 * The timing-based attack bar from Undertale's battle system.
 * A horizontal bar with a cursor that sweeps left-to-right.
 * The player presses a button to stop it; damage scales with
 * how close to the center the cursor was when stopped.
 */

#ifndef EP_CHAOS_UNDERTALE_FIGHT_BAR_H
#define EP_CHAOS_UNDERTALE_FIGHT_BAR_H

#include "drawable.h"
#include "sprite.h"
#include "bitmap.h"
#include "memory_management.h"

namespace Chaos {

/**
 * The fight bar timing widget.
 *
 * fightbar.png: 565x128, single image (the bar background).
 * timing.png: 28x128, two columns:
 *   Left half (0..13) = moving cursor
 *   Right half (14..27) = stopped cursor (animating)
 *
 * The bar is scaled to fit the screen and the cursor sweeps across it.
 */
class UndertaleFightBar {
public:
	UndertaleFightBar();

	/** Reset the bar for a new attack. Cursor goes to left edge. */
	void Start();

	/** Update cursor position each frame. Call every frame while active. */
	void Update();

	/** Stop the cursor at its current position. Returns damage multiplier 0.0-1.0. */
	float Stop();

	/** Whether the bar is currently active (cursor moving). */
	bool IsActive() const { return active; }

	/** Whether the bar has been stopped (waiting to show result). */
	bool IsStopped() const { return stopped; }
	bool IsFinished() const { return phase == AnimationPhase::Hidden; }
	bool CanStop() const { return phase == AnimationPhase::Active; }

	/** Set visibility of both sprites. */
	void SetVisible(bool visible);

	/** Position the bar on screen. */
	void SetPosition(int x, int y);

	/** Get the damage multiplier (0.0 = edge, 1.0 = dead center). */
	float GetMultiplier() const { return multiplier; }

	/** Bar display dimensions (scaled from 565x128 to fit screen) */
	static constexpr int BAR_DISPLAY_WIDTH = 240;
	static constexpr int BAR_DISPLAY_HEIGHT = 54;

private:
	enum class AnimationPhase {
		Hidden,
		Opening,
		Active,
		HoldStopped,
		Closing,
	};

	void LoadSprites();
	void UpdateCursorSrcRect();
	void UpdateVisuals(float width_scale, int opacity);
	void UpdateStoppedCursorAnimation();

	Sprite bar_sprite;
	Sprite cursor_sprite;

	bool active = false;
	bool stopped = false;
	AnimationPhase phase = AnimationPhase::Hidden;
	float cursor_pos = 0.0f;    // 0.0 = left edge, 1.0 = right edge
	float cursor_speed = 0.012f; // fraction of bar per frame
	float multiplier = 0.0f;

	int bar_x = 0;
	int bar_y = 0;

	int animation_frame = 0;
	int stopped_delay_frame = 0;

	static constexpr int BAR_SRC_WIDTH = 565;
	static constexpr int BAR_SRC_HEIGHT = 128;
	static constexpr int CURSOR_SHEET_WIDTH = 28;
	static constexpr int CURSOR_SHEET_HEIGHT = 128;
	static constexpr int CURSOR_HALF_WIDTH = 14;
	static constexpr int CURSOR_DISPLAY_HEIGHT = BAR_DISPLAY_HEIGHT;
	static constexpr int CURSOR_DISPLAY_WIDTH = 6; // scaled proportionally
	static constexpr float MIN_WIDTH_SCALE = 0.18f;
	static constexpr int OPEN_ANIMATION_FRAMES = 18;
	static constexpr int CLOSE_ANIMATION_FRAMES = 18;
	static constexpr int STOP_HOLD_FRAMES = 144;
};

} // namespace Chaos

#endif
