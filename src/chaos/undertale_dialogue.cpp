/*
 * Chaos Fork: Undertale Dialogue System implementation
 */

#include "chaos/undertale_dialogue.h"
#include "chaos/undertale_font.h"
#include "audio.h"
#include "audio_secache.h"
#include "filefinder.h"
#include "font.h"
#include "text.h"
#include "output.h"
#include "drawable_mgr.h"
#include "utils.h"
#include "color.h"

namespace Chaos {

namespace {
	constexpr Color kDialogueTextColor(255, 255, 255, 255);
	constexpr int kDialogueLineSpacing = 2;
	constexpr int kDialoguePadding = 4;
	constexpr int kDialogueAsteriskWidth = 14;

	void PlayDialogueSound(const std::string& name) {
		auto chaos_fs = FileFinder::ChaosAssets();
		if (!chaos_fs) return;

		const char* snd_name = name.empty() ? "snd_txt1" : name.c_str();

		auto stream = chaos_fs.OpenFile("UndertaleMode.content/Sound", snd_name, FileFinder::SOUND_TYPES);
		if (!stream) return;

		auto se_cache = AudioSeCache::Create(std::move(stream), snd_name);
		if (!se_cache) return;

		Audio().SE_Play(std::move(se_cache), 100, 100, 50);
	}
}

UndertaleDialogue::UndertaleDialogue()
	: Drawable(Priority_Window + 2, Drawable::Flags::Default)
{
	DrawableMgr::Register(this);
	SetVisible(false);
}

void UndertaleDialogue::Draw(Bitmap& dst) {
	if (!active || !text_surface) return;

	dst.Blit(bounds_x, bounds_y, *text_surface, text_surface->GetRect(), Opacity::Opaque());
}

void UndertaleDialogue::SetBounds(int x, int y, int width, int height) {
	bounds_x = x;
	bounds_y = y;
	bounds_w = width;
	bounds_h = height;
}

void UndertaleDialogue::SetPages(std::vector<UndertaleDialoguePage> new_pages) {
	pages = std::move(new_pages);
	current_page = 0;
	active = true;
	SetVisible(true);
	StartCurrentPage();
}

void UndertaleDialogue::AddPage(UndertaleDialoguePage page) {
	pages.push_back(std::move(page));
}

void UndertaleDialogue::Update() {
	if (!active || page_complete) return;

	++frame_counter;

	const auto& page = pages[current_page];
	if (frame_counter >= page.char_delay) {
		frame_counter = 0;

		if (revealed_chars < total_chars) {
			++revealed_chars;

			// Get the character just revealed to decide on sound
			std::string display_text = page.text;
			if (narrator_style && !display_text.empty()) {
				std::string result = "* ";
				for (char c : display_text) {
					result += c;
					if (c == '\n') result += "* ";
				}
				display_text = std::move(result);
			}

			const char* iter = display_text.data();
			const char* end = iter + display_text.size();
			int char_index = 0;
			char32_t last_ch = 0;
			while (iter != end && char_index < revealed_chars) {
				auto ret = Utils::UTF8Next(iter, end);
				iter = ret.next;
				if (ret) {
					last_ch = ret.ch;
					++char_index;
				}
			}

			// Play sound for non-whitespace characters
			if (last_ch != U' ' && last_ch != U'\n') {
				PlayCharSound();
			}

			RebuildTextBitmap();
		}

		if (revealed_chars >= total_chars) {
			page_complete = true;
		}
	}
}

void UndertaleDialogue::SkipToEnd() {
	if (!active) return;

	revealed_chars = total_chars;
	page_complete = true;
	RebuildTextBitmap();
}

bool UndertaleDialogue::NextPage() {
	if (!active) return false;

	++current_page;
	if (current_page >= static_cast<int>(pages.size())) {
		Reset();
		if (on_complete) on_complete();
		return false;
	}

	StartCurrentPage();
	return true;
}

bool UndertaleDialogue::IsPageComplete() const {
	return page_complete;
}

bool UndertaleDialogue::IsAllComplete() const {
	return !active;
}

void UndertaleDialogue::Reset() {
	active = false;
	page_complete = false;
	revealed_chars = 0;
	total_chars = 0;
	current_page = 0;
	pages.clear();
	text_surface.reset();
	SetVisible(false);
}

void UndertaleDialogue::SetOnComplete(std::function<void()> callback) {
	on_complete = std::move(callback);
}

void UndertaleDialogue::StartCurrentPage() {
	if (current_page >= static_cast<int>(pages.size())) return;

	const auto& page = pages[current_page];
	revealed_chars = 0;
	frame_counter = 0;
	page_complete = false;

	// Count total UTF-8 characters in the display string
	std::string display_text = page.text;
	if (narrator_style && !display_text.empty()) {
		std::string result = "* ";
		for (char c : display_text) {
			result += c;
			if (c == '\n') result += "* ";
		}
		display_text = std::move(result);
	}

	total_chars = 0;
	const char* iter = display_text.data();
	const char* end = iter + display_text.size();
	while (iter != end) {
		auto ret = Utils::UTF8Next(iter, end);
		iter = ret.next;
		if (ret) ++total_chars;
	}

	RebuildTextBitmap();
}

void UndertaleDialogue::PlayCharSound() {
	if (current_page >= static_cast<int>(pages.size())) return;
	PlayDialogueSound(pages[current_page].voice_sound);
}

void UndertaleDialogue::RebuildTextBitmap() {
	if (current_page >= static_cast<int>(pages.size())) return;

	auto font = UndertaleFont::Dialogue();
	if (!font) {
		font = Font::Default();
	}

	// Create or reuse the text surface
	if (!text_surface || text_surface->GetWidth() != bounds_w || text_surface->GetHeight() != bounds_h) {
		text_surface = Bitmap::Create(bounds_w, bounds_h, true);
	}
	text_surface->Clear();

	const auto& page = pages[current_page];

	// Build substring of revealed characters
	std::string display_text = page.text;
	if (narrator_style && !display_text.empty()) {
		std::string result = "* ";
		for (char c : display_text) {
			result += c;
			if (c == '\n') result += "* ";
		}
		display_text = std::move(result);
	}

	// Extract only the revealed portion
	std::string revealed_text;
	const char* iter = display_text.data();
	const char* end = iter + display_text.size();
	int char_count = 0;
	while (iter != end && char_count < revealed_chars) {
		auto ret = Utils::UTF8Next(iter, end);
		if (ret) {
			revealed_text.append(iter, ret.next);
			++char_count;
		}
		iter = ret.next;
	}

	// Apply font style: no shadow, no gradient, just white
	Font::Style style;
	style.size = font->GetCurrentStyle().size;
	style.draw_shadow = false;
	style.draw_gradient = false;
	auto guard = font->ApplyStyle(style);

	// Render text with manual line wrapping
	int draw_x = kDialoguePadding;
	int draw_y = kDialoguePadding;
	int line_height = style.size + kDialogueLineSpacing;
	int max_x = bounds_w - kDialoguePadding;

	const char* text_iter = revealed_text.data();
	const char* text_end = text_iter + revealed_text.size();

	while (text_iter != text_end) {
		auto ret = Utils::UTF8Next(text_iter, text_end);
		text_iter = ret.next;
		if (!ret) continue;

		if (ret.ch == U'\n') {
			draw_x = kDialoguePadding;
			draw_y += line_height;
			continue;
		}

		// Check if we need to wrap
		Rect glyph_size = font->GetSize(ret.ch);
		if (draw_x + glyph_size.width > max_x) {
			draw_x = kDialoguePadding;
			draw_y += line_height;
		}

		if (draw_y + line_height > bounds_h) break;

		auto advance = font->Render(*text_surface, draw_x, draw_y, kDialogueTextColor, ret.ch);
		draw_x += advance.x;
	}
}

} // namespace Chaos
