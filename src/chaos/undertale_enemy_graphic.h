/*
 * Chaos Fork: Undertale Enemy Graphic
 * Renders enemy battler sprites in 1-bit Undertale style
 * (pure white silhouette on black background).
 */

#ifndef EP_CHAOS_UNDERTALE_ENEMY_GRAPHIC_H
#define EP_CHAOS_UNDERTALE_ENEMY_GRAPHIC_H

#include "sprite.h"
#include "bitmap.h"
#include "memory_management.h"

#include <string>
#include <vector>
#include <memory>

class Game_Enemy;

namespace Chaos {

/**
 * Renders a single enemy as a 1-bit white silhouette, Undertale style.
 * The source RPG Maker monster graphic is converted to pure white pixels
 * (any non-transparent pixel becomes white), giving the classic Undertale look.
 */
class UndertaleEnemyGraphic : public Sprite {
public:
	explicit UndertaleEnemyGraphic(Game_Enemy* enemy);

	/** Reload graphic after enemy change */
	void Refresh();

	/** Set display position */
	void SetDisplayPosition(int x, int y);

	Game_Enemy* GetEnemy() const { return enemy; }

	/** Get the 1-bit converted bitmap width/height */
	int GetGraphicWidth() const;
	int GetGraphicHeight() const;

private:
	void LoadAndConvert();

	Game_Enemy* enemy = nullptr;
	std::string loaded_sprite_name;
};

/**
 * Manages rendering of all enemies in the Undertale battle.
 * Positions them centered above the battle box.
 */
class UndertaleEnemyGroup {
public:
	/** Initialize from the current enemy party */
	void Create();

	/** Update enemy sprites (handle death animations, visibility) */
	void Update();

	/** Show/hide all enemy sprites */
	void SetVisible(bool visible);

	/** Get number of alive enemies */
	int GetAliveCount() const;

	/** Get list of alive enemy names for ACT targeting */
	std::vector<std::string> GetAliveEnemyNames() const;

	/** Get ordered list of alive enemy indices */
	std::vector<int> GetAliveEnemyIndices() const;

	/** Get a specific enemy graphic */
	UndertaleEnemyGraphic* GetGraphic(int index);

	/** Position enemies centered above the battle box */
	void LayoutEnemies(int area_x, int area_y, int area_w, int area_h);

private:
	std::vector<std::unique_ptr<UndertaleEnemyGraphic>> graphics;
};

} // namespace Chaos

#endif
