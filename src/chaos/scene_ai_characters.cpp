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

#include "scene_ai_characters.h"
#include "ai_characters.h"
#include "../input.h"
#include "../game_system.h"
#include "../main_data.h"
#include "../player.h"
#include "../output.h"
#include "../options.h"

Scene_AICharacters::Scene_AICharacters() {
	Scene::type = Scene::AICharacters;
}

void Scene_AICharacters::Start() {
	// Ensure characters are loaded from disk
	Chaos::AICharacters::LoadCharacters();

	// Title bar at the top
	title_window.reset(new Window_Help(0, 0, Player::screen_width, 32));
	title_window->SetText("AI Character Manager", Font::ColorDefault, Text::AlignCenter, false);

	// Hint bar at the bottom
	hint_window.reset(new Window_Help(0, Player::screen_height - 32, Player::screen_width, 32));
	hint_window->SetText("[1] Create  [2] Delete  [3] Toggle  [Esc] Back", Font::ColorDefault, Text::AlignCenter, false);

	// Build the character list
	RefreshList();
}

void Scene_AICharacters::RefreshList() {
	auto& chars = Chaos::AICharacters::GetAllCharacters();

	std::vector<std::string> items;

	if (chars.empty()) {
		items.push_back("(No characters yet - press 1 to create)");
	} else {
		for (size_t i = 0; i < chars.size(); ++i) {
			std::string entry;
			entry += chars[i].enabled ? "[ON]  " : "[OFF] ";
			entry += chars[i].name;

			// Show personality + writing style
			entry += " (";
			entry += Chaos::AICharacters::PersonalityToString(chars[i].personality);
			entry += ", ";
			entry += Chaos::AICharacters::WritingStyleToString(chars[i].writing_style);
			entry += ")";

			items.push_back(std::move(entry));
		}
	}

	int saved_index = list_window ? list_window->GetIndex() : 0;

	// Command list fills the area between title and hint bars
	int list_y = 32;
	int list_h = Player::screen_height - 64; // 32 title + 32 hint
	list_window.reset(new Window_Command(items, Player::screen_width, list_h / 16));
	list_window->SetX(0);
	list_window->SetY(list_y);
	list_window->SetWidth(Player::screen_width);
	list_window->SetHeight(list_h);

	// Disable the placeholder item if list is empty
	if (chars.empty()) {
		list_window->DisableItem(0);
	}

	// Restore cursor position
	if (saved_index >= 0 && saved_index < static_cast<int>(items.size())) {
		list_window->SetIndex(saved_index);
	}
}

void Scene_AICharacters::vUpdate() {
	list_window->Update();

	auto& chars = Chaos::AICharacters::GetAllCharacters();

	// [1] Create character
	if (Input::IsTriggered(Input::N1)) {
		// Generate a default name based on count
		int num = static_cast<int>(chars.size()) + 1;
		std::string name = "Character " + std::to_string(num);
		int idx = Chaos::AICharacters::CreateCharacter(name);
		if (idx >= 0) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Decision));
			Output::Debug("Chaos AI Characters: created '{}'", chars[idx].name);
			RefreshList();
			list_window->SetIndex(idx);
		} else {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Buzzer));
		}
		return;
	}

	// [2] Delete selected character
	if (Input::IsTriggered(Input::N2)) {
		if (!chars.empty()) {
			int idx = list_window->GetIndex();
			if (idx >= 0 && idx < static_cast<int>(chars.size())) {
				Output::Debug("Chaos AI Characters: deleting '{}'", chars[idx].name);
				Chaos::AICharacters::DeleteCharacter(idx);
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Decision));
				RefreshList();
			}
		} else {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Buzzer));
		}
		return;
	}

	// [3] Toggle selected character (enable/disable)
	if (Input::IsTriggered(Input::N3)) {
		if (!chars.empty()) {
			int idx = list_window->GetIndex();
			if (idx >= 0 && idx < static_cast<int>(chars.size())) {
				if (chars[idx].enabled) {
					Chaos::AICharacters::DisableCharacter(idx);
				} else {
					if (!Chaos::AICharacters::EnableCharacter(idx)) {
						// Max active reached
						Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Buzzer));
						hint_window->SetText("Max 4 active characters!", Font::ColorKnockout, Text::AlignCenter, false);
						return;
					}
				}
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Decision));
				hint_window->SetText("[1] Create  [2] Delete  [3] Toggle  [Esc] Back", Font::ColorDefault, Text::AlignCenter, false);
				RefreshList();
			}
		} else {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Buzzer));
		}
		return;
	}

	// Cancel - go back
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Game_System::SFX_Cancel));
		Scene::Pop();
		return;
	}
}
