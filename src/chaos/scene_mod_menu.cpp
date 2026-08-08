/*
 * Chaos Fork: Mod Menu Scene implementation
 */

#include "chaos/scene_mod_menu.h"
#include "chaos/mod_loader.h"
#include "input.h"
#include "player.h"
#include "game_system.h"
#include "main_data.h"
#include "game_clock.h"
#include "bitmap.h"
#include "font.h"
#include "output.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace Chaos {

// ============================================================
// Window_ModList
// ============================================================

Window_ModList::Window_ModList(int x, int y, int w, int h)
	: Window_Selectable(x, y, w, h)
{
	column_max = 1;
}

void Window_ModList::Refresh() {
	entries.clear();

	// Scan mods/ directory for mod folders
	const fs::path mods_dir = "mods";

	std::error_code ec;
	if (fs::exists(mods_dir, ec) && fs::is_directory(mods_dir, ec)) {
		for (const auto& dir_entry : fs::directory_iterator(mods_dir, ec)) {
			if (!dir_entry.is_directory(ec)) continue;

			std::string folder = dir_entry.path().filename().string();
			fs::path mod_path = dir_entry.path();

			ModEntry entry;
			entry.folder_name = folder;
			entry.display_name = folder;

			// Detect language
			if (fs::exists(mod_path / "mod.as", ec)) {
				entry.language = "AngelScript";
			} else if (fs::exists(mod_path / "mod.lua", ec)) {
				entry.language = "Lua";
			} else {
				continue; // Not a valid mod
			}

			// Check if loaded in registry
			auto* mod_info = ModRegistry::Instance().FindMod(folder);
			if (mod_info) {
				entry.loaded = mod_info->enabled;
				if (!mod_info->name.empty()) entry.display_name = mod_info->name;
				entry.author = mod_info->author;
				entry.version = mod_info->version;
				entry.description = mod_info->description;
			}

			// Try parsing mod.json for display info if not loaded
			if (!entry.loaded) {
				fs::path json_path = mod_path / "mod.json";
				if (fs::exists(json_path, ec)) {
					std::ifstream file(json_path.string());
					if (file.is_open()) {
						std::stringstream buf;
						buf << file.rdbuf();
						std::string json = buf.str();

						auto extract = [&](const std::string& key) -> std::string {
							std::string search = "\"" + key + "\"";
							auto pos = json.find(search);
							if (pos == std::string::npos) return {};
							pos = json.find(':', pos + search.size());
							if (pos == std::string::npos) return {};
							pos = json.find('"', pos + 1);
							if (pos == std::string::npos) return {};
							auto end = json.find('"', pos + 1);
							if (end == std::string::npos) return {};
							return json.substr(pos + 1, end - pos - 1);
						};

						auto val = extract("name");
						if (!val.empty()) entry.display_name = val;
						val = extract("author");
						if (!val.empty()) entry.author = val;
						val = extract("version");
						if (!val.empty()) entry.version = val;
						val = extract("description");
						if (!val.empty()) entry.description = val;
					}
				}
			}

			entries.push_back(std::move(entry));
		}
	}

	// Sort alphabetically
	std::sort(entries.begin(), entries.end(),
		[](const ModEntry& a, const ModEntry& b) {
			return a.display_name < b.display_name;
		});

	if (entries.empty()) {
		item_max = 1;
		SetContents(Bitmap::Create(width - 16, height - 16));
		contents->Clear();
		contents->TextDraw(0, 0, Font::ColorDefault, "No mods found in mods/ directory");
	} else {
		item_max = static_cast<int>(entries.size());
		CreateContents();
		contents->Clear();
		for (int i = 0; i < item_max; ++i) {
			DrawItem(i);
		}
	}
}

void Window_ModList::DrawItem(int index) {
	if (index < 0 || index >= static_cast<int>(entries.size())) return;

	Rect rect = GetItemRect(index);
	contents->ClearRect(rect);

	auto& e = entries[index];

	// Color based on state
	auto color = e.loaded ? Font::ColorDefault : Font::ColorDisabled;

	// Draw mod name
	std::string label = e.display_name;
	contents->TextDraw(rect.x, rect.y, color, label);

	// Draw language tag on the right
	std::string tag = "[" + e.language + "]";
	if (e.loaded) tag += " ON";
	contents->TextDraw(rect.x + rect.width, rect.y, color, tag, Text::AlignRight);
}

const Window_ModList::ModEntry* Window_ModList::GetEntry(int index) const {
	if (index < 0 || index >= static_cast<int>(entries.size())) return nullptr;
	return &entries[index];
}

// ============================================================
// Scene_ModMenu
// ============================================================

Scene_ModMenu::Scene_ModMenu() {
	type = Scene::ModMenu;
}

void Scene_ModMenu::Start() {
	// Load mods if not already loaded
	ModLoader::Instance().DiscoverAndLoadAll();

	CreateWindows();
	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_ModMenu::CreateWindows() {
	title_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	title_window->SetText("Mod Manager");

	int list_h = Player::screen_height - 32 - 64; // title + info area
	mod_list_window = std::make_unique<Window_ModList>(0, 32, Player::screen_width, list_h);
	mod_list_window->Refresh();
	mod_list_window->SetActive(true);
	if (mod_list_window->GetEntryCount() > 0) {
		mod_list_window->SetIndex(0);
	}

	info_window = std::make_unique<Window_Help>(0, 32 + list_h, Player::screen_width, 64);
	info_window->SetText("");

	UpdateModInfo();
}

void Scene_ModMenu::vUpdate() {
	mod_list_window->Update();

	// Update info panel when selection changes
	if (mod_list_window->GetIndex() != last_index) {
		UpdateModInfo();
		last_index = mod_list_window->GetIndex();
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		auto* entry = mod_list_window->GetEntry(mod_list_window->GetIndex());
		if (entry) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			// Toggle would go here in a future update
			// For now, just play a sound to acknowledge
		}
	}
}

void Scene_ModMenu::UpdateModInfo() {
	auto* entry = mod_list_window->GetEntry(mod_list_window->GetIndex());
	if (!entry) {
		info_window->SetText("No mods available. Place mods in the mods/ folder.");
		return;
	}

	std::string info;

	// Line 1: Name, version, language
	info = entry->display_name;
	if (!entry->version.empty()) {
		info += " v" + entry->version;
	}
	info += " (" + entry->language + ")";

	// Line 2: Author + description
	if (!entry->author.empty()) {
		info += " by " + entry->author;
	}
	if (!entry->description.empty()) {
		info += " - " + entry->description;
	}

	info_window->SetText(info);
}

} // namespace Chaos
