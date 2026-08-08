/*
 * Chaos Fork: Rubber Band Drawable
 * Draws a tether line between two characters on the map (Knuckles Chaotix style).
 */

#ifndef EP_CHAOS_DRAWABLE_RUBBER_BAND_H
#define EP_CHAOS_DRAWABLE_RUBBER_BAND_H

#include "drawable.h"
#include "color.h"

class Game_Character;

namespace Chaos {

class Drawable_RubberBand : public Drawable {
public:
	Drawable_RubberBand();

	void Draw(Bitmap& dst) override;

	void SetEndpoints(Game_Character* a, Game_Character* b);
	void SetStretchRatio(float ratio);

private:
	void DrawLine(Bitmap& dst, int x0, int y0, int x1, int y1, const Color& color);

	Game_Character* char_a = nullptr;
	Game_Character* char_b = nullptr;
	float stretch_ratio = 0.0f; // 0.0 = relaxed, 1.0 = max stretch
};

} // namespace Chaos

#endif
