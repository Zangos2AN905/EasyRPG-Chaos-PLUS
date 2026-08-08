/*
 * Chaos Fork: Undertale Choice Menu implementation
 */

#include "chaos/undertale_choice.h"
#include "chaos/undertale_font.h"
#include "input.h"
#include "font.h"
#include "text.h"
#include "color.h"
#include "drawable_mgr.h"
#include "game_system.h"
#include "main_data.h"

#include <algorithm>

namespace Chaos {

namespace {
	constexpr Color kChoiceTextColor(255, 255, 255, 255);
	constexpr Color kChoiceDisabledColor(128, 128, 128, 255);
}

UndertaleChoiceMenu::UndertaleChoiceMenu()
	: Drawable(Priority_Window + 3, Drawable::Flags::Default)
{
	DrawableMgr::Register(this);
	SetVisible(false);
}

void UndertaleChoiceMenu::Draw(Bitmap& dst) {
	if (!open || items.empty()) return;

	auto font = UndertaleFont::Dialogue();
	if (!font) {
		font = Font::Default();
	}

	Font::Style style;
	style.size = font->GetCurrentStyle().size;
	style.draw_shadow = false;
	style.draw_gradient = false;
	auto guard = font->ApplyStyle(style);

	int col_width = (bounds_w - kPadding * 2) / kColumns;
	int page_start = page * kItemsPerPage;
	int page_end = std::min(page_start + kItemsPerPage, static_cast<int>(items.size()));

	for (int i = page_start; i < page_end; ++i) {
		int local = i - page_start;
		int col = local % kColumns;
		int row = local / kColumns;

		// Soul marker width offset: leave space for the heart
		int text_x = bounds_x + kPadding + col * col_width + 12;
		int text_y = bounds_y + kPadding + row * kLineHeight;

		Color color = items[i].enabled ? kChoiceTextColor : kChoiceDisabledColor;

		// Draw "* " prefix for each choice (Undertale style)
		Text::Draw(dst, text_x, text_y, *font, color, "* " + items[i].label);
	}

	// Draw page indicator if there are multiple pages
	int total_pages = GetPageCount();
	if (total_pages > 1) {
		std::string page_text = "PAGE " + std::to_string(page + 1) + "/" + std::to_string(total_pages);
		Rect page_size = Text::GetSize(*font, page_text);
		int page_x = bounds_x + bounds_w - kPadding - page_size.width;
		int page_y = bounds_y + bounds_h - kLineHeight;
		Text::Draw(dst, page_x, page_y, *font, kChoiceDisabledColor, page_text);
	}
}

void UndertaleChoiceMenu::SetBounds(int x, int y, int width, int height) {
	bounds_x = x;
	bounds_y = y;
	bounds_w = width;
	bounds_h = height;
}

void UndertaleChoiceMenu::Open(std::vector<UndertaleChoiceItem> new_items) {
	items = std::move(new_items);
	selected = 0;
	page = 0;
	open = true;
	confirmed = false;
	cancelled = false;
	SetVisible(true);
	UpdateCursorPosition();
}

void UndertaleChoiceMenu::Close() {
	open = false;
	confirmed = false;
	cancelled = false;
	items.clear();
	SetVisible(false);
}

void UndertaleChoiceMenu::Update() {
	if (!open) return;

	confirmed = false;
	cancelled = false;

	int num_items = static_cast<int>(items.size());
	if (num_items == 0) return;

	int page_start = page * kItemsPerPage;
	int page_end = std::min(page_start + kItemsPerPage, num_items);
	int page_count = page_end - page_start;

	// Local index within the current page
	int local = selected - page_start;
	int col = local % kColumns;
	int row = local / kColumns;
	int page_rows = (page_count + kColumns - 1) / kColumns;

	if (Input::IsTriggered(Input::RIGHT)) {
		int next_local = row * kColumns + ((col + 1) % kColumns);
		int next = page_start + next_local;
		if (next < page_end) {
			selected = next;
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
		}
	}
	if (Input::IsTriggered(Input::LEFT)) {
		int next_local = row * kColumns + ((col + kColumns - 1) % kColumns);
		int next = page_start + next_local;
		if (next < page_end) {
			selected = next;
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
		}
	}
	if (Input::IsTriggered(Input::DOWN)) {
		int next_row = row + 1;
		if (next_row >= page_rows) {
			// Move to next page
			int next_page = page + 1;
			if (next_page < GetPageCount()) {
				page = next_page;
				int new_start = page * kItemsPerPage;
				selected = std::min(new_start + col, num_items - 1);
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
			}
		} else {
			int next = page_start + next_row * kColumns + col;
			if (next < page_end) {
				selected = next;
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
			}
		}
	}
	if (Input::IsTriggered(Input::UP)) {
		int next_row = row - 1;
		if (next_row < 0) {
			// Move to previous page
			int prev_page = page - 1;
			if (prev_page >= 0) {
				page = prev_page;
				int new_start = page * kItemsPerPage;
				int new_end = std::min(new_start + kItemsPerPage, num_items);
				int new_rows = (new_end - new_start + kColumns - 1) / kColumns;
				int target = new_start + (new_rows - 1) * kColumns + col;
				selected = std::min(target, new_end - 1);
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
			}
		} else {
			int next = page_start + next_row * kColumns + col;
			if (next >= page_start) {
				selected = next;
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
			}
		}
	}

	if (Input::IsTriggered(Input::DECISION)) {
		if (items[selected].enabled) {
			confirmed = true;
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		} else {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
		}
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		cancelled = true;
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
	}

	UpdateCursorPosition();
}

std::string UndertaleChoiceMenu::GetSelectedLabel() const {
	if (selected >= 0 && selected < static_cast<int>(items.size())) {
		return items[selected].label;
	}
	return "";
}

void UndertaleChoiceMenu::UpdateCursorPosition() {
	if (items.empty()) return;

	int page_start = page * kItemsPerPage;
	int local = selected - page_start;
	int col = local % kColumns;
	int row = local / kColumns;

	int col_width = (bounds_w - kPadding * 2) / kColumns;

	soul_x = bounds_x + kPadding + col * col_width + kSoulOffsetX;
	soul_y = bounds_y + kPadding + row * kLineHeight + kLineHeight / 2 - 4;
}

int UndertaleChoiceMenu::GetPageCount() const {
	if (items.empty()) return 0;
	return (static_cast<int>(items.size()) + kItemsPerPage - 1) / kItemsPerPage;
}

int UndertaleChoiceMenu::GetPageForIndex(int index) const {
	return index / kItemsPerPage;
}

} // namespace Chaos
