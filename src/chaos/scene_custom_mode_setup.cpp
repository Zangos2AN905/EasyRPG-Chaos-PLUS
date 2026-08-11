/*
 * Chaos Fork: Custom Mode Setup Scene
 */

#include "chaos/scene_custom_mode_setup.h"
#include "chaos/custom_mode.h"
#include "chaos/multiplayer_mode.h"
#include "chaos/multiplayer_state.h"
#include "game_system.h"
#include "input.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "scene.h"
#include <fmt/format.h>

namespace Chaos {

Scene_CustomModeSetup::Scene_CustomModeSetup(bool relay_hosting)
	: relay_hosting(relay_hosting) {
	type = Scene::CustomModeSetup;
}

void Scene_CustomModeSetup::Start() {
	CreateWindows();
	RefreshList();
}

void Scene_CustomModeSetup::CreateWindows() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	help_window->SetText("Configure your custom mode");

	std::vector<std::string> options;
	options.resize(static_cast<int>(ItemIndex::Count));
	list_window = std::make_unique<Window_Command>(options, Player::screen_width / 2);
	list_window->SetX(Player::screen_width / 4);
	list_window->SetY(48);
	list_window->SetHeight(static_cast<int>(options.size()) * 16 + 32);
}

void Scene_CustomModeSetup::RefreshList() {
	auto set = [this](int idx, const std::string& text) {
		list_window->SetItemText(idx, text);
	};

	set(static_cast<int>(ItemIndex::Mode1),
		fmt::format("Mode 1: {}", GetModeProperties(static_cast<MultiplayerMode>(settings.mode_a)).name));
	set(static_cast<int>(ItemIndex::Mode2),
		fmt::format("Mode 2: {}", GetModeProperties(static_cast<MultiplayerMode>(settings.mode_b)).name));
	set(static_cast<int>(ItemIndex::Objective),
		fmt::format("Objective: {}", GetObjectiveName(static_cast<CustomObjective>(settings.objective))));
	set(static_cast<int>(ItemIndex::RandomEvents),
		fmt::format("Random Events: {}", settings.random_events ? "On" : "Off"));
	set(static_cast<int>(ItemIndex::ForceSkin),
		fmt::format("Force Skin: {}", settings.force_skin ? "On" : "Off"));
	set(static_cast<int>(ItemIndex::TurboMovement),
		fmt::format("Turbo Movement: {}", settings.turbo_movement ? "On" : "Off"));
	set(static_cast<int>(ItemIndex::RandomStart),
		fmt::format("Random Start: {}", settings.random_start ? "On" : "Off"));
	set(static_cast<int>(ItemIndex::StartGame), "Host Game!");
}

void Scene_CustomModeSetup::vUpdate() {
	list_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	int idx = list_window->GetIndex();

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		switch (static_cast<ItemIndex>(idx)) {
			case ItemIndex::Mode1:
				CycleMode(idx, +1);
				break;
			case ItemIndex::Mode2:
				CycleMode(idx, +1);
				break;
			case ItemIndex::Objective:
				CycleObjective(+1);
				break;
			case ItemIndex::RandomEvents:
				ToggleBool(idx);
				break;
			case ItemIndex::ForceSkin:
				ToggleBool(idx);
				break;
			case ItemIndex::TurboMovement:
				ToggleBool(idx);
				break;
			case ItemIndex::RandomStart:
				ToggleBool(idx);
				break;
			case ItemIndex::StartGame:
				StartCustomHosting();
				return;
			default:
				break;
		}
		RefreshList();
	}

	if (Input::IsTriggered(Input::LEFT) || Input::IsTriggered(Input::RIGHT)) {
		switch (static_cast<ItemIndex>(idx)) {
			case ItemIndex::Mode1:
				CycleMode(idx, Input::IsTriggered(Input::RIGHT) ? +1 : -1);
				RefreshList();
				break;
			case ItemIndex::Mode2:
				CycleMode(idx, Input::IsTriggered(Input::RIGHT) ? +1 : -1);
				RefreshList();
				break;
			case ItemIndex::Objective:
				CycleObjective(Input::IsTriggered(Input::RIGHT) ? +1 : -1);
				RefreshList();
				break;
			default:
				break;
		}
	}

	switch (static_cast<ItemIndex>(idx)) {
		case ItemIndex::Mode1:
		case ItemIndex::Mode2:
			help_window->SetText("Pick a base mode to mix. Both modes' sync settings combine.");
			break;
		case ItemIndex::Objective:
			help_window->SetText("Choose the win condition for this session.");
			break;
		case ItemIndex::RandomEvents:
			help_window->SetText("Random chaos events fire every 5-10 seconds.");
			break;
		case ItemIndex::ForceSkin:
			help_window->SetText("Force every player to use your selected skin.");
			break;
		case ItemIndex::TurboMovement:
			help_window->SetText("Everyone moves one speed tier faster.");
			break;
		case ItemIndex::RandomStart:
			help_window->SetText("All players start at a random spot on the map.");
			break;
		case ItemIndex::StartGame:
			help_window->SetText("Start hosting with these settings.");
			break;
		default:
			break;
	}
}

void Scene_CustomModeSetup::CycleMode(int item, int direction) {
	uint8_t& target = (item == static_cast<int>(ItemIndex::Mode1)) ? settings.mode_a : settings.mode_b;
	int count = GetModeCount();
	// Exclude Custom itself from the mix to avoid recursion
	int next = target + direction;
	while (next >= count || next == static_cast<int>(MultiplayerMode::Custom)) {
		if (next >= count) next = 0;
		else next++;
	}
	while (next < 0 || next == static_cast<int>(MultiplayerMode::Custom)) {
		if (next < 0) next = count - 1;
		else next--;
	}
	target = next;
}

void Scene_CustomModeSetup::CycleObjective(int direction) {
	int count = static_cast<int>(CustomObjective::Count);
	int next = static_cast<int>(settings.objective) + direction;
	if (next >= count) next = 0;
	if (next < 0) next = count - 1;
	settings.objective = next;
}

void Scene_CustomModeSetup::ToggleBool(int item) {
	switch (static_cast<ItemIndex>(item)) {
		case ItemIndex::RandomEvents:
			settings.random_events = !settings.random_events;
			break;
		case ItemIndex::ForceSkin:
			settings.force_skin = !settings.force_skin;
			break;
		case ItemIndex::TurboMovement:
			settings.turbo_movement = !settings.turbo_movement;
			break;
		case ItemIndex::RandomStart:
			settings.random_start = !settings.random_start;
			break;
		default:
			break;
	}
}

void Scene_CustomModeSetup::ApplySkinToSettings() {
	auto& mp = MultiplayerState::Instance();
	if (mp.HasSkin()) {
		settings.force_skin_charset = mp.GetSkinCharsetName();
		settings.force_skin_index = mp.GetSkinCharIndex();
		settings.force_skin_data = mp.GetSkinImageData();
	} else {
		settings.force_skin_charset.clear();
		settings.force_skin_index = 0;
		settings.force_skin_data.clear();
	}
}

void Scene_CustomModeSetup::StartCustomHosting() {
	ApplySkinToSettings();
	settings.host_via_relay = relay_hosting;
	CustomMode::Instance().SetPendingSettings(settings);
	Output::Debug("CustomMode: Pending settings stored (mix {}+{})", settings.mode_a, settings.mode_b);
	Scene::Pop();
}

} // namespace Chaos
