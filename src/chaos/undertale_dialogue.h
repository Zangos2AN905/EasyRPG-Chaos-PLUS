/*
 * Chaos Fork: Undertale Dialogue System
 * Typewriter-style text rendering inside the battle box.
 * Displays text character-by-character with configurable speed and sound.
 */

#ifndef EP_CHAOS_UNDERTALE_DIALOGUE_H
#define EP_CHAOS_UNDERTALE_DIALOGUE_H

#include "drawable.h"
#include "bitmap.h"
#include "memory_management.h"

#include <string>
#include <vector>
#include <functional>

namespace Chaos {

/**
 * A single page of Undertale dialogue.
 * Each page can have multiple lines (auto-wrapped) and a voice sound.
 */
struct UndertaleDialoguePage {
	/** The full text of this page (may contain newlines) */
	std::string text;
	/** Sound effect name to play for each character (empty = default "snd_txt1") */
	std::string voice_sound;
	/** Frames between each character reveal (default ~2 for Undertale speed) */
	int char_delay = 2;
};

/**
 * Undertale-style typewriter dialogue renderer.
 * Draws text inside a specified rectangle, revealing characters one at a time.
 * Plays a sound per character. Supports multiple pages with Z/Enter to advance.
 */
class UndertaleDialogue : public Drawable {
public:
	UndertaleDialogue();

	void Draw(Bitmap& dst) override;

	/** Set the drawing area (inner bounds of the battle box) */
	void SetBounds(int x, int y, int width, int height);

	/** Start displaying a sequence of pages */
	void SetPages(std::vector<UndertaleDialoguePage> pages);

	/** Add a single page */
	void AddPage(UndertaleDialoguePage page);

	/** Update typewriter state (call once per frame) */
	void Update();

	/** Skip to end of current page (when player presses confirm mid-typewrite) */
	void SkipToEnd();

	/** Advance to next page. Returns false if no more pages. */
	bool NextPage();

	/** Whether the current page has finished revealing all text */
	bool IsPageComplete() const;

	/** Whether all pages have been shown and dismissed */
	bool IsAllComplete() const;

	/** Whether dialogue is currently active */
	bool IsActive() const { return active; }

	/** Reset and hide */
	void Reset();

	/** Set callback for when all dialogue is finished */
	void SetOnComplete(std::function<void()> callback);

	/** Set whether an asterisk is prepended to dialogue lines (Undertale narrator style) */
	void SetNarratorStyle(bool enabled) { narrator_style = enabled; }

private:
	void StartCurrentPage();
	void PlayCharSound();
	void RebuildTextBitmap();

	int bounds_x = 0;
	int bounds_y = 0;
	int bounds_w = 256;
	int bounds_h = 60;

	std::vector<UndertaleDialoguePage> pages;
	int current_page = 0;
	int revealed_chars = 0;
	int total_chars = 0;
	int frame_counter = 0;
	bool active = false;
	bool page_complete = false;
	bool narrator_style = true;

	BitmapRef text_surface;
	std::function<void()> on_complete;
};

} // namespace Chaos

#endif
