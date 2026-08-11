/*
 * Chaos Fork: Custom Mode Setup Scene
 * UI for configuring a custom multiplayer mode.
 */

#ifndef EP_CHAOS_SCENE_CUSTOM_MODE_SETUP_H
#define EP_CHAOS_SCENE_CUSTOM_MODE_SETUP_H

#include "scene.h"
#include "window_command.h"
#include "window_help.h"
#include "chaos/custom_mode.h"
#include <memory>
#include <string>
#include <vector>

namespace Chaos {

class Scene_CustomModeSetup : public Scene {
public:
	explicit Scene_CustomModeSetup(bool relay_hosting);

	void Start() override;
	void vUpdate() override;

private:
	enum class ItemIndex {
		Mode1,
		Mode2,
		Objective,
		RandomEvents,
		ForceSkin,
		TurboMovement,
		RandomStart,
		StartGame,
		Count
	};

	void CreateWindows();
	void RefreshList();
	void CycleMode(int item, int direction);
	void CycleObjective(int direction);
	void ToggleBool(int item);
	void ApplySkinToSettings();
	void StartCustomHosting();

	bool relay_hosting;
	std::unique_ptr<Window_Command> list_window;
	std::unique_ptr<Window_Help> help_window;

	CustomModeSettings settings;
};

} // namespace Chaos

#endif