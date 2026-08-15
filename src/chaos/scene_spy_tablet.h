#ifndef EP_CHAOS_SCENE_SPY_TABLET_H
#define EP_CHAOS_SCENE_SPY_TABLET_H

#include "scene.h"
#include "window_command.h"
#include "window_help.h"
#include <memory>
#include <vector>

namespace Chaos {

class Scene_SpyTablet : public Scene {
public:
	Scene_SpyTablet();
	void Start() override;
	void vUpdate() override;

private:
	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_Command> player_window;
	std::vector<uint16_t> player_ids;
};

} // namespace Chaos

#endif
