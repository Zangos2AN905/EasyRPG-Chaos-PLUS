/*
 * Chaos Fork: God Mode Menu Scene Implementation
 */

#include "chaos/scene_god_mode.h"
#include "chaos/multiplayer_state.h"
#include "chaos/net_manager.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "game_switches.h"
#include "game_variables.h"
#include "game_actors.h"
#include "game_actor.h"
#include "game_system.h"
#include "input.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "options.h"
#include <lcf/data.h>
#include <lcf/reader_util.h>
#include <fmt/format.h>

namespace Chaos {

Scene_GodMode::Scene_GodMode() {
	Scene::type = Scene::Debug; // Reuse Debug scene type
}

void Scene_GodMode::Start() {
	CreateHelpWindow();
	CreateMainMenu();
}

void Scene_GodMode::CreateMainMenu() {
	std::vector<std::string> options = {
		"Teleport",
		"Switches",
		"Variables",
		"Gold",
		"Items",
		"Stats",
		"Full Heal"
	};

	main_window.reset(new Window_Command(options, 160));
	main_window->SetX(Player::menu_offset_x + 0);
	main_window->SetY(Player::menu_offset_y + 32);
}

void Scene_GodMode::CreateHelpWindow() {
	help_window.reset(new Window_Help(
		Player::menu_offset_x, Player::menu_offset_y,
		MENU_WIDTH, 32));
	help_window->SetText("God Mode - Control the game");
}

void Scene_GodMode::vUpdate() {
	if (main_window) main_window->Update();
	if (range_window) range_window->Update();
	if (detail_window) detail_window->Update();
	if (number_window) number_window->Update();

	switch (current_menu) {
		case eMain: UpdateMain(); break;
		case eTeleport: UpdateTeleport(); break;
		case eSwitches: UpdateSwitchRange(); break;
		case eVariables: UpdateVariableRange(); break;
		case eGold: UpdateGoldInput(); break;
		case eItems: UpdateItemRange(); break;
		case eStats: UpdateStatsActorSelect(); break;
		case eRangeList: UpdateRangeListDetail(); break;
		case eNumberInput: UpdateNumberInputDetail(); break;
		default: break;
	}
}

void Scene_GodMode::UpdateMain() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		switch (main_window->GetIndex()) {
			case 0: EnterTeleport(); break;
			case 1: EnterSwitches(); break;
			case 2: EnterVariables(); break;
			case 3: EnterGold(); break;
			case 4: EnterItems(); break;
			case 5: EnterStats(); break;
			case 6: DoFullHeal(); break;
		}
	}
}

// ========== Teleport ==========

void Scene_GodMode::EnterTeleport() {
	current_menu = eTeleport;
	teleport_step = 0;
	main_window->SetActive(false);

	number_window.reset(new Window_NumberInput(
		Player::menu_offset_x + 40, Player::menu_offset_y + 80, 240, 48));
	number_window->SetMaxDigits(4);
	number_window->SetNumber(Game_Map::GetMapId());
	number_window->SetVisible(true);
	number_window->SetActive(true);
	number_window->Refresh();

	help_window->SetText("Teleport: Enter Map ID");
}

void Scene_GodMode::UpdateTeleport() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		if (teleport_step == 0) {
			PopSubMenu();
		} else {
			teleport_step--;
			if (teleport_step == 0) {
				help_window->SetText("Teleport: Enter Map ID");
				number_window->SetNumber(teleport_map_id);
			} else if (teleport_step == 1) {
				help_window->SetText(fmt::format("Map {}: Enter X", teleport_map_id));
				number_window->SetNumber(teleport_x);
			}
			number_window->Refresh();
		}
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		int val = number_window->GetNumber();
		if (teleport_step == 0) {
			teleport_map_id = val;
			teleport_step = 1;
			help_window->SetText(fmt::format("Map {}: Enter X", teleport_map_id));
			number_window->SetNumber(0);
			number_window->Refresh();
		} else if (teleport_step == 1) {
			teleport_x = val;
			teleport_step = 2;
			help_window->SetText(fmt::format("Map {} ({},?): Enter Y", teleport_map_id, teleport_x));
			number_window->SetNumber(0);
			number_window->Refresh();
		} else {
			teleport_y = val;
			// Send teleport command (cmd_type 2)
			auto& mp = MultiplayerState::Instance();
			mp.SendGodCommand(2, {teleport_map_id, teleport_x, teleport_y});
			// Also apply locally
			if (Main_Data::game_player) {
				Main_Data::game_player->ReserveTeleport(teleport_map_id, teleport_x, teleport_y, -1, TeleportTarget::eParallelTeleport);
			}
			help_window->SetText(fmt::format("Teleported to Map {} ({},{})", teleport_map_id, teleport_x, teleport_y));
			PopSubMenu();
		}
	}
}

// ========== Switches ==========

void Scene_GodMode::EnterSwitches() {
	current_menu = eSwitches;
	parent_menu = eSwitches;
	range_page = 0;
	main_window->SetActive(false);

	int total = Main_Data::game_switches ? Main_Data::game_switches->GetSizeWithLimit() : 0;
	int pages = (total + 9) / 10;
	if (pages < 1) pages = 1;

	std::vector<std::string> items;
	for (int i = 0; i < std::min(pages, 20); ++i) {
		int start = i * 10 + 1;
		int end = std::min(start + 9, total);
		items.push_back(fmt::format("{:04d}-{:04d}", start, end));
	}

	range_window.reset(new Window_Command(items, 120, 10));
	range_window->SetX(Player::menu_offset_x + 160);
	range_window->SetY(Player::menu_offset_y + 32);
	range_window->SetActive(true);

	help_window->SetText("Switches: Select range");
}

void Scene_GodMode::UpdateSwitchRange() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		PopSubMenu();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		range_page = range_window->GetIndex();
		current_menu = eRangeList;
		range_window->SetActive(false);

		int base = range_page * 10 + 1;
		int total = Main_Data::game_switches ? Main_Data::game_switches->GetSizeWithLimit() : 0;

		std::vector<std::string> items;
		for (int i = 0; i < 10 && (base + i) <= total; ++i) {
			int sw_id = base + i;
			bool val = Main_Data::game_switches->Get(sw_id);
			items.push_back(fmt::format("[{}] {:04d}", val ? "ON " : "OFF", sw_id));
		}

		detail_window.reset(new Window_Command(items, 200, 10));
		detail_window->SetX(Player::menu_offset_x + 120);
		detail_window->SetY(Player::menu_offset_y + 32);
		detail_window->SetActive(true);

		help_window->SetText("Switches: Press Enter to toggle");
	}
}

void Scene_GodMode::UpdateSwitchList() {
	// This is called from vUpdate when current_menu == eRangeList and parent_menu == eSwitches
}

// Handle range list updates in vUpdate
// We override vUpdate's eRangeList handling below in a helper

void Scene_GodMode::PopSubMenu() {
	range_window.reset();
	detail_window.reset();
	number_window.reset();
	current_menu = eMain;
	main_window->SetActive(true);
	help_window->SetText("God Mode - Control the game");
}

// ========== Variables ==========

void Scene_GodMode::EnterVariables() {
	current_menu = eVariables;
	parent_menu = eVariables;
	range_page = 0;
	main_window->SetActive(false);

	int total = Main_Data::game_variables ? Main_Data::game_variables->GetSizeWithLimit() : 0;
	int pages = (total + 9) / 10;
	if (pages < 1) pages = 1;

	std::vector<std::string> items;
	for (int i = 0; i < std::min(pages, 20); ++i) {
		int start = i * 10 + 1;
		int end = std::min(start + 9, total);
		items.push_back(fmt::format("{:04d}-{:04d}", start, end));
	}

	range_window.reset(new Window_Command(items, 120, 10));
	range_window->SetX(Player::menu_offset_x + 160);
	range_window->SetY(Player::menu_offset_y + 32);
	range_window->SetActive(true);

	help_window->SetText("Variables: Select range");
}

void Scene_GodMode::UpdateVariableRange() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		PopSubMenu();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		range_page = range_window->GetIndex();
		current_menu = eRangeList;
		parent_menu = eVariables;
		range_window->SetActive(false);

		int base = range_page * 10 + 1;
		int total = Main_Data::game_variables ? Main_Data::game_variables->GetSizeWithLimit() : 0;

		std::vector<std::string> items;
		for (int i = 0; i < 10 && (base + i) <= total; ++i) {
			int var_id = base + i;
			int32_t val = Main_Data::game_variables->Get(var_id);
			items.push_back(fmt::format("{:04d}={}", var_id, val));
		}

		detail_window.reset(new Window_Command(items, 200, 10));
		detail_window->SetX(Player::menu_offset_x + 120);
		detail_window->SetY(Player::menu_offset_y + 32);
		detail_window->SetActive(true);

		help_window->SetText("Variables: Select to edit");
	}
}

void Scene_GodMode::UpdateVariableList() {
	// Handled inline in vUpdate for eRangeList
}

void Scene_GodMode::UpdateVariableEdit() {
	// Handled inline in vUpdate for eNumberInput
}

// ========== Gold ==========

void Scene_GodMode::EnterGold() {
	current_menu = eGold;
	main_window->SetActive(false);

	int current_gold = Main_Data::game_party ? Main_Data::game_party->GetGold() : 0;

	number_window.reset(new Window_NumberInput(
		Player::menu_offset_x + 40, Player::menu_offset_y + 80, 240, 48));
	number_window->SetMaxDigits(6);
	number_window->SetNumber(current_gold);
	number_window->SetShowOperator(false);
	number_window->SetVisible(true);
	number_window->SetActive(true);
	number_window->Refresh();

	help_window->SetText(fmt::format("Gold (current: {}): Set new amount", current_gold));
}

void Scene_GodMode::UpdateGoldInput() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		PopSubMenu();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		int new_gold = number_window->GetNumber();
		int current_gold = Main_Data::game_party ? Main_Data::game_party->GetGold() : 0;
		int diff = new_gold - current_gold;

		// Send god command (cmd_type 3 = change gold)
		auto& mp = MultiplayerState::Instance();
		mp.SendGodCommand(3, {diff});

		// Apply locally
		if (Main_Data::game_party) {
			if (diff >= 0) {
				Main_Data::game_party->GainGold(diff);
			} else {
				Main_Data::game_party->LoseGold(-diff);
			}
		}

		help_window->SetText(fmt::format("Gold set to {}", new_gold));
		PopSubMenu();
	}
}

// ========== Items ==========

void Scene_GodMode::EnterItems() {
	current_menu = eItems;
	parent_menu = eItems;
	range_page = 0;
	main_window->SetActive(false);

	int total = static_cast<int>(lcf::Data::items.size());
	int pages = (total + 9) / 10;
	if (pages < 1) pages = 1;

	std::vector<std::string> items;
	for (int i = 0; i < std::min(pages, 20); ++i) {
		int start = i * 10 + 1;
		int end = std::min(start + 9, total);
		items.push_back(fmt::format("{:04d}-{:04d}", start, end));
	}

	range_window.reset(new Window_Command(items, 120, 10));
	range_window->SetX(Player::menu_offset_x + 160);
	range_window->SetY(Player::menu_offset_y + 32);
	range_window->SetActive(true);

	help_window->SetText("Items: Select range");
}

void Scene_GodMode::UpdateItemRange() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		PopSubMenu();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		range_page = range_window->GetIndex();
		current_menu = eRangeList;
		parent_menu = eItems;
		range_window->SetActive(false);

		int base = range_page * 10 + 1;
		int total = static_cast<int>(lcf::Data::items.size());

		std::vector<std::string> items;
		for (int i = 0; i < 10 && (base + i) <= total; ++i) {
			int item_id = base + i;
			auto* item = lcf::ReaderUtil::GetElement(lcf::Data::items, item_id);
			std::string name = item ? ToString(item->name) : "???";
			int count = Main_Data::game_party ? Main_Data::game_party->GetItemCount(item_id) : 0;
			items.push_back(fmt::format("{}x{}", count, name));
		}

		detail_window.reset(new Window_Command(items, 200, 10));
		detail_window->SetX(Player::menu_offset_x + 120);
		detail_window->SetY(Player::menu_offset_y + 32);
		detail_window->SetActive(true);

		help_window->SetText("Items: Select to give");
	}
}

void Scene_GodMode::UpdateItemList() {
	// Handled inline in vUpdate for eRangeList + parent_menu == eItems
}

void Scene_GodMode::UpdateItemAmountInput() {
	// Handled inline in vUpdate for eNumberInput + parent_menu == eItems
}

// ========== Stats ==========

void Scene_GodMode::EnterStats() {
	current_menu = eStats;
	main_window->SetActive(false);

	std::vector<std::string> actor_names;
	if (Main_Data::game_party) {
		auto actors = Main_Data::game_party->GetActors();
		for (auto* actor : actors) {
			actor_names.push_back(fmt::format("{} L{} HP{}/{} SP{}/{}",
				ToString(actor->GetName()),
				actor->GetLevel(),
				actor->GetHp(), actor->GetMaxHp(),
				actor->GetSp(), actor->GetMaxSp()));
		}
	}
	if (actor_names.empty()) {
		actor_names.push_back("(No actors)");
	}

	range_window.reset(new Window_Command(actor_names, 280, 8));
	range_window->SetX(Player::menu_offset_x + 20);
	range_window->SetY(Player::menu_offset_y + 32);
	range_window->SetActive(true);

	help_window->SetText("Stats: Select actor");
}

void Scene_GodMode::UpdateStatsActorSelect() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		PopSubMenu();
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		auto actors = Main_Data::game_party->GetActors();
		int idx = range_window->GetIndex();
		if (idx < 0 || idx >= static_cast<int>(actors.size())) return;

		selected_actor_id = actors[idx]->GetId();
		auto* actor = actors[idx];
		current_menu = eRangeList;
		parent_menu = eStats;
		range_window->SetActive(false);

		std::vector<std::string> stat_opts = {
			fmt::format("Level: {}", actor->GetLevel()),
			fmt::format("HP: {}/{}", actor->GetHp(), actor->GetMaxHp()),
			fmt::format("SP: {}/{}", actor->GetSp(), actor->GetMaxSp()),
		};

		detail_window.reset(new Window_Command(stat_opts, 200));
		detail_window->SetX(Player::menu_offset_x + 120);
		detail_window->SetY(Player::menu_offset_y + 80);
		detail_window->SetActive(true);

		help_window->SetText(fmt::format("Stats for {}: Select stat", ToString(actor->GetName())));
	}
}

void Scene_GodMode::UpdateStatsStatSelect() {
	// Handled in eRangeList + parent_menu == eStats
}

void Scene_GodMode::UpdateStatsValueInput() {
	// Handled in eNumberInput + parent_menu == eStats
}

// ========== Full Heal ==========

void Scene_GodMode::DoFullHeal() {
	// Send god command (cmd_type 4 = full heal)
	auto& mp = MultiplayerState::Instance();
	mp.SendGodCommand(4, {});

	// Apply locally
	if (Main_Data::game_party) {
		auto actors = Main_Data::game_party->GetActors();
		for (auto* actor : actors) {
			actor->FullHeal();
		}
	}

	help_window->SetText("All party members fully healed!");
}

// ========== Utility ==========

void Scene_GodMode::RefreshRangeWindow(int, int) {
	// Placeholder for future pagination
}

void Scene_GodMode::RefreshDetailWindow() {
	// Placeholder for future detail refresh
}

void Scene_GodMode::EnterTeleportCoords() {
	// Handled inline in UpdateTeleport
}

void Scene_GodMode::UpdateTeleportCoords() {
	// Handled inline in UpdateTeleport
}

// ========== Range List Detail (switches toggle, variable select, item select, stat select) ==========

void Scene_GodMode::UpdateRangeListDetail() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		detail_window.reset();
		if (parent_menu == eStats) {
			// Go back to actor select
			current_menu = eStats;
			range_window->SetActive(true);
			help_window->SetText("Stats: Select actor");
		} else {
			// Go back to range select
			current_menu = parent_menu;
			range_window->SetActive(true);
			if (parent_menu == eSwitches) help_window->SetText("Switches: Select range");
			else if (parent_menu == eVariables) help_window->SetText("Variables: Select range");
			else if (parent_menu == eItems) help_window->SetText("Items: Select range");
		}
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		int idx = detail_window->GetIndex();

		if (parent_menu == eSwitches) {
			// Toggle switch
			int sw_id = range_page * 10 + 1 + idx;
			bool new_val = !(Main_Data::game_switches->Get(sw_id));

			// Send god command (cmd_type 0 = set switch)
			auto& mp = MultiplayerState::Instance();
			mp.SendGodCommand(0, {sw_id, new_val ? 1 : 0});

			// Apply locally
			Main_Data::game_switches->Set(sw_id, new_val);
			Game_Map::SetNeedRefresh(true);

			// Refresh display
			int base = range_page * 10 + 1;
			int total = Main_Data::game_switches->GetSizeWithLimit();
			std::vector<std::string> items;
			for (int i = 0; i < 10 && (base + i) <= total; ++i) {
				int sid = base + i;
				bool val = Main_Data::game_switches->Get(sid);
				items.push_back(fmt::format("[{}] {:04d}", val ? "ON " : "OFF", sid));
			}
			detail_window->ReplaceCommands(items);
			detail_window->SetIndex(idx);
			detail_window->Refresh();

			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		}
		else if (parent_menu == eVariables) {
			// Open number input for variable
			int var_id = range_page * 10 + 1 + idx;
			selected_id = var_id;
			current_menu = eNumberInput;
			detail_window->SetActive(false);

			int32_t current_val = Main_Data::game_variables->Get(var_id);

			number_window.reset(new Window_NumberInput(
				Player::menu_offset_x + 40, Player::menu_offset_y + 80, 240, 48));
			number_window->SetMaxDigits(6);
			number_window->SetShowOperator(true);
			number_window->SetNumber(current_val);
			number_window->SetVisible(true);
			number_window->SetActive(true);
			number_window->Refresh();

			help_window->SetText(fmt::format("Variable {:04d}: Enter new value", var_id));
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		}
		else if (parent_menu == eItems) {
			// Give 1 item
			int item_id = range_page * 10 + 1 + idx;

			// Send god command (cmd_type 8 = give item)
			auto& mp = MultiplayerState::Instance();
			mp.SendGodCommand(8, {item_id, 1});

			// Apply locally
			if (Main_Data::game_party) {
				Main_Data::game_party->AddItem(item_id, 1);
			}

			// Refresh display
			int base = range_page * 10 + 1;
			int total = static_cast<int>(lcf::Data::items.size());
			std::vector<std::string> items;
			for (int i = 0; i < 10 && (base + i) <= total; ++i) {
				int iid = base + i;
				auto* item = lcf::ReaderUtil::GetElement(lcf::Data::items, iid);
				std::string name = item ? ToString(item->name) : "???";
				int count = Main_Data::game_party ? Main_Data::game_party->GetItemCount(iid) : 0;
				items.push_back(fmt::format("{}x{}", count, name));
			}
			detail_window->ReplaceCommands(items);
			detail_window->SetIndex(idx);
			detail_window->Refresh();

			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		}
		else if (parent_menu == eStats) {
			// Select stat to edit
			selected_stat = idx; // 0=Level, 1=HP, 2=SP
			current_menu = eNumberInput;
			detail_window->SetActive(false);

			auto* actor = Main_Data::game_actors->GetActor(selected_actor_id);
			int current_val = 0;
			std::string stat_name;
			if (actor) {
				switch (selected_stat) {
					case 0: current_val = actor->GetLevel(); stat_name = "Level"; break;
					case 1: current_val = actor->GetHp(); stat_name = "HP"; break;
					case 2: current_val = actor->GetSp(); stat_name = "SP"; break;
				}
			}

			number_window.reset(new Window_NumberInput(
				Player::menu_offset_x + 40, Player::menu_offset_y + 140, 240, 48));
			number_window->SetMaxDigits(5);
			number_window->SetNumber(current_val);
			number_window->SetVisible(true);
			number_window->SetActive(true);
			number_window->Refresh();

			help_window->SetText(fmt::format("{} {}: Enter new value", ToString(actor->GetName()), stat_name));
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		}
	}
}

// ========== Number Input Detail (variable edit, stat edit) ==========

void Scene_GodMode::UpdateNumberInputDetail() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		number_window.reset();
		current_menu = eRangeList;
		detail_window->SetActive(true);
		if (parent_menu == eVariables) help_window->SetText("Variables: Select to edit");
		else if (parent_menu == eStats) {
			auto* actor = Main_Data::game_actors->GetActor(selected_actor_id);
			help_window->SetText(fmt::format("Stats for {}: Select stat", actor ? ToString(actor->GetName()) : "???"));
		}
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		int new_val = number_window->GetNumber();

		if (parent_menu == eVariables) {
			// Send god command (cmd_type 1 = set variable)
			auto& mp = MultiplayerState::Instance();
			mp.SendGodCommand(1, {selected_id, new_val});

			// Apply locally
			if (Main_Data::game_variables) {
				Main_Data::game_variables->Set(selected_id, new_val);
			}
			Game_Map::SetNeedRefresh(true);

			// Go back to variable list and refresh
			number_window.reset();
			current_menu = eRangeList;
			detail_window->SetActive(true);

			int base = range_page * 10 + 1;
			int total = Main_Data::game_variables->GetSizeWithLimit();
			std::vector<std::string> items;
			for (int i = 0; i < 10 && (base + i) <= total; ++i) {
				int var_id = base + i;
				int32_t val = Main_Data::game_variables->Get(var_id);
				items.push_back(fmt::format("{:04d}={}", var_id, val));
			}
			detail_window->ReplaceCommands(items);
			detail_window->Refresh();

			help_window->SetText("Variables: Select to edit");
		}
		else if (parent_menu == eStats) {
			auto* actor = Main_Data::game_actors->GetActor(selected_actor_id);
			if (actor) {
				auto& mp = MultiplayerState::Instance();
				switch (selected_stat) {
					case 0: // Level
						mp.SendGodCommand(5, {selected_actor_id, new_val});
						actor->ChangeLevel(new_val, nullptr);
						break;
					case 1: // HP
						mp.SendGodCommand(6, {selected_actor_id, new_val});
						actor->SetHp(new_val);
						break;
					case 2: // SP
						mp.SendGodCommand(7, {selected_actor_id, new_val});
						actor->SetSp(new_val);
						break;
				}

				// Refresh stat display
				std::vector<std::string> stat_opts = {
					fmt::format("Level: {}", actor->GetLevel()),
					fmt::format("HP: {}/{}", actor->GetHp(), actor->GetMaxHp()),
					fmt::format("SP: {}/{}", actor->GetSp(), actor->GetMaxSp()),
				};
				detail_window->ReplaceCommands(stat_opts);
				detail_window->Refresh();
			}

			number_window.reset();
			current_menu = eRangeList;
			detail_window->SetActive(true);
			help_window->SetText(fmt::format("Stats for {}: Select stat", actor ? ToString(actor->GetName()) : "???"));
		}
	}
}

} // namespace Chaos
