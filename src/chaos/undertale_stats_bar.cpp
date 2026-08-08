/*
 * Chaos Fork: Undertale Stats Bar implementation
 */

#include "chaos/undertale_stats_bar.h"
#include "chaos/undertale_font.h"
#include "font.h"
#include "text.h"
#include "color.h"
#include "drawable_mgr.h"
#include "output.h"

namespace Chaos {

namespace {
	constexpr Color kWhite(255, 255, 255, 255);
	constexpr Color kYellow(255, 255, 0, 255);
	constexpr Color kHPBarGreen(0, 192, 0, 255);
	constexpr Color kHPBarRed(192, 0, 0, 255);
	constexpr Color kHPBarBackground(64, 0, 0, 255);
	constexpr int kBarPixelHeight = 10;
	constexpr int kBarPixelWidth = 40;
	constexpr int kStatsHeight = 14;
}

UndertaleStatsBar::UndertaleStatsBar()
	: Drawable(Priority_Window + 1, Drawable::Flags::Default)
{
	DrawableMgr::Register(this);
}

void UndertaleStatsBar::Draw(Bitmap& dst) {
	auto font = UndertaleFont::Stats();
	if (!font) {
		font = Font::Default();
	}

	// Style: no shadow, no gradient, white text
	Font::Style style;
	style.size = font->GetCurrentStyle().size;
	style.draw_shadow = false;
	style.draw_gradient = false;
	auto guard = font->ApplyStyle(style);

	// Layout: "LV X" then optional name then "HP [bar] cur / max"
	// Centered within bar_width
	int section_spacing = 16;

	// Measure all text widths first
	std::string lv_text = "LV " + std::to_string(level);
	Rect lv_size = Text::GetSize(*font, lv_text);

	std::string hp_label = "HP";
	Rect hp_label_size = Text::GetSize(*font, hp_label);

	std::string hp_numbers = std::to_string(hp) + " / " + std::to_string(max_hp);
	Rect hp_num_size = Text::GetSize(*font, hp_numbers);

	int total_width = lv_size.width + section_spacing + hp_label_size.width + 4 + kBarPixelWidth + 4 + hp_num_size.width;
	int start_x = bar_x + (bar_width - total_width) / 2;
	int text_y = bar_y;

	// Draw "LV X"
	int dx = start_x;
	Text::Draw(dst, dx, text_y, *font, kWhite, lv_text);
	dx += lv_size.width + section_spacing;

	// Draw "HP"
	Text::Draw(dst, dx, text_y, *font, kWhite, hp_label);
	dx += hp_label_size.width + 4;

	// Draw HP bar background
	int bar_y_offset = text_y + (kStatsHeight - kBarPixelHeight) / 2;
	dst.FillRect(Rect(dx, bar_y_offset, kBarPixelWidth, kBarPixelHeight), kHPBarBackground);

	// Draw HP bar fill
	int fill_width = 0;
	if (max_hp > 0) {
		fill_width = (hp * kBarPixelWidth) / max_hp;
	}
	Color bar_color = (hp > max_hp / 4) ? kHPBarGreen : kHPBarRed;
	if (fill_width > 0) {
		dst.FillRect(Rect(dx, bar_y_offset, fill_width, kBarPixelHeight), kYellow);
	}
	dx += kBarPixelWidth + 4;

	// Draw "cur / max"
	Text::Draw(dst, dx, text_y, *font, kWhite, hp_numbers);
}

void UndertaleStatsBar::SetPosition(int x, int y, int width) {
	bar_x = x;
	bar_y = y;
	bar_width = width;
}

void UndertaleStatsBar::SetStats(int lv, int new_hp, int new_max_hp, const std::string& name) {
	level = lv;
	hp = new_hp;
	max_hp = new_max_hp;
	player_name = name;
}

} // namespace Chaos
