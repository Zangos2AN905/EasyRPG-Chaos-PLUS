/*
 * Chaos Fork: Custom chat dialogue window implementation.
 */

#include "chaos/window_chat_dialogue.h"
#include "bitmap.h"
#include "cache.h"
#include "font.h"
#include "player.h"
#include "game_system.h"
#include "main_data.h"

namespace Chaos {

Window_ChatDialogue::Window_ChatDialogue(
	const std::string& face_name, int face_index,
	const std::string& sender, const std::string& message,
	int frames)
	: Window_Base(0, Player::screen_height - 80, Player::screen_width, 80)
	, timer(frames)
{
	SetZ(Priority_Window + 190);

	// Content area = width-16 x height-16 (8px border each side)
	SetContents(Bitmap::Create(width - 16, height - 16));

	// Layout constants matching Window_Message
	constexpr int LeftMargin = 8;
	constexpr int FaceSize = 48;
	constexpr int RightFaceMargin = 16;
	constexpr int TopMargin = 2;

	text_x_start = 0;

	// Draw face if provided
	if (!face_name.empty()) {
		DrawFace(face_name, face_index, LeftMargin, TopMargin);
		text_x_start = LeftMargin + FaceSize + RightFaceMargin; // 72
	}

	// Store full text for typewriter
	full_text = sender + ": " + message;
	text_draw_x = text_x_start;
	char_index = 0;
	text_done = false;

	// Play message open sound
	Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));

	SetVisible(true);
	SetActive(false); // non-interactive
}

void Window_ChatDialogue::DrawNextChar() {
	if (char_index >= static_cast<int>(full_text.size())) {
		text_done = true;
		return;
	}

	constexpr int TopMargin = 2;
	char ch = full_text[char_index];
	std::string glyph(1, ch);

	auto result = contents->TextDraw(text_draw_x, TopMargin, Font::ColorDefault, glyph);
	text_draw_x += result.x;
	char_index++;

	if (char_index >= static_cast<int>(full_text.size())) {
		text_done = true;
	}
}

void Window_ChatDialogue::Update() {
	Window_Base::Update();

	if (finished) return;

	// Typewriter: reveal characters
	if (!text_done) {
		for (int i = 0; i < CHARS_PER_FRAME; ++i) {
			DrawNextChar();
		}
		return;
	}

	// After text fully revealed, count down the dismiss timer
	timer--;
	if (timer <= 0) {
		finished = true;
		SetVisible(false);
	}
}

} // namespace Chaos
