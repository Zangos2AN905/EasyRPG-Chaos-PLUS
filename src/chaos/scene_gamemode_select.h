/*
 * Chaos Fork: Game Mode Selection Scene
 * Shown when starting singleplayer, lets user pick Normal/Horror/Undertale.
 */

#ifndef EP_CHAOS_SCENE_GAMEMODE_SELECT_H
#define EP_CHAOS_SCENE_GAMEMODE_SELECT_H

#include "chaos/game_mode.h"
#include "scene.h"
#include "window_command.h"
#include "window_help.h"
#include <memory>
#include <string>
#include <vector>

namespace Chaos {

struct GameModeSelectionEntry {
	std::string name;
	std::string description;
	bool scripted = false;
	GameMode builtin_mode = GameMode::Normal;
	std::string scripted_id;
};

class Scene_GameModeSelect : public Scene {
public:
	Scene_GameModeSelect();

	void Start() override;
	void vUpdate() override;

private:
	void CreateWindows();
	void UpdateHelp();

	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_Command> command_window;
	std::vector<GameModeSelectionEntry> mode_entries;
};

} // namespace Chaos

#endif
