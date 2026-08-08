/*
 * Chaos Fork: Undertale Fight Bar implementation
 */

#include "chaos/undertale_fight_bar.h"
#include "filefinder.h"
#include "output.h"
#include "bitmap.h"

#include <cmath>
#include <algorithm>

namespace Chaos {

namespace {
	float Lerp(float from, float to, float t) {
		return from + (to - from) * t;
	}
}

UndertaleFightBar::UndertaleFightBar()
	: bar_sprite(Drawable::Flags::Default),
	  cursor_sprite(Drawable::Flags::Default)
{
	bar_sprite.SetZ(Priority_Window + 2);
	cursor_sprite.SetZ(Priority_Window + 3);

	LoadSprites();
	SetVisible(false);
}

void UndertaleFightBar::LoadSprites() {
	auto chaos_fs = FileFinder::ChaosAssets();
	if (!chaos_fs) {
		Output::Debug("UndertaleFightBar: ChaosAssets not available");
		return;
	}

	// Load bar background
	auto bar_stream = chaos_fs.OpenFile("UndertaleMode.content/Sprite/GUI", "fightbar", FileFinder::IMG_TYPES);
	if (bar_stream) {
		auto bitmap = Bitmap::Create(std::move(bar_stream), false);
		if (bitmap) {
			bar_sprite.SetBitmap(bitmap);
			bar_sprite.SetSrcRect(Rect(0, 0, BAR_SRC_WIDTH, BAR_SRC_HEIGHT));
			// Scale via zoom
			bar_sprite.SetZoomX(static_cast<double>(BAR_DISPLAY_WIDTH) / BAR_SRC_WIDTH);
			bar_sprite.SetZoomY(static_cast<double>(BAR_DISPLAY_HEIGHT) / BAR_SRC_HEIGHT);
		}
	}

	// Load timing cursor
	auto cursor_stream = chaos_fs.OpenFile("UndertaleMode.content/Sprite/GUI", "timing", FileFinder::IMG_TYPES);
	if (cursor_stream) {
		auto bitmap = Bitmap::Create(std::move(cursor_stream), false);
		if (bitmap) {
			cursor_sprite.SetBitmap(bitmap);
			// Scale to match bar height
			cursor_sprite.SetZoomX(static_cast<double>(CURSOR_DISPLAY_WIDTH) / CURSOR_HALF_WIDTH);
			cursor_sprite.SetZoomY(static_cast<double>(CURSOR_DISPLAY_HEIGHT) / CURSOR_SHEET_HEIGHT);
		}
	}

	UpdateCursorSrcRect();
}

void UndertaleFightBar::UpdateVisuals(float width_scale, int opacity) {
	const int current_width = std::max(1, static_cast<int>(std::round(BAR_DISPLAY_WIDTH * width_scale)));
	const int centered_x = bar_x + (BAR_DISPLAY_WIDTH - current_width) / 2;

	bar_sprite.SetX(centered_x);
	bar_sprite.SetY(bar_y);
	bar_sprite.SetZoomX((static_cast<double>(BAR_DISPLAY_WIDTH) / BAR_SRC_WIDTH) * width_scale);
	bar_sprite.SetZoomY(static_cast<double>(BAR_DISPLAY_HEIGHT) / BAR_SRC_HEIGHT);
	bar_sprite.SetOpacity(opacity);

	cursor_sprite.SetZoomX(static_cast<double>(CURSOR_DISPLAY_WIDTH) / CURSOR_HALF_WIDTH);
	cursor_sprite.SetZoomY(static_cast<double>(CURSOR_DISPLAY_HEIGHT) / CURSOR_SHEET_HEIGHT);
	cursor_sprite.SetY(bar_y);
	cursor_sprite.SetOpacity(opacity);
	cursor_sprite.SetX(centered_x + static_cast<int>(cursor_pos * current_width) - CURSOR_DISPLAY_WIDTH / 2);
}

void UndertaleFightBar::UpdateStoppedCursorAnimation() {
	const int frame = (stopped_delay_frame / 7) % 2;
	const int src_x = frame == 0 ? 0 : CURSOR_HALF_WIDTH;

	cursor_sprite.SetZoomX(static_cast<double>(CURSOR_DISPLAY_WIDTH) / CURSOR_HALF_WIDTH);
	cursor_sprite.SetZoomY(static_cast<double>(CURSOR_DISPLAY_HEIGHT) / CURSOR_SHEET_HEIGHT);
	cursor_sprite.SetOpacity(255);
	cursor_sprite.SetY(bar_y);
	cursor_sprite.SetSrcRect(Rect(src_x, 0, CURSOR_HALF_WIDTH, CURSOR_SHEET_HEIGHT));
}

void UndertaleFightBar::UpdateCursorSrcRect() {
	if (stopped) {
		// Right half = stopped/animating
		cursor_sprite.SetSrcRect(Rect(CURSOR_HALF_WIDTH, 0, CURSOR_HALF_WIDTH, CURSOR_SHEET_HEIGHT));
	} else {
		// Left half = moving
		cursor_sprite.SetSrcRect(Rect(0, 0, CURSOR_HALF_WIDTH, CURSOR_SHEET_HEIGHT));
	}
}

void UndertaleFightBar::Start() {
	active = false;
	stopped = false;
	phase = AnimationPhase::Opening;
	cursor_pos = 0.0f;
	multiplier = 0.0f;
	animation_frame = 0;
	stopped_delay_frame = 0;
	SetVisible(true);
	cursor_sprite.SetVisible(false);
	UpdateCursorSrcRect();
	UpdateVisuals(MIN_WIDTH_SCALE, 255);
}

void UndertaleFightBar::Update() {
	switch (phase) {
		case AnimationPhase::Hidden:
			return;
		case AnimationPhase::Opening: {
			++animation_frame;
			const float progress = std::min(1.0f, static_cast<float>(animation_frame) / OPEN_ANIMATION_FRAMES);
			UpdateVisuals(Lerp(MIN_WIDTH_SCALE, 1.0f, progress), 255);
			if (progress >= 1.0f) {
				phase = AnimationPhase::Active;
				active = true;
				cursor_sprite.SetVisible(true);
			}
			return;
		}
		case AnimationPhase::Active:
			cursor_pos += cursor_speed;
			if (cursor_pos >= 1.0f) {
				cursor_pos = 1.0f;
				Stop();
				return;
			}
			UpdateCursorSrcRect();
			UpdateVisuals(1.0f, 255);
			return;
		case AnimationPhase::HoldStopped:
			++stopped_delay_frame;
			UpdateVisuals(1.0f, 255);
			UpdateStoppedCursorAnimation();
			if (stopped_delay_frame >= STOP_HOLD_FRAMES) {
				phase = AnimationPhase::Closing;
				animation_frame = 0;
			}
			return;
		case AnimationPhase::Closing: {
			++animation_frame;
			const float progress = std::min(1.0f, static_cast<float>(animation_frame) / CLOSE_ANIMATION_FRAMES);
			const float scale = Lerp(1.0f, MIN_WIDTH_SCALE, progress);
			const int opacity = std::max(0, static_cast<int>(std::round(Lerp(255.0f, 0.0f, progress))));
			UpdateVisuals(scale, opacity);
			if (progress >= 1.0f) {
				phase = AnimationPhase::Hidden;
				SetVisible(false);
			}
			return;
		}
	}
}

float UndertaleFightBar::Stop() {
	if (phase != AnimationPhase::Active) return 0.0f;

	stopped = true;
	active = false;
	phase = AnimationPhase::HoldStopped;
	stopped_delay_frame = 0;

	// Multiplier based on distance from center (0.5)
	float dist = std::abs(cursor_pos - 0.5f);
	multiplier = std::max(0.0f, 1.0f - dist * 2.0f);

	UpdateCursorSrcRect();
	return multiplier;
}

void UndertaleFightBar::SetVisible(bool visible) {
	bar_sprite.SetVisible(visible);
	cursor_sprite.SetVisible(visible);
	if (!visible) {
		active = false;
		stopped = false;
		phase = AnimationPhase::Hidden;
	}
}

void UndertaleFightBar::SetPosition(int x, int y) {
	bar_x = x;
	bar_y = y;
	bar_sprite.SetX(x);
	bar_sprite.SetY(y);
	cursor_sprite.SetY(y);
	// Cursor X is updated dynamically in Update()
	cursor_sprite.SetX(x);
}

} // namespace Chaos
