/*
 * Chaos Fork: Multiplayer Wait Scene Implementation
 * Client waits here while the host selects a save file or starts a new game.
 * When host enters a map, HostMapReady is received and the client starts.
 */

#include "chaos/scene_multiplayer_wait.h"
#include "chaos/net_manager.h"
#include "chaos/multiplayer_state.h"
#include "chaos/multiplayer_mode.h"
#include "input.h"
#include "player.h"
#include "scene.h"
#include "scene_map.h"
#include "game_actor.h"
#include "game_actors.h"
#include "game_switches.h"
#include "game_system.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "game_variables.h"
#include "main_data.h"
#include "game_clock.h"
#include "output.h"
#include <fmt/format.h>

namespace Chaos {

Scene_MultiplayerWait::Scene_MultiplayerWait() {
	type = Scene::MultiplayerWait;
}

void Scene_MultiplayerWait::Start() {
	status_window = std::make_unique<Window_Help>(
		0, Player::screen_height / 2 - 16,
		Player::screen_width, 32);
	status_window->SetText("Waiting for host to select a game...");
	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_MultiplayerWait::vUpdate() {
	auto& net = NetManager::Instance();
	net.Update();

	timer++;

	if (Input::IsTriggered(Input::CANCEL)) {
		if (Main_Data::game_system) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		}
		MultiplayerState::Instance().StopMultiplayer();
		net.Disconnect();
		Scene::Pop();
		return;
	}

	// Check if host disconnected
	if (net.IsHostDisconnected() || !net.IsConnected()) {
		net.ClearHostDisconnected();
		status_window->SetText("Host disconnected!");
		// Wait a moment then pop
		if (timer > 120) {
			MultiplayerState::Instance().StopMultiplayer();
			net.Disconnect();
			Scene::Pop();
		}
		return;
	}

	// Wait until the complete host snapshot (map + switches + variables)
	// is available before bootstrapping. This prevents starting the game
	// with a half-applied host state.
	if (!bootstrap_started && net.IsJoinSnapshotReady()) {
		net.ClearJoinSnapshot();
		bootstrap_started = true;
	}

	if (bootstrap_started && !bootstrap_complete) {
		UpdateBootstrap(net);
		return;
	}

	// Animate dots
	std::string dots;
	int n = (timer / 30) % 4;
	for (int i = 0; i < n; ++i) dots += '.';
	status_window->SetText(fmt::format("Waiting for host to select a game{}", dots));
}

void Scene_MultiplayerWait::UpdateBootstrap(NetManager& net) {
	// One-time: capture the host snapshot into local members.
	// ClearJoinSnapshot() only clears the ready-flags, the data survives.
	if (!bootstrap_data_captured) {
		bootstrap_data_captured = true;
		bootstrap_map_id = net.GetHostMapId();
		bootstrap_map_x = net.GetHostMapX();
		bootstrap_map_y = net.GetHostMapY();
		bootstrap_host_party = net.GetHostParty();
		bootstrap_host_switches = net.GetHostSwitches();
		bootstrap_host_variables = net.GetHostVariables();
	}

	// Give the network a frame or two to settle before rebuilding the world.
	if (bootstrap_wait_frames < 2) {
		++bootstrap_wait_frames;
		return;
	}

	// If we never got a valid map from the host, we cannot bootstrap.
	if (bootstrap_map_id <= 0) {
		Output::Warning("Multiplayer: Join snapshot has no valid map ({}), waiting", bootstrap_map_id);
		// Re-arm the snapshot so a fresh HostMapReady can retry.
		bootstrap_started = false;
		bootstrap_data_captured = false;
		return;
	}

	// Rebuild the complete game object graph exactly once before applying
	// host state. The client may be coming from the lobby/browser with
	// stale objects.
	if (!bootstrap_objects_reset) {
		bootstrap_objects_reset = true;
		Player::ResetGameObjects();
	}

	if (!Main_Data::game_system || !Main_Data::game_party ||
		!Main_Data::game_player || !Main_Data::game_switches ||
		!Main_Data::game_variables || !Main_Data::game_actors ||
		!Main_Data::game_screen || !Main_Data::game_pictures ||
		!Main_Data::game_dynrpg || !Main_Data::game_targets) {
		Output::Warning("Multiplayer: Game objects missing after reset, aborting join");
		// Do not push a half-initialized map scene; bail to the lobby instead.
		MultiplayerState::Instance().StopMultiplayer();
		net.Disconnect();
		Scene::Pop();
		return;
	}

	// Pre-game setup (replaces Player::SetupNewGame to apply host state).
	Main_Data::game_party->SetupNewGame();

	// Apply host party: actor IDs with level/HP/SP.
	if (!bootstrap_host_party.empty()) {
		Main_Data::game_party->Clear();
		for (const auto& m : bootstrap_host_party) {
			Main_Data::game_party->AddActor(m.actor_id);
			auto* actor = Main_Data::game_actors->GetActor(m.actor_id);
			if (actor) {
				actor->ChangeLevel(m.level, nullptr);
				actor->SetHp(m.hp);
				actor->SetSp(m.sp);
			}
		}
	}

	// Apply host switches (1-indexed).
	for (size_t i = 0; i < bootstrap_host_switches.size(); ++i) {
		Main_Data::game_switches->Set(static_cast<int>(i + 1), bootstrap_host_switches[i]);
	}
	// Apply host variables (1-indexed).
	for (size_t i = 0; i < bootstrap_host_variables.size(); ++i) {
		Main_Data::game_variables->Set(static_cast<int>(i + 1), bootstrap_host_variables[i]);
	}

	// Move to the host's map. Game_Player::MoveTo loads the map synchronously
	// (LoadMapFile + Setup), so Scene_Map::Start() always sees a valid map.
	Main_Data::game_player->MoveTo(bootstrap_map_id, bootstrap_map_x, bootstrap_map_y);

	bootstrap_complete = true;
	Output::Debug("Multiplayer: Bootstrap complete, entering map {}", bootstrap_map_id);

	// Replace this wait scene with the map scene.
	Scene::Push(std::make_shared<Scene_Map>(0), true);
}

} // namespace Chaos
