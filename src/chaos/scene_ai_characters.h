/*
 * This file is part of EasyRPG Player (Chaos Fork).
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EP_CHAOS_SCENE_AI_CHARACTERS_H
#define EP_CHAOS_SCENE_AI_CHARACTERS_H

#include "../scene.h"
#include "../window_command.h"
#include "../window_help.h"

/**
 * Scene_AICharacters class.
 * Character management screen for Chaos AI Characters.
 *
 * Controls:
 *   1 - Create a new character
 *   2 - Delete the selected character
 *   3 - Toggle (enable/disable) the selected character
 *   Cancel - Return to settings
 */
class Scene_AICharacters : public Scene {
public:
	Scene_AICharacters();

	void Start() override;
	void vUpdate() override;

private:
	/** Rebuild the command window from the current character list */
	void RefreshList();

	/** Title/instruction window at the top */
	std::unique_ptr<Window_Help> title_window;

	/** Hint window at the bottom showing keybinds */
	std::unique_ptr<Window_Help> hint_window;

	/** Character list window */
	std::unique_ptr<Window_Command> list_window;
};

#endif
