/*
 * Chaos Fork: Multiplayer radio scene.
 */

#ifndef EP_CHAOS_SCENE_RADIO_H
#define EP_CHAOS_SCENE_RADIO_H

#include "scene.h"
#include "window_help.h"
#include "window_command.h"
#include <memory>

namespace Chaos {

class Scene_Radio : public Scene {
public:
	explicit Scene_Radio(bool boombox = false);

	void Start() override;
	void vUpdate() override;

private:
	void CreateWindows();
	void RefreshList();
	void RefreshQueue();
	bool SelectCustomMusicFile(std::string& path) const;

	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_Help> queue_window;
	std::unique_ptr<Window_Command> list_window;
	bool boombox = false;
};

} // namespace Chaos

#endif
