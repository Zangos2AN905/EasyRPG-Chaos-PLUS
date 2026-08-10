/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

// Headers
#include "window_help.h"
#include "bitmap.h"
#include "font.h"
#include <algorithm>

Window_Help::Window_Help(int ix, int iy, int iwidth, int iheight, Drawable::Flags flags) :
	Window_Base(ix, iy, iwidth, iheight, flags),
	align(Text::AlignLeft) {

	SetContents(Bitmap::Create(width - 16, height - 16));

	contents->Clear();
}

void Window_Help::SetText(std::string text, int color, Text::Alignment align, bool halfwidthspace) {
	if (this->text != text || this->color != color || this->align != align) {
		contents->Clear();

		text_x_offset = 0;
		AddText(text, color, align, halfwidthspace);

		this->text = std::move(text);
		this->align = align;
		this->color = color;
		this->text_x_scroll = 0;
		this->text_x_scroll_dir = false;
		this->text_x_width = text_x_offset;
	}
}

void Window_Help::Clear() {
	this->text = "";
	this->color = Font::ColorDefault;
	this->align = Text::AlignLeft;
	text_x_offset = 0;
	contents->Clear();
}

void Window_Help::AddText(std::string text, int color, Text::Alignment align, bool halfwidthspace) {
	const int start_x = text_x_offset;
	int max_width = 0;
	int line_y = 2;
	std::string::size_type line_start = 0;
	while (line_start <= text.size()) {
		const auto line_end = text.find('\n', line_start);
		const auto line = std::string_view(text).substr(line_start, line_end - line_start);
		text_x_offset = start_x;

		std::string::size_type pos = 0;
		while (pos <= line.size()) {
			const auto nextpos = line.find(' ', pos);
			auto segment = line.substr(pos, nextpos - pos);
			auto offset = contents->TextDraw(text_x_offset, line_y, color, segment, align);
			text_x_offset += offset.x;

			// Special handling for proportional fonts: If the "normal" space is already small do not half it again
			if (nextpos != std::string_view::npos) {
				int space_width = Text::GetSize(*(font ? font : Font::Default()), " ").width;
				text_x_offset += (halfwidthspace && space_width >= 6) ? space_width / 2 : space_width;
				pos = nextpos + 1;
			} else {
				break;
			}
		}
		max_width = std::max(max_width, text_x_offset - start_x);

		if (line_end == std::string::npos) break;
		line_start = line_end + 1;
		line_y += Text::GetSize(*(font ? font : Font::Default()), "A").height;
	}
	text_x_offset = start_x + max_width;
}

void Window_Help::SetAnimation(Window_Help::Animation animation) {
	text_x_scroll = 0;
	this->animation = animation;
}

void Window_Help::UpdateScroll() {
	if (animation == Animation::None) {
		return;
	}

	if (text_x_width <= contents->GetWidth()) {
		// no need to scroll
		return;
	}

	if (animation == Animation::BackAndForth) {
		text_x_scroll += text_x_scroll_dir ? 1 : -1;

		if ((!text_x_scroll_dir && (text_x_width + text_x_scroll) == contents->GetWidth()) ||
				(text_x_scroll_dir && text_x_scroll == 0)) {
			text_x_scroll_dir = !text_x_scroll_dir;
		}

		contents->Clear();
		text_x_offset = text_x_scroll;
		AddText(text, color, align, true);
	} else if (animation == Animation::Loop) {
		--text_x_scroll;

		const int gap_size = 18;

		if (text_x_scroll == -text_x_width - gap_size) {
			text_x_scroll = 0;
		}

		contents->Clear();
		text_x_offset = text_x_scroll;
		AddText(text, color, align, true);
		text_x_offset += gap_size;
		AddText(text, color, align, true);
	}
}

void Window_Help::Update() {
	Window_Base::Update();
	UpdateScroll();
}
