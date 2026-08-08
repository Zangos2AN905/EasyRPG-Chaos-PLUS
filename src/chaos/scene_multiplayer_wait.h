/*
 * Chaos Fork: Multiplayer Wait Scene
 * Shown to clients while the host picks a save/new game.
 */

#ifndef EP_CHAOS_SCENE_MULTIPLAYER_WAIT_H
#define EP_CHAOS_SCENE_MULTIPLAYER_WAIT_H

#include "scene.h"
#include "window_help.h"
#include <memory>
#include <vector>
#include "chaos/net_manager.h"

namespace Chaos {

class Scene_MultiplayerWait : public Scene {
public:
	Scene_MultiplayerWait();

	void Start() override;
	void vUpdate() override;

private:
	// Applies the host's snapshot and enters the map once it is loaded.
	void UpdateBootstrap(NetManager& net);

	std::unique_ptr<Window_Help> status_window;
	int timer = 0;

	// Late-join bootstrap state (one-shot).
	bool bootstrap_started = false;
	bool bootstrap_data_captured = false;
	bool bootstrap_objects_reset = false;
	bool bootstrap_complete = false;
	int bootstrap_wait_frames = 0;

	// Snapshot data captured from the network manager at bootstrap time.
	int bootstrap_map_id = 0;
	int bootstrap_map_x = 0;
	int bootstrap_map_y = 0;
	std::vector<HostPartyMember> bootstrap_host_party;
	std::vector<bool> bootstrap_host_switches;
	std::vector<int32_t> bootstrap_host_variables;
};

} // namespace Chaos

#endif
