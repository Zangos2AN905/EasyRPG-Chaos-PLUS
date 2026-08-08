/*
 * Chaos Fork: Undertale Battle UI
 * Drawable widgets for the Undertale-style battle interface:
 * - UndertaleButton: Fight/Act/Item/Mercy button sprites (selected/unselected)
 * - UndertaleBattleBox: The dialogue/bullet box (white-bordered black rectangle)
 */

#ifndef EP_CHAOS_UNDERTALE_BATTLE_UI_H
#define EP_CHAOS_UNDERTALE_BATTLE_UI_H

#include "sprite.h"
#include "bitmap.h"
#include "memory_management.h"

#include <string>

namespace Chaos {

enum class UndertaleButtonType {
	Fight = 0,
	Act,
	Item,
	Mercy,
	Count
};

/**
 * A single Undertale-style menu button (Fight/Act/Item/Mercy).
 * Each button sprite sheet is 220x42 with two columns:
 *   Left half (0..109) = selected state
 *   Right half (110..219) = normal (unselected) state
 * The sprite is tinted orange when unselected, yellow when selected.
 */
class UndertaleButton : public Sprite {
public:
	explicit UndertaleButton(UndertaleButtonType type);

	void SetSelected(bool selected);
	bool IsSelected() const { return is_selected; }

	UndertaleButtonType GetType() const { return button_type; }

	static constexpr int SHEET_WIDTH = 220;
	static constexpr int SHEET_HEIGHT = 42;
	static constexpr int HALF_WIDTH = 110;

private:
	void LoadSprite();
	void UpdateAppearance();

	UndertaleButtonType button_type;
	bool is_selected = false;
	BitmapRef selected_bitmap;
	BitmapRef normal_bitmap;
};

/**
 * The Undertale-style dialogue/bullet box.
 * A white-bordered black rectangle drawn in the lower portion of the screen.
 * In dialogue mode it shows text; in bullet mode it's the dodging arena.
 */
class UndertaleBattleBox : public Drawable {
public:
	UndertaleBattleBox();

	void Draw(Bitmap& dst) override;

	void SetBounds(int x, int y, int width, int height);

	int GetBoxX() const { return box_x; }
	int GetBoxY() const { return box_y; }
	int GetBoxWidth() const { return box_width; }
	int GetBoxHeight() const { return box_height; }

	/** Inner area (inside the border) */
	int GetInnerX() const { return box_x + border_thickness; }
	int GetInnerY() const { return box_y + border_thickness; }
	int GetInnerWidth() const { return box_width - border_thickness * 2; }
	int GetInnerHeight() const { return box_height - border_thickness * 2; }

	void SetBorderThickness(int thickness) { border_thickness = thickness; }

private:
	int box_x = 32;
	int box_y = 140;
	int box_width = 256;
	int box_height = 60;
	int border_thickness = 3;
};

} // namespace Chaos

#endif
