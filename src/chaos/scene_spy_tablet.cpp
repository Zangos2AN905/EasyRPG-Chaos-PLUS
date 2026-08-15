#include "chaos/scene_spy_tablet.h"
#include "chaos/multiplayer_state.h"
#include "game_system.h"
#include "input.h"
#include "main_data.h"
#include "player.h"

namespace Chaos {

Scene_SpyTablet::Scene_SpyTablet() {
	type = Scene::SpyTablet;
}

void Scene_SpyTablet::Start() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	help_window->SetText("Spy Tablet - choose a player to spectate");

	std::vector<std::string> names;
	for (const auto& [peer_id, player] : MultiplayerState::Instance().GetRemotePlayers()) {
		if (!player) continue;
		player_ids.push_back(peer_id);
		names.push_back(player->GetPlayerName());
	}
	if (names.empty()) names.push_back("(No other players)");
	player_window = std::make_unique<Window_Command>(names, Player::screen_width / 2);
	player_window->SetX(Player::screen_width / 4);
	player_window->SetY(48);
	player_window->SetHeight(std::min(Player::screen_height - 64, static_cast<int>(names.size()) * 16 + 32));
}

void Scene_SpyTablet::vUpdate() {
	player_window->Update();
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}
	if (!Input::IsTriggered(Input::DECISION) || player_ids.empty()) return;
	const int index = player_window->GetIndex();
	if (index < 0 || index >= static_cast<int>(player_ids.size())) return;
	Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
	MultiplayerState::Instance().SpectatePlayer(player_ids[index]);
	Scene::PopUntil(Scene::Map);
}

} // namespace Chaos
