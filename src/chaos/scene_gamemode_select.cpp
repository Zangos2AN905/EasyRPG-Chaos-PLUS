/*
 * Chaos Fork: Game Mode Selection Scene Implementation
 */

#include "chaos/scene_gamemode_select.h"
#include "chaos/game_mode.h"
#include "chaos/mod_api.h"
#include "chaos/mod_loader.h"
#include "input.h"
#include "player.h"
#include "game_system.h"
#include "game_clock.h"
#include "main_data.h"
#include "scene_logo.h"
#include "scene_title.h"
#include "scene.h"

namespace Chaos {

Scene_GameModeSelect::Scene_GameModeSelect() {
	type = Scene::GameModeSelect;
}

void Scene_GameModeSelect::Start() {
	ModLoader::Instance().EnsureLoaded();
	CreateWindows();
	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_GameModeSelect::CreateWindows() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);

	mode_entries.clear();
	std::vector<std::string> options;
	for (int i = 0; i < static_cast<int>(GameMode::Count); i++) {
		auto& info = GetGameModeInfo(static_cast<GameMode>(i));
		mode_entries.push_back({info.name, info.description, false, static_cast<GameMode>(i), {}});
		options.push_back(info.name);
	}

	for (const auto* gamemode : ModRegistry::Instance().GetAllGamemodes()) {
		if (!gamemode) {
			continue;
		}
		if (gamemode->id == "chaos_plus" || gamemode->name == "Chaos+") {
			continue;
		}

		const auto name = gamemode->name.empty() ? gamemode->id : gamemode->name;
		const auto description = gamemode->description.empty() ? "Scripted gamemode provided by a mod." : gamemode->description;
		mode_entries.push_back({name, description, true, GameMode::Normal, gamemode->id});
		options.push_back(name);
	}

	command_window = std::make_unique<Window_Command>(options, Player::screen_width / 2);
	command_window->SetX(Player::screen_width / 4);
	command_window->SetY(Player::screen_height / 2 - command_window->GetHeight() / 2);

	UpdateHelp();
}

void Scene_GameModeSelect::UpdateHelp() {
	int idx = command_window->GetIndex();
	if (idx >= 0 && idx < static_cast<int>(mode_entries.size())) {
		help_window->SetText(mode_entries[idx].description);
	}
}

void Scene_GameModeSelect::vUpdate() {
	command_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	// Update description when selection changes
	UpdateHelp();

	if (Input::IsTriggered(Input::DECISION)) {
		int idx = command_window->GetIndex();
		if (idx >= 0 && idx < static_cast<int>(mode_entries.size())) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));

			const auto& entry = mode_entries[idx];
			if (entry.scripted) {
				SetCurrentScriptedGamemode(entry.scripted_id);
			} else {
				SetCurrentGameMode(entry.builtin_mode);
			}

			// Proceed to logo/title scene
			auto logos = Scene_Logo::LoadLogos();
			if (!logos.empty()) {
				Scene::Push(std::make_shared<Scene_Logo>(std::move(logos), 1), true);
			} else {
				Scene::Push(std::make_shared<Scene_Title>(), true);
			}
		}
	}
}

} // namespace Chaos
