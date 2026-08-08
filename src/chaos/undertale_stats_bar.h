/*
 * Chaos Fork: Undertale Stats Bar
 * Draws the LV / HP display between the battle box and buttons,
 * matching Undertale's bottom-screen layout.
 */

#ifndef EP_CHAOS_UNDERTALE_STATS_BAR_H
#define EP_CHAOS_UNDERTALE_STATS_BAR_H

#include "drawable.h"
#include "bitmap.h"
#include "memory_management.h"

#include <string>

namespace Chaos {

/**
 * Draws the Undertale-style stats bar: "LV X  [name]  HP [bar] cur / max"
 * Rendered as a Drawable so it auto-draws every frame.
 */
class UndertaleStatsBar : public Drawable {
public:
	UndertaleStatsBar();

	void Draw(Bitmap& dst) override;

	/** Set the Y coordinate and horizontal centering bounds */
	void SetPosition(int x, int y, int width);

	/** Update the displayed stats */
	void SetStats(int level, int hp, int max_hp, const std::string& name);

private:
	int bar_x = 0;
	int bar_y = 0;
	int bar_width = 320;
	int level = 1;
	int hp = 20;
	int max_hp = 20;
	std::string player_name = "CHARA";
};

} // namespace Chaos

#endif
