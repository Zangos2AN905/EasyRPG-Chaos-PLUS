/*
 * Chaos Fork: Skin Selection Scene Implementation
 */

#include "chaos/scene_skin_select.h"
#include "chaos/multiplayer_state.h"
#include "bitmap.h"
#include "cache.h"
#include "filefinder.h"
#include "input.h"
#include "main_data.h"
#include "game_system.h"
#include "output.h"
#include "player.h"
#include "utils.h"
#include <algorithm>
#include <filesystem>

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
		ApplySkin();
		Scene::Pop();
		return;
	}
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
			preview_sprites[0]->SetSrcRect(Rect(preview_anim_frame * cw, 0, cw, ch));
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

			// Source rect: character i, facing down (row 0), current animation frame
			int src_x = (col * CHAR_FRAMES + preview_anim_frame) * cw;
			int src_y = row * CHAR_DIRS * ch; // first direction (down)

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

	// Read the charset file bytes for network transfer
	auto image_data = ReadCharsetFileBytes(charset_entries[idx]);
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

} // namespace Chaos
