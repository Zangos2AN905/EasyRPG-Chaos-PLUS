/*
 * Chaos Fork: Multiplayer Menu Scene
 * Shown after selecting a game from the browser.
 * Offers choice between Singleplayer and Multiplayer.
 */

#ifndef EP_CHAOS_SCENE_MULTIPLAYER_MENU_H
#define EP_CHAOS_SCENE_MULTIPLAYER_MENU_H

#include "scene.h"
#include "window_command.h"
#include "window_help.h"
#include <memory>

namespace Chaos {

class Scene_MultiplayerMenu : public Scene {
public:
	Scene_MultiplayerMenu();

	void Start() override;
	void vUpdate() override;

private:
	void CreateWindows();
	void OnSingleplayer();
	void OnMultiplayer();

	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_Command> command_window;
};

} // namespace Chaos

#endif
