/*
 * Chaos Fork: Undertale Damage Display
 * Floating damage numbers, enemy shake/vaporize effects.
 */

#ifndef EP_CHAOS_UNDERTALE_DAMAGE_H
#define EP_CHAOS_UNDERTALE_DAMAGE_H

#include "drawable.h"
#include "bitmap.h"
#include "memory_management.h"

#include <vector>
#include <string>
#include <functional>
#include <utility>

namespace Chaos {

class UndertaleEnemyGraphic;

/**
 * A single floating damage number that rises and fades out.
 */
struct DamagePopup {
	int x = 0;
	int y = 0;
	int start_y = 0;
	int damage = 0;
	int frame = 0;
	bool miss = false;
	bool active = true;

	static constexpr int kRisePixels = 16;
	static constexpr int kDuration = 50;
};

/**
 * Enemy shake effect state.
 */
struct EnemyShake {
	int enemy_index = -1;
	int original_x = 0;
	int frame = 0;
	bool active = false;

	static constexpr int kDuration = 20;
	static constexpr int kAmplitude = 3;
};

/**
 * Enemy vaporize (death dissolve) effect state.
 * Collects all non-transparent pixel positions at start, shuffles them,
 * then erases batches each frame while drifting rows upward at
 * varying speeds — matching Undertale's disintegration animation.
 */
struct EnemyVaporize {
	int enemy_index = -1;
	int frame = 0;
	bool active = false;
	int original_y = 0;
	BitmapRef snapshot;
	std::vector<std::pair<int,int>> opaque_pixels; // shuffled list of (x,y)
	int pixels_dissolved = 0;

	static constexpr int kDuration = 50;
	static constexpr int kRisePixels = 20;
};

/**
 * Manages all damage display effects:
 * - Floating damage numbers using the Damage font
 * - Enemy shake on hit
 * - Enemy vaporize on death
 */
class UndertaleDamageRenderer : public Drawable {
public:
	UndertaleDamageRenderer();

	void Draw(Bitmap& dst) override;

	/** Show a damage number popup at the given position */
	void ShowDamage(int x, int y, int damage);

	/** Show a "MISS" popup */
	void ShowMiss(int x, int y);

	/** Start shaking an enemy graphic */
	void StartShake(int enemy_index, UndertaleEnemyGraphic* graphic);

	/** Start vaporize effect on an enemy graphic */
	void StartVaporize(int enemy_index, UndertaleEnemyGraphic* graphic);

	/** Update all active effects. */
	void UpdatePopups();

	/** Update shake/vaporize effects using a callback to get graphics */
	void UpdateEffects(std::function<UndertaleEnemyGraphic*(int)> get_graphic);

	/** Whether any effects are still active */
	bool HasActiveEffects() const;

	/** Clear all effects */
	void Clear();

private:
	std::vector<DamagePopup> popups;
	std::vector<EnemyShake> shakes;
	std::vector<EnemyVaporize> vaporizes;
};

} // namespace Chaos

#endif
