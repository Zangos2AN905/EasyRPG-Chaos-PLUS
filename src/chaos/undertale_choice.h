/*
 * Chaos Fork: Undertale Choice Menu
 * Renders a list of selectable choices inside the battle box
 * with a soul cursor, used for ACT and MERCY submenus.
 */

#ifndef EP_CHAOS_UNDERTALE_CHOICE_H
#define EP_CHAOS_UNDERTALE_CHOICE_H

#include "drawable.h"
#include "bitmap.h"
#include "memory_management.h"

#include <string>
#include <vector>
#include <functional>

namespace Chaos {

struct UndertaleChoiceItem {
	std::string label;
	bool enabled = true;
};

/**
 * Undertale-style choice list rendered inside the battle box.
 * Displays items in a 2-column grid with a soul heart cursor.
 * Used for ACT actions, MERCY options, enemy targeting, and item lists.
 */
class UndertaleChoiceMenu : public Drawable {
public:
	UndertaleChoiceMenu();

	void Draw(Bitmap& dst) override;

	/** Set the drawing area (inner bounds of the battle box) */
	void SetBounds(int x, int y, int width, int height);

	/** Open the menu with a set of choices */
	void Open(std::vector<UndertaleChoiceItem> items);

	/** Close and hide the menu */
	void Close();

	/** Update input (call once per frame when active) */
	void Update();

	/** Currently selected index */
	int GetSelectedIndex() const { return selected; }

	/** Get the selected item label */
	std::string GetSelectedLabel() const;

	/** Whether the menu is currently showing */
	bool IsOpen() const { return open; }

	/** Whether the player just confirmed a selection this frame */
	bool IsConfirmed() const { return confirmed; }

	/** Whether the player just pressed cancel this frame */
	bool IsCancelled() const { return cancelled; }

	/** Get the soul cursor position for external soul sprite positioning */
	int GetSoulX() const { return soul_x; }
	int GetSoulY() const { return soul_y; }

private:
	void UpdateCursorPosition();
	int GetPageCount() const;
	int GetPageForIndex(int index) const;

	int bounds_x = 0;
	int bounds_y = 0;
	int bounds_w = 256;
	int bounds_h = 60;

	std::vector<UndertaleChoiceItem> items;
	int selected = 0;
	int page = 0;
	bool open = false;
	bool confirmed = false;
	bool cancelled = false;

	int soul_x = 0;
	int soul_y = 0;

	// Layout: 2 columns, 2 rows per page = 4 items per page
	static constexpr int kColumns = 2;
	static constexpr int kRows = 2;
	static constexpr int kItemsPerPage = kColumns * kRows;
	static constexpr int kPadding = 10;
	static constexpr int kLineHeight = 16;
	static constexpr int kSoulOffsetX = -2; // soul sits just left of text
};

} // namespace Chaos

#endif
