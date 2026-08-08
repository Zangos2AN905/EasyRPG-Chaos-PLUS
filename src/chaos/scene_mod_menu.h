/*
 * Chaos Fork: Mod Menu Scene
 * Accessible from the game browser's top command bar.
 * Shows loaded mods, allows enabling/disabling, and displays mod info.
 */

#ifndef EP_CHAOS_SCENE_MOD_MENU_H
#define EP_CHAOS_SCENE_MOD_MENU_H

#include "scene.h"
#include "window_help.h"
#include "window_selectable.h"
#include "chaos/mod_api.h"

#include <memory>
#include <vector>
#include <string>

namespace Chaos {

/**
 * Window that displays a list of discovered mods.
 */
class Window_ModList : public Window_Selectable {
public:
	Window_ModList(int x, int y, int w, int h);

	/** Refresh mod entries from the mods/ directory. */
	void Refresh();

	void DrawItem(int index);

	/** Get info about the selected mod entry. Returns null if none. */
	struct ModEntry {
		std::string folder_name;
		std::string display_name;
		std::string language;    // "AngelScript" or "Lua"
		std::string author;
		std::string version;
		std::string description;
		bool loaded = false;
	};

	const ModEntry* GetEntry(int index) const;
	int GetEntryCount() const { return static_cast<int>(entries.size()); }

private:
	std::vector<ModEntry> entries;
};

/**
 * Scene displaying the mod menu.
 */
class Scene_ModMenu : public Scene {
public:
	Scene_ModMenu();

	void Start() override;
	void vUpdate() override;

private:
	void CreateWindows();
	void UpdateModInfo();

	std::unique_ptr<Window_Help> title_window;
	std::unique_ptr<Window_ModList> mod_list_window;
	std::unique_ptr<Window_Help> info_window;

	int last_index = -1;
};

} // namespace Chaos

#endif // EP_CHAOS_SCENE_MOD_MENU_H
