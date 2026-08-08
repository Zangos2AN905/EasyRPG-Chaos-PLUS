/*
 * Chaos Fork: Multiplayer Menu Scene Implementation
 */

#include "chaos/scene_multiplayer_menu.h"
#include "chaos/scene_multiplayer_lobby.h"
#include "chaos/scene_gamemode_select.h"
#include "chaos/mod_loader.h"
#include "input.h"
#include "player.h"
#include "scene.h"
#include "game_system.h"
#include "main_data.h"
#include "game_clock.h"

namespace Chaos {

Scene_MultiplayerMenu::Scene_MultiplayerMenu() {
	type = Scene::MultiplayerMenu;
}

void Scene_MultiplayerMenu::Start() {
	ModLoader::Instance().EnsureLoaded();
	CreateWindows();
	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_MultiplayerMenu::CreateWindows() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	help_window->SetText("Select play mode");

	std::vector<std::string> options;
	options.push_back("Singleplayer");
	options.push_back("Multiplayer");

	command_window = std::make_unique<Window_Command>(options, Player::screen_width / 2);
	command_window->SetX(Player::screen_width / 4);
	command_window->SetY(Player::screen_height / 2 - command_window->GetHeight() / 2);
}

void Scene_MultiplayerMenu::vUpdate() {
	command_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		switch (command_window->GetIndex()) {
			case 0:
				OnSingleplayer();
				break;
			case 1:
				OnMultiplayer();
				break;
		}
	}
}

void Scene_MultiplayerMenu::OnSingleplayer() {
	// Show game mode selection before proceeding to the game
	Scene::Push(std::make_shared<Scene_GameModeSelect>());
}

void Scene_MultiplayerMenu::OnMultiplayer() {
	Scene::Push(std::make_shared<Scene_MultiplayerLobby>());
}

} // namespace Chaos
