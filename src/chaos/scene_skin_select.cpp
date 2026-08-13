/*
 * Chaos Fork: Skin Selection Scene Implementation
 */

#include "chaos/scene_skin_select.h"
#include "filefinder_rtp.h"
#include "chaos/multiplayer_state.h"
#include "bitmap.h"
#include "cache.h"
#include "filefinder.h"
#include "input.h"
#include "main_data.h"
#include "game_system.h"
#include "output.h"
#include "player.h"
#include "sprite_character.h"
#include "utils.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Chaos {

// Standard charset: 288x256, each character = 72x128 (3 frames x 24px, 4 dirs x 32px)
static constexpr int CHAR_WIDTH = 24;
static constexpr int CHAR_HEIGHT = 32;
static constexpr int CHAR_FRAMES = 3;
static constexpr int CHAR_DIRS = 4;
static constexpr int CHARS_PER_ROW = 4;
static constexpr int CHARS_ROWS = 2;
static constexpr int WINDOW_TOP = 32;
static constexpr int WINDOW_BOTTOM = 32;

static std::string ColorName(const Color& color) {
	std::ostringstream stream;
	stream << "#" << std::hex << std::uppercase;
	stream.width(2); stream.fill('0'); stream << static_cast<int>(color.red);
	stream.width(2); stream << static_cast<int>(color.green);
	stream.width(2); stream << static_cast<int>(color.blue);
	return stream.str();
}

static std::vector<Color> ReplacementPalette() {
	return {
		Color(0, 0, 0, 255), Color(255, 255, 255, 255), Color(255, 0, 0, 255),
		Color(0, 255, 0, 255), Color(0, 128, 255, 255), Color(255, 255, 0, 255),
		Color(255, 128, 0, 255), Color(255, 0, 255, 255), Color(128, 0, 255, 255),
		Color(128, 128, 128, 255), Color(128, 64, 32, 255)
	};
}

static bool IsBackgroundColor(const Color& color, const Color& background) {
	return color.alpha == 0 || (color.red == background.red &&
		color.green == background.green && color.blue == background.blue);
}

static std::string GetExecutableDirectory() {
#ifdef _WIN32
	wchar_t exe_path[MAX_PATH];
	GetModuleFileNameW(NULL, exe_path, MAX_PATH);
	std::wstring wpath(exe_path);
	size_t pos = wpath.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		wpath = wpath.substr(0, pos);
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
	std::string result(len > 0 ? len - 1 : 0, '\0');
	if (len > 1) {
		WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], len, NULL, NULL);
	}
	return result;
#else
	return ".";
#endif
}

static std::string PathLeafName(const std::string& path) {
	std::error_code ec;
	auto p = std::filesystem::path(path);
	auto name = p.filename().u8string();
	if (!name.empty()) {
		return name;
	}
	return path;
}

static void AddCharsetsFromGamePath(const std::string& game_path, const std::string& game_name,
		std::vector<SkinCharsetEntry>& entries, std::vector<std::string>& seen_keys) {
	auto fs = FileFinder::Root().Create(game_path);
	if (!fs) {
		return;
	}

	auto* dir_entries = fs.ListDirectory("CharSet");
	if (!dir_entries) {
		return;
	}

	for (auto& [key, entry] : *dir_entries) {
		if (entry.type != DirectoryTree::FileType::Regular) {
			continue;
		}

		std::string charset_name = entry.name;
		auto dot = charset_name.rfind('.');
		if (dot != std::string::npos) {
			charset_name = charset_name.substr(0, dot);
		}

		std::string seen_key = Utils::LowerCase(game_path) + "|" + Utils::LowerCase(charset_name);
		if (std::find(seen_keys.begin(), seen_keys.end(), seen_key) != seen_keys.end()) {
			continue;
		}
		seen_keys.push_back(seen_key);

		SkinCharsetEntry item;
		item.display_name = game_name + ": " + charset_name;
		item.charset_name = charset_name;
		item.game_path = game_path;
		item.game_name = game_name;
		entries.push_back(std::move(item));
	}
}

Scene_SkinSelect::Scene_SkinSelect() {
	type = Scene::SkinSelect;
}

void Scene_SkinSelect::Start() {
	ScanCharsets();
	CreateWindows();
}

void Scene_SkinSelect::ScanCharsets() {
	charset_entries.clear();
	std::vector<std::string> seen_keys;

	auto game_fs = FileFinder::Game();
	if (game_fs) {
		const std::string current_game_path = game_fs.GetFullPath();
		if (!current_game_path.empty()) {
			std::string game_name = !Player::game_title.empty() ? Player::game_title : PathLeafName(current_game_path);
			AddCharsetsFromGamePath(current_game_path, game_name, charset_entries, seen_keys);

			std::error_code ec;
			auto parent = std::filesystem::path(current_game_path).parent_path();
			if (!parent.empty() && std::filesystem::is_directory(parent, ec)) {
				for (const auto& dir_entry : std::filesystem::directory_iterator(parent, ec)) {
					if (ec || !dir_entry.is_directory()) {
						continue;
					}
					auto sibling_path = dir_entry.path().u8string();
					AddCharsetsFromGamePath(sibling_path, dir_entry.path().filename().u8string(), charset_entries, seen_keys);
				}
			}
		}
	}

	// RTP charsets are valid RPG Maker assets too. Expose them in the skin
	// picker so players can use the standard RTP character sheets.
	if (Main_Data::filefinder_rtp) {
		for (const auto& rtp_fs : Main_Data::filefinder_rtp->GetSearchPaths()) {
			const std::string rtp_path = rtp_fs.GetFullPath();
			if (!rtp_path.empty()) {
				AddCharsetsFromGamePath(rtp_path, "RTP: " + PathLeafName(rtp_path), charset_entries, seen_keys);
			}
		}
	}

	const std::string exe_dir = GetExecutableDirectory();
	if (!exe_dir.empty()) {
		std::error_code ec;
		if (std::filesystem::is_directory(exe_dir, ec)) {
			for (const auto& dir_entry : std::filesystem::directory_iterator(exe_dir, ec)) {
				if (ec || !dir_entry.is_directory()) {
					continue;
				}
				AddCharsetsFromGamePath(dir_entry.path().u8string(), dir_entry.path().filename().u8string(), charset_entries, seen_keys);
			}
		}
	}

	std::sort(charset_entries.begin(), charset_entries.end(), [](const SkinCharsetEntry& lhs, const SkinCharsetEntry& rhs) {
		return Utils::LowerCase(lhs.display_name) < Utils::LowerCase(rhs.display_name);
	});
}

void Scene_SkinSelect::CreateWindows() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	help_window->SetText("Select a charset for your multiplayer skin");

	std::vector<std::string> display_names;
	if (charset_entries.empty()) {
		display_names.push_back("(No charsets found)");
	} else {
		for (const auto& entry : charset_entries) {
			display_names.push_back(entry.display_name);
		}
	}

	const int visible_rows = std::max(1, (Player::screen_height - WINDOW_TOP - WINDOW_BOTTOM) / 16);
	charset_window = std::make_unique<Window_Command>(display_names, Player::screen_width / 2, visible_rows);
	charset_window->SetX(0);
	charset_window->SetY(32);

	info_window = std::make_unique<Window_Help>(
		0, Player::screen_height - 32, Player::screen_width, 32);

	auto& mp = MultiplayerState::Instance();
	if (mp.HasSkin()) {
		info_window->SetText("Current: " + mp.GetSkinCharsetName() + " #" + std::to_string(mp.GetSkinCharIndex()));
	} else {
		info_window->SetText("No skin selected (using default appearance)");
	}

	// Create preview sprites (initialized empty, updated each frame)
	for (int i = 0; i < 8; ++i) {
		preview_sprites[i] = std::make_unique<Sprite>();
		preview_sprites[i]->SetVisible(false);
	}

	customize_window = std::make_unique<Window_Command>(
		std::vector<std::string>{"Easy", "Advanced", "Use original"}, Player::screen_width / 2);
	customize_window->SetX(Player::screen_width / 4);
	customize_window->SetY(48);
	customize_window->SetVisible(false);

	easy_window = std::make_unique<Window_Command>(
		std::vector<std::string>{"Original", "Invert", "Hue +60", "Hue +180", "Monochrome",
			"Fell", "Ice", "Moge-ko", "Rawberry"}, Player::screen_width / 2);
	easy_window->SetX(Player::screen_width / 4);
	easy_window->SetY(48);
	easy_window->SetVisible(false);
}

void Scene_SkinSelect::vUpdate() {
	help_window->Update();
	info_window->Update();

	switch (state) {
		case State::CharsetList:
			UpdateCharsetList();
			break;
		case State::IndexSelect:
			UpdateIndexSelect();
			break;
		case State::CustomizeMode:
			UpdateCustomizeMode();
			break;
		case State::EasySelect:
			UpdateEasySelect();
			break;
		case State::AdvancedSelect:
			UpdateAdvancedSelect();
			break;
		case State::ReplacementSelect:
			UpdateReplacementSelect();
			break;
	}

	UpdatePreview();
}

void Scene_SkinSelect::UpdateCharsetList() {
	charset_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		int idx = charset_window->GetIndex();
		if (idx >= 0 && idx < static_cast<int>(charset_entries.size())) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			selected_charset = charset_entries[idx].charset_name;
			selected_game_path = charset_entries[idx].game_path;
			state = State::IndexSelect;
			selected_index = 0;
			charset_window->SetActive(false);
			charset_window->SetVisible(false);
			help_window->SetText("Choose a character (LEFT/RIGHT to select, DECISION to confirm)");

			// Load the charset bitmap for preview
			preview_bitmap = LoadCharsetBitmap(charset_entries[idx]);
		} else {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
		}
	}
}

void Scene_SkinSelect::UpdateIndexSelect() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		state = State::CharsetList;
		charset_window->SetActive(true);
		charset_window->SetVisible(true);
		preview_bitmap.reset();
		for (int i = 0; i < 8; ++i) {
			preview_sprites[i]->SetVisible(false);
		}
		help_window->SetText("Select a charset for your multiplayer skin");
		return;
	}

	if (Input::IsTriggered(Input::LEFT)) {
		selected_index = (selected_index + 7) % 8;
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
	}
	if (Input::IsTriggered(Input::RIGHT)) {
		selected_index = (selected_index + 1) % 8;
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
	}
	if (Input::IsTriggered(Input::UP)) {
		selected_index = (selected_index < 4) ? selected_index + 4 : selected_index - 4;
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
	}
	if (Input::IsTriggered(Input::DOWN)) {
		selected_index = (selected_index < 4) ? selected_index + 4 : selected_index - 4;
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		customize_window->SetVisible(true);
		customize_window->SetActive(true);
		state = State::CustomizeMode;
		help_window->SetText("Customize your character: choose Easy or Advanced");
		return;
	}
}

void Scene_SkinSelect::UpdateCustomizeMode() {
	customize_window->Update();
	if (Input::IsTriggered(Input::CANCEL)) {
		customize_window->SetVisible(false);
		customize_window->SetActive(false);
		state = State::IndexSelect;
		return;
	}
	if (!Input::IsTriggered(Input::DECISION)) return;
	const int choice = customize_window->GetIndex();
	if (choice == 0) {
		customize_window->SetVisible(false);
		easy_window->SetVisible(true);
		easy_window->SetActive(true);
		state = State::EasySelect;
		help_window->SetText("Easy customization: apply an effect");
	} else if (choice == 1) {
		DetectColors();
		std::vector<std::string> options;
		for (const auto& color : detected_colors) options.push_back("Replace " + ColorName(color));
		options.push_back("Apply replacements");
		advanced_window = std::make_unique<Window_Command>(options, Player::screen_width / 2);
		advanced_window->SetX(Player::screen_width / 4);
		advanced_window->SetY(48);
		advanced_window->SetVisible(true);
		advanced_window->SetActive(true);
		customize_window->SetVisible(false);
		state = State::AdvancedSelect;
		help_window->SetText("Advanced customization: choose a detected color");
	} else {
		customized_bitmap.reset();
		ApplySkin();
		Scene::Pop();
	}
}

void Scene_SkinSelect::UpdateEasySelect() {
	easy_window->Update();
	if (Input::IsTriggered(Input::CANCEL)) {
		easy_window->SetVisible(false);
		easy_window->SetActive(false);
		customize_window->SetVisible(true);
		customize_window->SetActive(true);
		state = State::CustomizeMode;
		return;
	}
	if (!Input::IsTriggered(Input::DECISION)) return;
	const int effect = easy_window->GetIndex();
	if (effect >= 5) {
		DetectColors();
		static const std::vector<std::vector<Color>> palettes = {
			// Fell / Underfell: hot reds with a dark base.
			{Color(240, 75, 75, 255), Color(143, 29, 29, 255), Color(58, 7, 7, 255), Color(181, 45, 45, 255)},
			// Ice: pale cyan, blue and deep cold shadows.
			{Color(217, 246, 255, 255), Color(121, 217, 255, 255), Color(22, 59, 96, 255), Color(61, 143, 202, 255)},
			// Moge-ko: hair/hat, long shirt, tshirt, pants/skirt.
			{Color(0xFF, 0xD9, 0x9B, 255), Color(0xF7, 0x64, 0x6E, 255), Color(0x1B, 0x0C, 0x05, 255), Color(0xBE, 0x2E, 0x45, 255)},
			// Rawberry Preserves: hair, shirt, wings.
			{Color(0xFF, 0xAF, 0x93, 255), Color(0x28, 0x1D, 0x19, 255), Color(0xC3, 0x1E, 0x23, 255)}
		};
		customized_bitmap = BuildQuickPaletteBitmap(palettes[effect - 5]);
	} else {
		customized_bitmap = BuildEasyBitmap(effect);
	}
	ApplySkin();
	Scene::Pop();
}

void Scene_SkinSelect::UpdateAdvancedSelect() {
	advanced_window->Update();
	if (Input::IsTriggered(Input::CANCEL)) {
		advanced_window->SetVisible(false);
		advanced_window->SetActive(false);
		customize_window->SetVisible(true);
		customize_window->SetActive(true);
		state = State::CustomizeMode;
		return;
	}
	if (!Input::IsTriggered(Input::DECISION)) return;
	const int index = advanced_window->GetIndex();
	if (index == static_cast<int>(detected_colors.size())) {
		customized_bitmap = BuildAdvancedBitmap();
		ApplySkin();
		Scene::Pop();
		return;
	}

	selected_color = index;
	std::vector<std::string> options{"Keep original"};
	for (const auto& color : replacement_colors) options.push_back(ColorName(color));
	replacement_window = std::make_unique<Window_Command>(options, Player::screen_width / 2);
	replacement_window->SetX(Player::screen_width / 4);
	replacement_window->SetY(48);
	replacement_window->SetVisible(true);
	replacement_window->SetActive(true);
	advanced_window->SetVisible(false);
	state = State::ReplacementSelect;
	help_window->SetText("Choose replacement for " + ColorName(detected_colors[selected_color]));
}

void Scene_SkinSelect::UpdateReplacementSelect() {
	replacement_window->Update();
	if (Input::IsTriggered(Input::CANCEL)) {
		replacement_window->SetVisible(false);
		replacement_window->SetActive(false);
		advanced_window->SetVisible(true);
		advanced_window->SetActive(true);
		state = State::AdvancedSelect;
		return;
	}
	if (!Input::IsTriggered(Input::DECISION)) return;
	const int index = replacement_window->GetIndex();
	color_replacements[selected_color] = index == 0 ? detected_colors[selected_color] : replacement_colors[index - 1];
	replacement_window->SetVisible(false);
	replacement_window->SetActive(false);
	advanced_window->SetVisible(true);
	advanced_window->SetActive(true);
	state = State::AdvancedSelect;
}

void Scene_SkinSelect::UpdatePreview() {
	// Animate preview sprites
	preview_anim_counter++;
	if (preview_anim_counter >= 12) {
		preview_anim_counter = 0;
		preview_anim_frame = (preview_anim_frame + 1) % CHAR_FRAMES;
	}

	if (state == State::CharsetList) {
		// Show single character preview from highlighted charset
		int idx = charset_window->GetIndex();
		if (idx != last_highlighted && idx >= 0 && idx < static_cast<int>(charset_entries.size())) {
			last_highlighted = idx;
			preview_bitmap = LoadCharsetBitmap(charset_entries[idx]);
		}

		// Hide all but first sprite in charset list mode, show character 0 facing down
		for (int i = 1; i < 8; ++i) {
			preview_sprites[i]->SetVisible(false);
		}

		if (preview_bitmap) {
			int cw = preview_bitmap->GetWidth() / (CHARS_PER_ROW * CHAR_FRAMES);
			int ch = preview_bitmap->GetHeight() / (CHARS_ROWS * CHAR_DIRS);
			if (cw <= 0 || ch <= 0) {
				preview_sprites[0]->SetVisible(false);
				return;
			}

			preview_sprites[0]->SetBitmap(preview_bitmap);
			preview_sprites[0]->SetSrcRect(Rect(preview_anim_frame * cw, 2 * ch, cw, ch));
			preview_sprites[0]->SetX(Player::screen_width / 2 + Player::screen_width / 4 - cw / 2);
			preview_sprites[0]->SetY(Player::screen_height / 2 - ch / 2);
			preview_sprites[0]->SetVisible(true);
		}
	} else if (state == State::IndexSelect && preview_bitmap) {
		// Show all 8 characters in a 4x2 grid
		int cw = preview_bitmap->GetWidth() / (CHARS_PER_ROW * CHAR_FRAMES);
		int ch = preview_bitmap->GetHeight() / (CHARS_ROWS * CHAR_DIRS);
		if (cw <= 0 || ch <= 0) {
			for (int i = 0; i < 8; ++i) {
				preview_sprites[i]->SetVisible(false);
			}
			return;
		}

		int grid_width = CHARS_PER_ROW * cw + (CHARS_PER_ROW - 1) * 16;
		int pane_left = Player::screen_width / 2;
		int pane_width = Player::screen_width - pane_left;
		int start_x = pane_left + std::max(8, (pane_width - grid_width) / 2);
		int start_y = 56;

		for (int i = 0; i < 8; ++i) {
			int col = i % CHARS_PER_ROW;
			int row = i / CHARS_PER_ROW;

			// Source rect: character i, facing down (direction row 2), current frame
			int src_x = (col * CHAR_FRAMES + preview_anim_frame) * cw;
			int src_y = row * CHAR_DIRS * ch + 2 * ch; // RPG Maker direction 2 is down

			preview_sprites[i]->SetBitmap(preview_bitmap);
			preview_sprites[i]->SetSrcRect(Rect(src_x, src_y, cw, ch));
			preview_sprites[i]->SetX(start_x + col * (cw + 16));
			preview_sprites[i]->SetY(start_y + row * (ch + 16));
			preview_sprites[i]->SetVisible(true);

			// Highlight selected character
			if (i == selected_index) {
				preview_sprites[i]->SetOpacity(255);
			} else {
				preview_sprites[i]->SetOpacity(128);
			}
		}

		info_window->SetText("Character " + std::to_string(selected_index) + " selected - Press DECISION to apply");
	}
}

BitmapRef Scene_SkinSelect::LoadCharsetBitmap(const SkinCharsetEntry& entry) {
	auto fs = FileFinder::Root().Create(entry.game_path);
	if (!fs) {
		return BitmapRef();
	}
	auto is = fs.OpenFile("CharSet", entry.charset_name, FileFinder::IMG_TYPES);
	if (!is) {
		return BitmapRef();
	}
	return Bitmap::Create(std::move(is), true);
}

std::vector<uint8_t> Scene_SkinSelect::ReadCharsetFileBytes(const SkinCharsetEntry& entry) {
	auto fs = FileFinder::Root().Create(entry.game_path);
	if (!fs) return {};
	auto is = fs.OpenFile("CharSet", entry.charset_name, FileFinder::IMG_TYPES);
	if (!is) return {};

	std::vector<uint8_t> data;
	// Read all bytes from the stream
	is.seekg(0, std::ios::end);
	auto size = is.tellg();
	if (size <= 0 || size > 1024 * 1024) return {};
	is.seekg(0, std::ios::beg);
	data.resize(static_cast<size_t>(size));
	is.read(reinterpret_cast<char*>(data.data()), size);
	return data;
}

void Scene_SkinSelect::ApplySkin() {
	auto& mp = MultiplayerState::Instance();
	int idx = charset_window ? charset_window->GetIndex() : -1;
	if (idx < 0 || idx >= static_cast<int>(charset_entries.size())) {
		return;
	}

	// Read the transformed or original charset bytes for network transfer.
	auto image_data = customized_bitmap ? EncodeBitmap(customized_bitmap) : ReadCharsetFileBytes(charset_entries[idx]);
	if (image_data.empty()) {
		Output::Warning("Skin: Failed to read charset file '{}'", selected_charset);
		return;
	}

	mp.SetSkin(selected_charset, selected_index, image_data);

	// If multiplayer is active, broadcast immediately
	if (mp.IsActive()) {
		mp.BroadcastSkin();
	}

	Output::Debug("Skin: Applied skin '{}' index {}", selected_charset, selected_index);
}

BitmapRef Scene_SkinSelect::BuildEasyBitmap(int effect) const {
	if (!preview_bitmap || effect == 0) return preview_bitmap;
	const Color background = preview_bitmap->GetColorAt(0, 0);
	BitmapRef result = Bitmap::Create(preview_bitmap->GetWidth(), preview_bitmap->GetHeight(), true);
	if (effect == 2 || effect == 3) {
		result->HueChangeBlit(0, 0, *preview_bitmap, preview_bitmap->GetRect(), effect == 2 ? 60 : 180);
		for (int y = 0; y < preview_bitmap->GetHeight(); ++y) {
			for (int x = 0; x < preview_bitmap->GetWidth(); ++x) {
				const Color source = preview_bitmap->GetColorAt(x, y);
				if (IsBackgroundColor(source, background)) result->SetColorAt(x, y, source);
			}
		}
		return result;
	}

	for (int y = 0; y < preview_bitmap->GetHeight(); ++y) {
		for (int x = 0; x < preview_bitmap->GetWidth(); ++x) {
			Color color = preview_bitmap->GetColorAt(x, y);
			if (IsBackgroundColor(color, background)) {
				result->SetColorAt(x, y, color);
				continue;
			}
			if (effect == 1) {
				color.red = 255 - color.red;
				color.green = 255 - color.green;
				color.blue = 255 - color.blue;
			} else {
				const uint8_t gray = static_cast<uint8_t>((30 * color.red + 59 * color.green + 11 * color.blue) / 100);
				color.red = color.green = color.blue = gray;
			}
			result->SetColorAt(x, y, color);
		}
	}
	return result;
}

BitmapRef Scene_SkinSelect::BuildQuickPaletteBitmap(const std::vector<Color>& palette) const {
	if (!preview_bitmap) return preview_bitmap;
	const Color background = preview_bitmap->GetColorAt(0, 0);
	BitmapRef result = Bitmap::Create(preview_bitmap->GetWidth(), preview_bitmap->GetHeight(), true);
	for (int y = 0; y < preview_bitmap->GetHeight(); ++y) {
		for (int x = 0; x < preview_bitmap->GetWidth(); ++x) {
			Color color = preview_bitmap->GetColorAt(x, y);
			if (!IsBackgroundColor(color, background)) {
				for (size_t i = 0; i < detected_colors.size() && i < palette.size(); ++i) {
					if (color.red == detected_colors[i].red && color.green == detected_colors[i].green && color.blue == detected_colors[i].blue) {
						color.red = palette[i].red;
						color.green = palette[i].green;
						color.blue = palette[i].blue;
						break;
					}
				}
			}
			result->SetColorAt(x, y, color);
		}
	}
	return result;
}

void Scene_SkinSelect::DetectColors() {
	detected_colors.clear();
	color_replacements.clear();
	replacement_colors = ReplacementPalette();
	if (!preview_bitmap) return;

	const Rect character = Sprite_Character::GetCharacterRect(
		selected_charset, selected_index, preview_bitmap->GetRect());
	// RPG Maker charset palette index 0 is the transparent background. Some
	// true-colour images keep its alpha opaque, so also identify it by the
	// top-left background pixel instead of relying on alpha alone.
	const Color background = preview_bitmap->GetColorAt(0, 0);
	std::map<uint32_t, int> counts;
	for (int y = character.y; y < character.y + character.height; ++y) {
		for (int x = character.x; x < character.x + character.width; ++x) {
			const Color color = preview_bitmap->GetColorAt(x, y);
			const bool is_background = color.red == background.red &&
				color.green == background.green && color.blue == background.blue;
			if (color.alpha > 16 && !is_background) {
				const uint32_t key = (static_cast<uint32_t>(color.red) << 16) |
					(static_cast<uint32_t>(color.green) << 8) | color.blue;
				++counts[key];
			}
		}
	}

	std::vector<std::pair<uint32_t, int>> sorted(counts.begin(), counts.end());
	std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
	for (size_t i = 0; i < std::min<size_t>(sorted.size(), 12); ++i) {
		const uint32_t key = sorted[i].first;
		detected_colors.emplace_back((key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF, 255);
	}
	color_replacements = detected_colors;
}

BitmapRef Scene_SkinSelect::BuildAdvancedBitmap() const {
	if (!preview_bitmap) return preview_bitmap;
	const Color background = preview_bitmap->GetColorAt(0, 0);
	BitmapRef result = Bitmap::Create(preview_bitmap->GetWidth(), preview_bitmap->GetHeight(), true);
	for (int y = 0; y < preview_bitmap->GetHeight(); ++y) {
		for (int x = 0; x < preview_bitmap->GetWidth(); ++x) {
			Color color = preview_bitmap->GetColorAt(x, y);
			if (!IsBackgroundColor(color, background)) {
				for (size_t i = 0; i < detected_colors.size(); ++i) {
					if (color.red == detected_colors[i].red && color.green == detected_colors[i].green && color.blue == detected_colors[i].blue) {
						color.red = color_replacements[i].red;
						color.green = color_replacements[i].green;
						color.blue = color_replacements[i].blue;
						break;
					}
				}
			}
			result->SetColorAt(x, y, color);
		}
	}
	return result;
}

std::vector<uint8_t> Scene_SkinSelect::EncodeBitmap(const BitmapRef& bitmap) const {
	std::ostringstream stream;
	if (!bitmap || !bitmap->WritePNG(stream, true)) return {};
	const std::string data = stream.str();
	return std::vector<uint8_t>(data.begin(), data.end());
}

} // namespace Chaos
