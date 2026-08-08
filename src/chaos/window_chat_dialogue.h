/*
 * Chaos Fork: Custom chat dialogue window.
 * Resembles the RPG message box (face + text) but is non-blocking,
 * auto-dismisses after a timer, and does not respond to Z/Enter.
 */

#ifndef EP_CHAOS_WINDOW_CHAT_DIALOGUE_H
#define EP_CHAOS_WINDOW_CHAT_DIALOGUE_H

#include "window_base.h"
#include <string>

namespace Chaos {

class Window_ChatDialogue : public Window_Base {
public:
	/**
	 * @param face_name  FaceSet graphic filename (empty = no face).
	 * @param face_index Index within the FaceSet (0-7).
	 * @param sender     Name of the sender.
	 * @param message    Chat message text.
	 * @param frames     Number of frames before auto-dismiss (after text fully revealed).
	 */
	Window_ChatDialogue(const std::string& face_name, int face_index,
						const std::string& sender, const std::string& message,
						int frames);

	void Update() override;

	/** Whether the window has finished its display time. */
	bool IsFinished() const { return finished; }

private:
	void DrawNextChar();

	// Full display text and typewriter state
	std::string full_text;
	int text_x_start = 0; // x offset (accounts for face)
	int char_index = 0;   // how many chars revealed so far
	int text_draw_x = 0;  // current x draw position
	bool text_done = false;

	// Typewriter speed: draw one char every N frames
	static constexpr int CHARS_PER_FRAME = 1;
	int char_wait = 0;

	int timer = 0;
	bool finished = false;
};

} // namespace Chaos

#endif
