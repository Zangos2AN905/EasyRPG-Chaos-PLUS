/*
 * Chaos Fork: God Mode Menu Scene
 * Allows the god player to control the game via a debug-like menu.
 */

#ifndef EP_CHAOS_SCENE_GOD_MODE_H
#define EP_CHAOS_SCENE_GOD_MODE_H

#include "scene.h"
#include "window_command.h"
#include "window_numberinput.h"
#include "window_help.h"
#include <vector>

namespace Chaos {

class Scene_GodMode : public Scene {
public:
	Scene_GodMode();

	void Start() override;
	void vUpdate() override;

private:
	enum SubMenu {
		eMain,
		eTeleport,
		eSwitches,
		eVariables,
		eGold,
		eItems,
		eStats,
		eFullHeal,
		// Range/detail sub-states
		eRangeList,
		eNumberInput,
	};

	void CreateMainMenu();
	void CreateHelpWindow();

	// Main menu actions
	void UpdateMain();

	// Teleport sub-menu
	void EnterTeleport();
	void UpdateTeleport();
	void EnterTeleportCoords();
	void UpdateTeleportCoords();

	// Switches sub-menu
	void EnterSwitches();
	void UpdateSwitchRange();
	void UpdateSwitchList();

	// Variables sub-menu
	void EnterVariables();
	void UpdateVariableRange();
	void UpdateVariableList();
	void UpdateVariableEdit();

	// Gold sub-menu
	void EnterGold();
	void UpdateGoldInput();

	// Items sub-menu
	void EnterItems();
	void UpdateItemRange();
	void UpdateItemList();
	void UpdateItemAmountInput();

	// Stats sub-menu
	void EnterStats();
	void UpdateStatsActorSelect();
	void UpdateStatsStatSelect();
	void UpdateStatsValueInput();

	// Full Heal
	void DoFullHeal();

	// Utility
	void PopSubMenu();
	void RefreshRangeWindow(int total, int per_page);
	void RefreshDetailWindow();

	// Generic detail/number input handlers for sub-menus
	void UpdateRangeListDetail();
	void UpdateNumberInputDetail();

	// Windows
	std::unique_ptr<Window_Command> main_window;
	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_Command> range_window;
	std::unique_ptr<Window_Command> detail_window;
	std::unique_ptr<Window_NumberInput> number_window;

	SubMenu current_menu = eMain;
	SubMenu parent_menu = eMain;

	// Tracking state for navigating ranges
	int range_page = 0;
	int selected_id = 0;         // Generic selected ID (switch/var/item/map)
	int selected_actor_id = 0;
	int selected_stat = 0;       // For stats menu: 0=Level, 1=HP, 2=SP
	int teleport_map_id = 0;
	int teleport_step = 0;       // 0=entering map, 1=entering X, 2=entering Y
	int teleport_x = 0;
	int teleport_y = 0;
};

} // namespace Chaos

#endif
