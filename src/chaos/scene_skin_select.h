/*
 * Chaos Fork: Skin Selection Scene
 * Browse charsets and select a character index for multiplayer appearance.
 */

#ifndef EP_CHAOS_SCENE_SKIN_SELECT_H
#define EP_CHAOS_SCENE_SKIN_SELECT_H

#include "scene.h"
#include "window_command.h"
#include "window_help.h"
#include "sprite.h"
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace Chaos {

struct SkinCharsetEntry {
	std::string display_name;
	std::string charset_name;
	std::string game_path;
	std::string game_name;
};

class Scene_SkinSelect : public Scene {
public:
	Scene_SkinSelect();

	void Start() override;
	void vUpdate() override;

private:
	enum class State {
		CharsetList,    // Browse available charsets
		IndexSelect,    // Pick character index 0-7
		CustomizeMode,  // Choose easy or advanced customization
		EasySelect,     // Choose a built-in effect
		AdvancedSelect, // Choose a detected source color
		ReplacementSelect // Choose the replacement color
	};

	void ScanCharsets();
	void CreateWindows();
	void UpdateCharsetList();
	void UpdateIndexSelect();
	void UpdateCustomizeMode();
	void UpdateEasySelect();
	void UpdateAdvancedSelect();
	void UpdateReplacementSelect();
	void UpdatePreview();
	void ApplySkin();
	BitmapRef LoadCharsetBitmap(const SkinCharsetEntry& entry);
	BitmapRef BuildEasyBitmap(int effect) const;
	BitmapRef BuildQuickPaletteBitmap(const std::vector<Color>& palette) const;
	BitmapRef BuildAdvancedBitmap() const;
	void DetectColors();
	std::vector<uint8_t> EncodeBitmap(const BitmapRef& bitmap) const;

	/** Read the raw bytes of the currently selected charset file */
	std::vector<uint8_t> ReadCharsetFileBytes(const SkinCharsetEntry& entry);

	State state = State::CharsetList;

	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_Command> charset_window;
	std::unique_ptr<Window_Command> customize_window;
	std::unique_ptr<Window_Command> easy_window;
	std::unique_ptr<Window_Command> advanced_window;
	std::unique_ptr<Window_Command> replacement_window;
	std::unique_ptr<Window_Help> info_window;

	std::vector<SkinCharsetEntry> charset_entries;
	std::string selected_charset;
	std::string selected_game_path;
	int selected_index = 0;
	int selected_color = -1;
	std::vector<Color> detected_colors;
	std::vector<Color> replacement_colors;
	std::vector<Color> color_replacements;
	BitmapRef customized_bitmap;

	// Preview sprites: up to 8 character previews
	std::unique_ptr<Sprite> preview_sprites[8];
	BitmapRef preview_bitmap;
	int preview_anim_frame = 0;
	int preview_anim_counter = 0;
	int last_highlighted = -1;
};

} // namespace Chaos

#endif
