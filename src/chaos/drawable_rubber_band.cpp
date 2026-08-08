/*
 * Chaos Fork: Rubber Band Drawable Implementation
 */

#include "chaos/drawable_rubber_band.h"
#include "bitmap.h"
#include "drawable_mgr.h"
#include "game_character.h"
#include "rect.h"
#include <cmath>
#include <algorithm>

namespace Chaos {

Drawable_RubberBand::Drawable_RubberBand()
	: Drawable(Priority_Player + 1, Flags::Default)
{
	DrawableMgr::Register(this);
}

void Drawable_RubberBand::SetEndpoints(Game_Character* a, Game_Character* b) {
	char_a = a;
	char_b = b;
}

void Drawable_RubberBand::SetStretchRatio(float ratio) {
	stretch_ratio = std::clamp(ratio, 0.0f, 1.0f);
}

void Drawable_RubberBand::Draw(Bitmap& dst) {
	if (!char_a || !char_b) return;
	if (!IsVisible()) return;

	// Get screen positions (center of each character sprite)
	int ax = char_a->GetScreenX();
	int ay = char_a->GetScreenY() - 8; // offset up from feet to body center
	int bx = char_b->GetScreenX();
	int by = char_b->GetScreenY() - 8;

	// Color interpolation: yellow (relaxed) -> red (stretched)
	uint8_t r = static_cast<uint8_t>(255);
	uint8_t g = static_cast<uint8_t>(255 * (1.0f - stretch_ratio));
	uint8_t b = 0;
	uint8_t a = static_cast<uint8_t>(200 + 55 * stretch_ratio);
	Color line_color(r, g, b, a);

	// Draw the main band line
	DrawLine(dst, ax, ay, bx, by, line_color);

	// Draw a slightly thicker line when stretched (2px wide)
	if (stretch_ratio > 0.3f) {
		Color inner(r, g, b, static_cast<uint8_t>(a / 2));
		DrawLine(dst, ax + 1, ay, bx + 1, by, inner);
		DrawLine(dst, ax, ay + 1, bx, by + 1, inner);
	}
}

void Drawable_RubberBand::DrawLine(Bitmap& dst, int x0, int y0, int x1, int y1, const Color& color) {
	// Bresenham's line algorithm using 1x1 FillRects
	int dx = std::abs(x1 - x0);
	int dy = std::abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx - dy;

	while (true) {
		dst.FillRect(Rect(x0, y0, 1, 1), color);

		if (x0 == x1 && y0 == y1) break;

		int e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y0 += sy;
		}
	}
}

} // namespace Chaos
