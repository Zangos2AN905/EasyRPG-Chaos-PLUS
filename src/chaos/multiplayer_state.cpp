/*
 * Chaos Fork: Multiplayer State Manager Implementation
 */

#include "chaos/multiplayer_state.h"
#include "chaos/multiplayer_radio.h"
#include "chaos/multiplayer_chat.h"
#include "chaos/multiplayer_commands.h"
#include "chaos/custom_mode.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "chaos/drawable_rubber_band.h"
#include "chaos/discord_integration.h"
#include "chaos/undertale_mode.h"
#include "chaos/game_mode.h"
#include "game_actor.h"
#include "game_actors.h"
#include "game_event.h"
#include "game_map.h"
#include "game_interpreter.h"
#include "game_party.h"
#include "game_player.h"
#include "game_switches.h"
#include "game_variables.h"
#include "game_system.h"
#include "audio.h"
#include "audio_secache.h"
#include "bitmap.h"
#include "cache.h"
#include "filefinder.h"
#include "input.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "rand.h"
#include "scene.h"
#include "scene_battle.h"
#include "scene_gameover.h"
#include "sprite.h"
#include "spriteset_map.h"
#include "sprite_character.h"
#include "window_help.h"
#include <lcf/reader_util.h>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace Chaos {

static constexpr const char* kLocalSkinCacheName = "__skin_local";

MultiplayerState& MultiplayerState::Instance() {
	static MultiplayerState instance;
	return instance;
}

void MultiplayerState::StartMultiplayer() {
	if (active) return;
	active = true;
	admin_peers.clear();
	MultiplayerRadio::Instance().Start();
	noclip_peers.clear();
	SetupCallbacks();
	if (NetManager::Instance().GetMode() == MultiplayerMode::Custom) {
		CustomMode::Instance().Start();
	}
	if (IsModeActive(MultiplayerMode::Split)) {
		SplitMode::Instance().Start();
	}
	if (IsModeActive(MultiplayerMode::Underwater)) {
		UnderwaterMode::Instance().Start();
	}
	Output::Debug("Multiplayer: State manager started");
}

bool MultiplayerState::IsModeActive(MultiplayerMode m) const {
	if (!active) return false;
	auto cur = NetManager::Instance().GetMode();
	if (cur == m) return true;
	if (cur == MultiplayerMode::Custom) {
		return CustomMode::Instance().IsModeInMix(m);
	}
	return false;
}

bool MultiplayerState::IsBattleSyncMode() const {
	return IsModeActive(MultiplayerMode::Single) ||
	       IsModeActive(MultiplayerMode::TeamParty) ||
	       IsModeActive(MultiplayerMode::Chaotix);
}

bool MultiplayerState::IsAdmin(uint16_t peer_id) const {
	return peer_id == 1 || admin_peers.count(peer_id) != 0;
}

bool MultiplayerState::IsNoclipEnabled(uint16_t peer_id) const {
	return noclip_peers.count(peer_id) != 0;
}

void MultiplayerState::SetNoclipEnabled(uint16_t peer_id, bool value) {
	if (value) noclip_peers.insert(peer_id);
	else noclip_peers.erase(peer_id);
}

void MultiplayerState::SetAdmin(uint16_t peer_id, bool value) {
	if (peer_id == 1) return;
	if (value) admin_peers.insert(peer_id);
	else admin_peers.erase(peer_id);
	Output::Debug("Multiplayer: Peer {} {} admin", peer_id, value ? "ranked as" : "demoted from");
}

void MultiplayerState::StopMultiplayer() {
	if (!active) return;
	SplitMode::Instance().Stop();
	UnderwaterMode::Instance().Stop();
	CustomMode::Instance().Stop();
	MultiplayerRadio::Instance().Stop();
	ExitSpectatorMode();
	active = false;
	host_lost = false;
	in_battle = false;
	pending_battle_invite = false;
	forced_battle = false;
	forced_map_change = false;
	forced_map_id = 0;
	forced_map_x = 0;
	forced_map_y = 0;
	last_announced_map_id = -1;
	remote_battle_ended = false;
	remote_battle_result = 0;
	last_actor_states.clear();
	actor_state_sync_counter = 0;
	DestroyRubberBandDrawable();
	darkness_overlay.reset();
	is_local_god = false;
	god_player_id = 0;
	CleanupHorrorMode();
	CleanupAsymMode();
	battle_invite_window.reset();
	remote_battle_actions.clear();
	if (current_spriteset) {
		for (auto& [id, player] : remote_players) {
			if (player) {
				current_spriteset->RemoveCharacterSprites(player.get());
			}
		}
	}
	remote_players.clear();
	admin_peers.clear();
	noclip_peers.clear();
	last_switches.clear();
	last_variables.clear();
	 applied_event_commands.clear();
	 pending_event_requests.clear();
	pending_event_inputs.clear();
	next_event_command = 0;
	next_event_request = 0;
	current_spriteset = nullptr;
	skin_sent_this_session = false;
	for (auto& [id, name] : peer_skin_names) {
		Cache::UnregisterCharset(name);
	}
	peer_skin_names.clear();
	DiscordIntegration::ClearMultiplayerPresence();
	Output::Debug("Multiplayer: State manager stopped");
}

void MultiplayerState::SetupCallbacks() {
	auto& net = NetManager::Instance();

	net.SetConnectCallback([this](uint16_t peer_id) {
		OnPlayerConnected(peer_id);
	});

	net.SetDisconnectCallback([this](uint16_t peer_id) {
		OnPlayerDisconnected(peer_id);
	});

	net.SetPacketCallback([this](uint16_t sender_id, const uint8_t* data, size_t len) {
		OnPacketReceived(sender_id, data, len);
	});
}

void MultiplayerState::Update() {
	if (!active) return;

	auto& net = NetManager::Instance();
	if (!net.IsConnected()) {
		// Check if host disconnected (client side)
		if (net.IsHostDisconnected()) {
			net.ClearHostDisconnected();
			host_lost = true;
			Output::Debug("Multiplayer: Host disconnected, returning to menu");
		}
		return;
	}

	// Process network events
	net.Update();
	MultiplayerRadio::Instance().Update();

	// Only do gameplay sync when we're on a map (spriteset exists)
	if (!current_spriteset) return;

	// Send local player position periodically (skip if spectating — we're dead)
	if (!spectating) {
		// Broadcast skin data once per session when first on a map
		if (HasSkin() && !skin_sent_this_session) {
			BroadcastSkin();
		}

		position_send_counter++;
		if (position_send_counter >= POSITION_SEND_INTERVAL) {
			position_send_counter = 0;
			SendLocalPlayerPosition();
		}
	}

	// Update remote player characters (movement animation, stepping)
	// Collect IDs first to avoid iterator invalidation if a callback removes a player
	std::vector<uint16_t> update_ids;
	update_ids.reserve(remote_players.size());
	for (auto& [id, rp] : remote_players) {
		if (rp && rp->IsOnCurrentMap()) {
			update_ids.push_back(id);
		}
	}
	for (auto id : update_ids) {
		auto it = remote_players.find(id);
		if (it != remote_players.end() && it->second) {
			it->second->UpdateRemote();
		}
	}

	// Sync switches and variables based on mode
	auto props = (net.GetMode() == MultiplayerMode::Custom)
		? CustomMode::Instance().GetEffectiveProperties()
		: GetModeProperties(net.GetMode());
	if (props.sync_switches && net.IsHost()) {
		CheckAndSyncSwitches();
	}
	if (props.sync_variables && net.IsHost()) {
		CheckAndSyncVariables();
	}

	// Sync actor states (HP/SP/conditions) in Chaotix mode
	if (props.sync_actor_states && net.IsHost()) {
		actor_state_sync_counter++;
		if (actor_state_sync_counter >= ACTOR_STATE_SYNC_INTERVAL) {
			actor_state_sync_counter = 0;
			CheckAndSyncActorStates();
		}
	}

	// Sync event positions from host to clients
	if (net.IsHost()) {
		event_sync_counter++;
		if (event_sync_counter >= EVENT_SYNC_INTERVAL) {
			event_sync_counter = 0;
			SyncEventPositions();
		}
	}

	// Update spectator mode
	if (spectating) {
		UpdateSpectator();
	}

	// Update battle invite prompt
	if (pending_battle_invite && !in_battle) {
		UpdateBattleInvite();
	}

	// Update Chaotix rubber band
	if (props.proximity_required && !in_battle) {
		UpdateRubberBand();
	}

	// Update Horror mode
	if (net.GetMode() == MultiplayerMode::DeprecatedHorror) {
		UpdateHorror();
	}

	// Update ASYM mode
	if (net.GetMode() == MultiplayerMode::Asym) {
		UpdateAsym();
	}

	if (IsModeActive(MultiplayerMode::Split)) {
		SplitMode::Instance().Update();
	}
	if (IsModeActive(MultiplayerMode::Underwater)) {
		UnderwaterMode::Instance().Update();
	}
	if (net.GetMode() == MultiplayerMode::Custom) {
		CustomMode::Instance().Update();
	}

	// Undertale mode: pixel movement is handled via Game_Player hooks
	// (UndertaleMode::Instance().IsActive() checks for this mode)
}

void MultiplayerState::OnMapLoaded(Spriteset_Map* spriteset) {
	// OnMapLoaded fires on every spriteset refresh, so only finalize the
	// local player slot when this is the first activation of a map (after
	// the join bootstrap there is no map yet). Later refreshes must not
	// re-run ResetGraphic(), which would stomp move-route sprite changes.
	const bool first_map_activation = (current_spriteset == nullptr);
	current_spriteset = spriteset;
	MultiplayerRadio::Instance().OnMapLoaded();

	if (HasSkin() && !skin_image_data.empty()) {
		BitmapRef bmp = Bitmap::Create(skin_image_data.data(), static_cast<unsigned>(skin_image_data.size()), true);
		if (bmp) {
			Cache::RegisterCharset(kLocalSkinCacheName, bmp);
		}
		if (Main_Data::game_player) {
			Main_Data::game_player->SetSpriteGraphic(kLocalSkinCacheName, skin_char_index);
		}
	}

	// Create sprites for all remote players that are on this map
	for (auto& [id, player] : remote_players) {
		if (player && player->IsOnCurrentMap()) {
			CreateRemotePlayerSprite(player.get());
		}
	}

	// Finalize the local player's team-slot assignment on first map entry.
	// During bootstrap AddActor() runs ResetGraphic() with a partially
	// built party; re-run it here so a valid slot gets its actor sprite and
	// a genuinely out-of-range slot (more players than party members)
	// enters spectator mode via the map-active guard above.
	if (first_map_activation && Main_Data::game_player) {
		Main_Data::game_player->ResetGraphic();
	}

	// Host: notify clients which map we loaded so they can join
	auto& net = NetManager::Instance();

	// Create rubber band drawable for Chaotix mode
	if (net.IsConnected()) {
		auto props = (net.GetMode() == MultiplayerMode::Custom)
			? CustomMode::Instance().GetEffectiveProperties()
			: GetModeProperties(net.GetMode());
		if (props.proximity_required) {
			CreateRubberBandDrawable();
		}
	}

	// Create darkness/lighting overlay (available in all modes, including singleplayer)
	if (!darkness_overlay) {
		darkness_overlay = std::make_unique<Drawable_DarknessOverlay>();
	}
	if (IsModeActive(MultiplayerMode::Split)) {
		SplitMode::Instance().OnMapLoaded(spriteset);
	}
	if (IsModeActive(MultiplayerMode::Underwater)) {
		UnderwaterMode::Instance().OnMapLoaded(spriteset);
	}
	if (net.GetMode() == MultiplayerMode::Custom) {
		CustomMode::Instance().OnMapLoaded();
	}

	// Singleplayer Horror mode: enable darkness overlay
	if (IsHorrorSingleplayerMode() && !net.IsConnected()) {
		darkness_overlay->SetDarknessLevel(HORROR_DARKNESS_LEVEL);
		darkness_overlay->SetPlayerLight(HORROR_FLASHLIGHT_RADIUS);
		darkness_overlay->SetEnabled(true);
	}

	// Initialize Horror mode on map load (skip if spectating — already dead)
	if (net.IsConnected() && net.GetMode() == MultiplayerMode::DeprecatedHorror && !spectating) {
		InitHorrorMode();
	}

	// Initialize ASYM mode on map load (skip if spectating — already dead)
	if (net.IsConnected() && net.GetMode() == MultiplayerMode::Asym && !spectating) {
		InitAsymMode();
	}

	if (net.IsHost() && net.IsConnected()) {
		auto* hero = Main_Data::game_player.get();
		if (hero) {
			int map_id = Game_Map::GetMapId();
			int x = hero->GetX();
			int y = hero->GetY();

			// Only announce the snapshot when the map actually changed.
			// OnMapLoaded fires on every spriteset refresh; re-broadcasting
			// the full snapshot would spam clients and re-arm their
			// join-snapshot flags.
			if (map_id == last_announced_map_id) {
				// Same map, nothing new to announce.
			} else {
				last_announced_map_id = map_id;

				PacketWriter pw(PacketType::HostMapReady);
				pw.write(static_cast<int32_t>(map_id));
				pw.write(static_cast<int32_t>(x));
				pw.write(static_cast<int32_t>(y));

				// Send party actor IDs and levels
				auto actors = Main_Data::game_party->GetActors();
				// Count non-null actors first
				uint16_t actor_count = 0;
				for (auto* a : actors) { if (a) actor_count++; }
				pw.write(actor_count);
				for (auto* actor : actors) {
					if (!actor) continue;
					pw.write(static_cast<uint16_t>(actor->GetId()));
					pw.write(static_cast<int32_t>(actor->GetLevel()));
					pw.write(static_cast<int32_t>(actor->GetHp()));
					pw.write(static_cast<int32_t>(actor->GetSp()));
				}

				net.Broadcast(pw, true);
				Output::Debug("Multiplayer: Host sent HostMapReady map={} x={} y={} actors={}",
					map_id, x, y, actor_count);

				// Always send bulk switch and variable packets, including empty snapshots.
				PacketWriter sw_pw(PacketType::SwitchBulkSync);
				if (Main_Data::game_switches) {
					auto& sw = Main_Data::game_switches->GetData();
					sw_pw.write(static_cast<uint16_t>(sw.size()));
					for (bool value : sw) sw_pw.write(static_cast<uint8_t>(value ? 1 : 0));
				} else {
					sw_pw.write(static_cast<uint16_t>(0));
				}
				net.Broadcast(sw_pw, true);

				PacketWriter var_pw(PacketType::VariableBulkSync);
				if (Main_Data::game_variables) {
					auto& vars = Main_Data::game_variables->GetData();
					var_pw.write(static_cast<uint16_t>(vars.size()));
					for (auto value : vars) var_pw.write(static_cast<int32_t>(value));
				} else {
					var_pw.write(static_cast<uint16_t>(0));
				}
				net.Broadcast(var_pw, true);

				// Chaotix mode: force all clients to follow the host's map change
				if (IsModeActive(MultiplayerMode::Chaotix)) {
					PacketWriter fpw(PacketType::MapChangeForce);
					fpw.write(static_cast<int32_t>(map_id));
					fpw.write(static_cast<int32_t>(x));
					fpw.write(static_cast<int32_t>(y));
					net.Broadcast(fpw, true);
					Output::Debug("Multiplayer: Chaotix host map force to map {}", map_id);
				}
			}
		}
	}
}

void MultiplayerState::OnMapUnloaded() {
	if (IsModeActive(MultiplayerMode::Split)) {
		SplitMode::Instance().OnMapUnloaded();
	}
	if (IsModeActive(MultiplayerMode::Underwater)) {
		UnderwaterMode::Instance().OnMapUnloaded();
	}
	DestroyRubberBandDrawable();
	// Save lighting settings before destroying overlay
	if (darkness_overlay) {
		lighting_enabled = darkness_overlay->IsEnabled();
		lighting_darkness_level = darkness_overlay->GetDarknessLevel();
		lighting_player_radius = darkness_overlay->GetPlayerLightRadius();
	}
	darkness_overlay.reset();
	current_spriteset = nullptr;
}

void MultiplayerState::OnPlayerConnected(uint16_t peer_id) {
	auto& net = NetManager::Instance();

	// Skip if this is our own connection notification
	if (peer_id == net.GetLocalPeerId()) return;

	// Find the peer info to get name
	auto* pi = net.FindPeer(peer_id);
	std::string name = pi ? pi->player_name : "Player";

	// Create remote player
	auto rp = std::make_unique<Game_RemotePlayer>(peer_id, name);
	auto* ptr = rp.get();
	remote_players[peer_id] = std::move(rp);

	Output::Debug("Multiplayer: Created remote player for peer {}", peer_id);

	// If we're on a map, create sprite immediately
	if (current_spriteset && ptr->IsOnCurrentMap()) {
		CreateRemotePlayerSprite(ptr);
	}

	// If the host already started, send GameStart so the client leaves the lobby.
	// Map state is sent below when a map is currently loaded.
	if (net.IsHost() && net.IsGameStarted()) {
		PacketWriter gs(PacketType::GameStart);
		net.SendTo(peer_id, gs, true);
	}

	// Send HostMapReady with position near host (offset +1 tile)
	if (net.IsHost() && net.IsGameStarted() && current_spriteset) {
		auto* hero = Main_Data::game_player.get();
		if (hero) {
			int map_id = Game_Map::GetMapId();
			// Clamp spawn inside the map so the late joiner never starts
			// outside the loaded map (OOB coords crash map lookups).
			int tiles_x = Game_Map::GetTilesX();
			int tiles_y = Game_Map::GetTilesY();
			int x = std::clamp(hero->GetX() + 1, 0, std::max(tiles_x - 1, 0));
			int y = std::clamp(hero->GetY(), 0, std::max(tiles_y - 1, 0));

			PacketWriter pw(PacketType::HostMapReady);
			pw.write(static_cast<int32_t>(map_id));
			pw.write(static_cast<int32_t>(x));
			pw.write(static_cast<int32_t>(y));

			auto actors = Main_Data::game_party->GetActors();
			uint16_t actor_count = 0;
			for (auto* a : actors) { if (a) actor_count++; }
			pw.write(actor_count);
			for (auto* actor : actors) {
				if (!actor) continue;
				pw.write(static_cast<uint16_t>(actor->GetId()));
				pw.write(static_cast<int32_t>(actor->GetLevel()));
				pw.write(static_cast<int32_t>(actor->GetHp()));
				pw.write(static_cast<int32_t>(actor->GetSp()));
			}

			net.SendTo(peer_id, pw, true);

			// Always send bulk switch and variable packets, including empty snapshots.
			PacketWriter sw_pw(PacketType::SwitchBulkSync);
			if (Main_Data::game_switches) {
				auto& sw = Main_Data::game_switches->GetData();
				sw_pw.write(static_cast<uint16_t>(sw.size()));
				for (bool value : sw) sw_pw.write(static_cast<uint8_t>(value ? 1 : 0));
			} else {
				sw_pw.write(static_cast<uint16_t>(0));
			}
			net.SendTo(peer_id, sw_pw, true);

			PacketWriter var_pw(PacketType::VariableBulkSync);
			if (Main_Data::game_variables) {
				auto& vars = Main_Data::game_variables->GetData();
				var_pw.write(static_cast<uint16_t>(vars.size()));
				for (auto value : vars) var_pw.write(static_cast<int32_t>(value));
			} else {
				var_pw.write(static_cast<uint16_t>(0));
			}
			net.SendTo(peer_id, var_pw, true);

			Output::Debug("Multiplayer: Sent game state to late joiner peer {}", peer_id);
		}
	}

	// Send our skin to the new player if we have one
	if (HasSkin() && !skin_image_data.empty()) {
		PacketWriter skin_pw(PacketType::SkinSet);
		skin_pw.write(net.GetLocalPeerId());
		skin_pw.write(skin_charset_name);
		skin_pw.write(static_cast<int32_t>(skin_char_index));
		skin_pw.write(static_cast<int32_t>(skin_image_data.size()));
		skin_pw.writeBytes(skin_image_data.data(), skin_image_data.size());
		net.SendTo(peer_id, skin_pw, true);
	}

if (net.IsHost() && IsModeActive(MultiplayerMode::Split)) {
		SplitMode::Instance().AssignPeer(peer_id);
	}

	if (net.IsHost() && net.GetMode() == MultiplayerMode::Custom) {
		CustomMode::Instance().SendSettingsTo(peer_id);
	}

	if (net.IsHost()) {
		MultiplayerRadio::Instance().SendQueueTo(peer_id);
	}
}

void MultiplayerState::OnPlayerDisconnected(uint16_t peer_id) {
	RemoveRemotePlayerSprite(peer_id);
	remote_players.erase(peer_id);

	// Clean up peer's skin from cache
	auto skin_it = peer_skin_names.find(peer_id);
	if (skin_it != peer_skin_names.end()) {
		Cache::UnregisterCharset(skin_it->second);
		peer_skin_names.erase(skin_it);
	}

	Output::Debug("Multiplayer: Removed remote player for peer {}", peer_id);

	// If we were spectating the disconnected player, switch target
	if (spectating && spectate_target_id == peer_id) {
		CycleSpectateTarget(1);
		if (spectate_target_id == peer_id || remote_players.empty()) {
			// No one left to spectate — game over
			ExitSpectatorMode();
			Scene::Push(std::make_shared<Scene_Gameover>());
		}
	}
}

void MultiplayerState::OnPacketReceived(uint16_t sender_id, const uint8_t* data, size_t len) {
	if (len < 1) return;

	PacketReader reader(data, len);
	PacketType ptype = reader.readType();

	switch (ptype) {
		case PacketType::PlayerPosition:
			HandlePlayerPosition(sender_id, data, len);
			break;
		case PacketType::SwitchSync:
			HandleSwitchSync(data, len);
			break;
		case PacketType::VariableSync:
			HandleVariableSync(data, len);
			break;
		case PacketType::GodCommand:
			HandleGodCommand(sender_id, data, len);
			break;
		case PacketType::GodAssign:
			HandleGodAssign(data, len);
			break;
		case PacketType::BattleStart:
			HandleBattleStart(sender_id, data, len);
			break;
		case PacketType::BattleJoin:
			HandleBattleJoin(sender_id, data, len);
			break;
		case PacketType::BattleForce:
			HandleBattleForce(data, len);
			break;
		case PacketType::BattleAction:
			HandleBattleAction(data, len);
			break;
		case PacketType::BattleEnd:
			HandleBattleEnd(data, len);
			break;
		case PacketType::EventSync:
			HandleEventSync(data, len);
			break;
		case PacketType::BattleTurnSync:
			HandleBattleTurnSync(data, len);
			break;
		case PacketType::BattleEscapeVote:
			HandleBattleEscapeVote(sender_id, data, len);
			break;
		case PacketType::MapChangeForce:
			HandleMapChangeForce(data, len);
			break;
		case PacketType::ActorStateSync:
			HandleActorStateSync(data, len);
			break;
		case PacketType::EventTriggerSync:
			HandleEventTriggerSync(data, len);
			break;
		case PacketType::EventInputSync:
			HandleEventInputSync(sender_id, data, len);
			break;
		case PacketType::SplitSideAssign:
			SplitMode::Instance().HandlePacket(sender_id, data, len);
			break;
		case PacketType::UnderwaterPocketSync:
			UnderwaterMode::Instance().HandlePacket(sender_id, data, len);
			break;
		case PacketType::RawberrySync:
			HandleRawberrySync(data, len);
			break;
		case PacketType::AsymHunterAssign:
			HandleAsymHunterAssign(data, len);
			break;
		case PacketType::AsymKill:
			HandleAsymKill(data, len);
			break;
		case PacketType::ChatMessage:
			HandleChatMessage(sender_id, data, len);
			break;
		case PacketType::MultiplayerCommand:
			HandleMultiplayerCommand(sender_id, data, len);
			break;
		case PacketType::RadioQueueSync:
		case PacketType::RadioAddRequest:
		case PacketType::RadioCustomBegin:
		case PacketType::RadioCustomChunk:
		case PacketType::RadioCustomComplete:
			MultiplayerRadio::Instance().HandlePacket(sender_id, data, len);
			break;
		case PacketType::VoiceData:
			break;
		case PacketType::SkinSet:
			HandleSkinSet(sender_id, data, len);
			break;
		case PacketType::CustomModeSettings:
			CustomMode::Instance().HandleSettingsPacket(data, len);
			break;
		case PacketType::ChaosEvent:
			CustomMode::Instance().HandleEventPacket(data, len);
			break;
		default:
			break;
	}
}

void MultiplayerState::HandlePlayerPosition(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType(); // Skip type byte

	uint16_t peer_id = reader.readU16();
	int32_t map_id = reader.readI32();
	int32_t x = reader.readI32();
	int32_t y = reader.readI32();
	int32_t direction = reader.readI32();
	int32_t facing = reader.readI32();
	std::string sprite_name = reader.readString();
	int32_t sprite_index = reader.readI32();

	// Use sender_id if this is on the server, or peer_id from packet on client
	uint16_t actual_id = (sender_id != 0) ? sender_id : peer_id;

	auto* rp = GetRemotePlayer(actual_id);
	if (!rp) {
		// Player not yet registered, create them
		auto& net = NetManager::Instance();
		auto* pi = net.FindPeer(actual_id);
		std::string name = pi ? pi->player_name : "Player";
		auto new_rp = std::make_unique<Game_RemotePlayer>(actual_id, name);
		rp = new_rp.get();
		remote_players[actual_id] = std::move(new_rp);
	}

	bool was_on_map = rp->IsOnCurrentMap();
	int old_map_id = rp->GetMapId();
	rp->SetNetworkPosition(map_id, x, y, direction, facing);
	if (!sprite_name.empty()) {
		rp->SetCharacterSprite(sprite_name, sprite_index);
	}
	bool is_on_map = rp->IsOnCurrentMap();

	// Handle map transitions
	if (current_spriteset) {
		if (!was_on_map && is_on_map) {
			CreateRemotePlayerSprite(rp);
		} else if (was_on_map && !is_on_map) {
			RemoveRemotePlayerSprite(actual_id);
		}
	}

	// Chaotix mode: when a remote player changes map, force all players to follow
	auto& net = NetManager::Instance();
	if (net.IsHost() && IsModeActive(MultiplayerMode::Chaotix)) {
		if (old_map_id != map_id && old_map_id > 0 && map_id > 0) {
			// Broadcast MapChangeForce to all clients
			PacketWriter fpw(PacketType::MapChangeForce);
			fpw.write(static_cast<int32_t>(map_id));
			fpw.write(static_cast<int32_t>(x));
			fpw.write(static_cast<int32_t>(y));
			net.Broadcast(fpw, true);
			Output::Debug("Multiplayer: Chaotix map force from peer {} to map {}", actual_id, map_id);

			// Host also follows if not already on that map
			if (Game_Map::GetMapId() != map_id && !forced_map_change) {
				forced_map_change = true;
				forced_map_id = map_id;
				forced_map_x = x;
				forced_map_y = y;
			}
		}
	}
}

void MultiplayerState::HandleSwitchSync(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t switch_id = reader.readI32();
	uint8_t value = reader.readU8();

	if (Main_Data::game_switches) {
		Main_Data::game_switches->Set(switch_id, value != 0);
		Game_Map::SetNeedRefreshForSwitchChange(switch_id);
	}
}

void MultiplayerState::HandleVariableSync(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t var_id = reader.readI32();
	int32_t value = reader.readI32();

	if (Main_Data::game_variables) {
		Main_Data::game_variables->Set(var_id, value);
		Game_Map::SetNeedRefreshForVarChange(var_id);
	}
}

void MultiplayerState::HandleGodCommand(uint16_t sender_id, const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	if (net.GetMode() != MultiplayerMode::GodMode) return;

	// Only the assigned god player can send god commands
	if (sender_id != god_player_id) return;

	PacketReader reader(data, len);
	reader.readType();

	uint8_t cmd_type = reader.readU8();
	switch (cmd_type) {
		case 0: {
			// Set switch
			int32_t id = reader.readI32();
			uint8_t val = reader.readU8();
			if (Main_Data::game_switches) {
				Main_Data::game_switches->Set(id, val != 0);
			}
			break;
		}
		case 1: {
			// Set variable
			int32_t id = reader.readI32();
			int32_t val = reader.readI32();
			if (Main_Data::game_variables) {
				Main_Data::game_variables->Set(id, val);
			}
			break;
		}
		case 2: {
			// Teleport all players
			int32_t map_id = reader.readI32();
			int32_t x = reader.readI32();
			int32_t y = reader.readI32();
			if (Main_Data::game_player) {
				Main_Data::game_player->ReserveTeleport(map_id, x, y, -1, TeleportTarget::eParallelTeleport);
			}
			break;
		}
		case 3: {
			// Change gold
			int32_t amount = reader.readI32();
			if (Main_Data::game_party) {
				if (amount >= 0) {
					Main_Data::game_party->GainGold(amount);
				} else {
					Main_Data::game_party->LoseGold(-amount);
				}
			}
			break;
		}
		case 4: {
			// Full heal all party
			if (Main_Data::game_party) {
				auto actors = Main_Data::game_party->GetActors();
				for (auto* actor : actors) {
					actor->FullHeal();
				}
			}
			break;
		}
		case 5: {
			// Change level of actor
			int32_t actor_id = reader.readI32();
			int32_t new_level = reader.readI32();
			auto* actor = Main_Data::game_actors->GetActor(actor_id);
			if (actor) {
				actor->ChangeLevel(new_level, nullptr);
			}
			break;
		}
		case 6: {
			// Change HP of actor
			int32_t actor_id = reader.readI32();
			int32_t new_hp = reader.readI32();
			auto* actor = Main_Data::game_actors->GetActor(actor_id);
			if (actor) {
				actor->SetHp(new_hp);
			}
			break;
		}
		case 7: {
			// Change SP of actor
			int32_t actor_id = reader.readI32();
			int32_t new_sp = reader.readI32();
			auto* actor = Main_Data::game_actors->GetActor(actor_id);
			if (actor) {
				actor->SetSp(new_sp);
			}
			break;
		}
		case 8: {
			// Give/remove item
			int32_t item_id = reader.readI32();
			int32_t amount = reader.readI32();
			if (Main_Data::game_party) {
				if (amount >= 0) {
					Main_Data::game_party->AddItem(item_id, amount);
				} else {
					Main_Data::game_party->RemoveItem(item_id, -amount);
				}
			}
			break;
		}
		default:
			break;
	}
}

void MultiplayerState::HandleGodAssign(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	if (net.GetMode() != MultiplayerMode::GodMode) return;

	PacketReader reader(data, len);
	reader.readType();

	uint16_t assigned_id = reader.readU16();
	god_player_id = assigned_id;
	is_local_god = (assigned_id == net.GetLocalPeerId());

	// Set is_god flag on the right peer
	for (auto& p : const_cast<std::vector<PeerInfo>&>(net.GetPeers())) {
		p.is_god = (p.peer_id == assigned_id);
	}

	if (is_local_god) {
		Output::Debug("Multiplayer: You are the God player!");
	} else {
		Output::Debug("Multiplayer: God player is peer {}", assigned_id);
	}
}

void MultiplayerState::AssignRandomGod() {
	auto& net = NetManager::Instance();
	if (net.GetMode() != MultiplayerMode::GodMode) return;

	// Build list of all peer IDs including host
	std::vector<uint16_t> all_ids;
	all_ids.push_back(net.GetLocalPeerId()); // host (peer 1)
	for (auto& p : net.GetPeers()) {
		all_ids.push_back(p.peer_id);
	}

	// Pick a random one
	int idx = Rand::GetRandomNumber(0, static_cast<int32_t>(all_ids.size()) - 1);
	uint16_t chosen = all_ids[idx];

	god_player_id = chosen;
	is_local_god = (chosen == net.GetLocalPeerId());

	// Broadcast GodAssign to all clients
	PacketWriter packet(PacketType::GodAssign);
	packet.write(chosen);
	net.Broadcast(packet, true);

	Output::Debug("Multiplayer: Assigned god player to peer {}", chosen);
}

void MultiplayerState::SendGodCommand(uint8_t cmd_type, const std::vector<int32_t>& args) {
	auto& net = NetManager::Instance();
	if (!is_local_god) return;

	PacketWriter packet(PacketType::GodCommand);
	packet.write(cmd_type);
	for (auto arg : args) {
		packet.write(arg);
	}
	net.Broadcast(packet, true);
}

void MultiplayerState::SendLocalPlayerPosition() {
	auto& net = NetManager::Instance();
	if (!Main_Data::game_player) return;

	auto* player = Main_Data::game_player.get();

	// Override sprite with skin if set
	std::string sprite_name;
	int32_t sprite_index;
	if (HasSkin()) {
		sprite_name = "__skin_" + std::to_string(net.GetLocalPeerId());
		sprite_index = skin_char_index;
	} else {
		sprite_name = player->GetSpriteName().empty() ? std::string("") : std::string(player->GetSpriteName());
		sprite_index = player->GetSpriteIndex();
	}

	PacketWriter packet(PacketType::PlayerPosition);
	packet.write(net.GetLocalPeerId());
	packet.write(static_cast<int32_t>(Game_Map::GetMapId()));
	packet.write(static_cast<int32_t>(player->GetX()));
	packet.write(static_cast<int32_t>(player->GetY()));
	packet.write(static_cast<int32_t>(player->GetDirection()));
	packet.write(static_cast<int32_t>(player->GetFacing()));
	packet.write(sprite_name);
	packet.write(sprite_index);

	if (net.IsHost()) {
		net.Broadcast(packet, false); // Unreliable for position updates
	} else {
		net.SendToServer(packet, false);
	}
}

void MultiplayerState::CheckAndSyncSwitches() {
	if (!Main_Data::game_switches) return;

	auto& sw_data = Main_Data::game_switches->GetData();
	auto& net = NetManager::Instance();

	// Initialize tracking on first call
	if (last_switches.empty()) {
		last_switches = sw_data;
		return;
	}

	// Detect changes
	size_t max_size = std::max(sw_data.size(), last_switches.size());
	for (size_t i = 0; i < max_size; ++i) {
		bool cur = (i < sw_data.size()) ? sw_data[i] : false;
		bool prev = (i < last_switches.size()) ? last_switches[i] : false;
		if (cur != prev) {
			PacketWriter packet(PacketType::SwitchSync);
			packet.write(static_cast<int32_t>(i + 1)); // 1-indexed
			packet.write(static_cast<uint8_t>(cur ? 1 : 0));
			net.Broadcast(packet, true);
		}
	}

	last_switches = sw_data;
}

void MultiplayerState::CheckAndSyncVariables() {
	if (!Main_Data::game_variables) return;

	auto& var_data = Main_Data::game_variables->GetData();
	auto& net = NetManager::Instance();

	// Initialize tracking on first call
	if (last_variables.empty()) {
		last_variables = var_data;
		return;
	}

	// Detect changes
	size_t max_size = std::max(var_data.size(), last_variables.size());
	for (size_t i = 0; i < max_size; ++i) {
		int32_t cur = (i < var_data.size()) ? var_data[i] : 0;
		int32_t prev = (i < last_variables.size()) ? last_variables[i] : 0;
		if (cur != prev) {
			PacketWriter packet(PacketType::VariableSync);
			packet.write(static_cast<int32_t>(i + 1)); // 1-indexed
			packet.write(cur);
			net.Broadcast(packet, true);
		}
	}

	last_variables = var_data;
}

void MultiplayerState::CreateRemotePlayerSprite(Game_RemotePlayer* player) {
	if (!current_spriteset || !player || !player->IsOnCurrentMap()) return;

	if (CustomMode::Instance().IsForcedSkinActive()) {
		player->SetCharacterSprite(CustomMode::Instance().GetForcedSkinName(), CustomMode::Instance().GetForcedSkinIndex());
	}

	current_spriteset->AddCharacterSprite(player);
	Output::Debug("Multiplayer: Created sprite for peer {}", player->GetPeerId());
}

void MultiplayerState::RemoveRemotePlayerSprite(uint16_t peer_id) {
	if (!current_spriteset) return;
	auto it = remote_players.find(peer_id);
	if (it != remote_players.end() && it->second) {
		current_spriteset->RemoveCharacterSprites(it->second.get());
	}
	Output::Debug("Multiplayer: Removed sprites for peer {}", peer_id);
}

Game_RemotePlayer* MultiplayerState::GetRemotePlayer(uint16_t peer_id) {
	auto it = remote_players.find(peer_id);
	return (it != remote_players.end()) ? it->second.get() : nullptr;
}

bool MultiplayerState::ShouldInterceptGameOver() {
	if (!active) return false;
	if (IsModeActive(MultiplayerMode::Split) &&
		SplitMode::Instance().HandlePlayerDeath()) {
		return true;
	}
	EnterSpectatorMode();
	return spectating;
}

void MultiplayerState::EnterSpectatorMode() {
	if (spectating) return;

	// Defer spectator entry until an actual map is active. During the
	// late-join bootstrap (wait scene) the party is built member-by-member
	// via Game_Party::AddActor(), which calls Game_Player::ResetGraphic()
	// while the party is still smaller than the player-slot index. Without
	// this guard every joining client would be pushed into spectator mode
	// before their actor is assigned and would stay stuck there.
	if (!current_spriteset) {
		Output::Debug("Multiplayer: Deferring spectator mode (no active map)");
		return;
	}

	// Find first remote player on current map as target
	spectate_target_id = 0;
	for (auto& [id, rp] : remote_players) {
		if (rp && rp->IsOnCurrentMap()) {
			spectate_target_id = id;
			break;
		}
	}

	if (spectate_target_id == 0 && !remote_players.empty()) {
		// No remote player on current map, pick any
		spectate_target_id = remote_players.begin()->first;
	}

	if (spectate_target_id == 0) {
		Output::Debug("Multiplayer: No players to spectate");
		return;
	}

	spectating = true;

	// Hide local player while preserving its appearance for a clean exit.
	if (Main_Data::game_player) {
		spectator_saved_sprite_name = Main_Data::game_player->GetSpriteName();
		spectator_saved_sprite_index = Main_Data::game_player->GetSpriteIndex();
		spectator_saved_transparency = Main_Data::game_player->GetTransparency();
		Main_Data::game_player->SetSpriteGraphic("", 0);
		Main_Data::game_player->SetTransparency(7);
	}

	// Create spectator overlay window
	auto* target = GetRemotePlayer(spectate_target_id);
	std::string target_name = target ? target->GetPlayerName() : "Unknown";
	spectator_window = std::make_unique<Window_Help>(
		0, 0, Player::screen_width, 32);
	spectator_window->SetText(fmt::format("Spectating: {} (Left/Right to switch)", target_name));

	Output::Debug("Multiplayer: Entered spectator mode, watching peer {}", spectate_target_id);
}

void MultiplayerState::SpectatePlayer(uint16_t peer_id) {
	if (peer_id == NetManager::Instance().GetLocalPeerId() || !GetRemotePlayer(peer_id)) return;
	if (!spectating) EnterSpectatorMode();
	if (!spectating) return;
	spectate_target_id = peer_id;
	if (spectator_window) {
		spectator_window->SetText(fmt::format("Spectating: {} (Left/Right to switch)",
			GetRemotePlayer(peer_id)->GetPlayerName()));
	}
}

void MultiplayerState::ExitSpectatorMode() {
	if (!spectating) return;
	spectating = false;
	spectate_target_id = 0;
	if (Main_Data::game_player) {
		Main_Data::game_player->SetSpriteGraphic(
			spectator_saved_sprite_name, spectator_saved_sprite_index);
		Main_Data::game_player->SetTransparency(spectator_saved_transparency);
	}
	spectator_saved_sprite_name.clear();
	spectator_saved_sprite_index = 0;
	spectator_saved_transparency = 0;
	spectator_window.reset();
	Output::Debug("Multiplayer: Exited spectator mode");
}

void MultiplayerState::CycleSpectateTarget(int direction) {
	if (remote_players.empty()) return;

	// Build list of valid targets (on current map preferred)
	std::vector<uint16_t> targets;
	for (auto& [id, rp] : remote_players) {
		if (rp->IsOnCurrentMap()) {
			targets.push_back(id);
		}
	}
	// If no one on current map, use all
	if (targets.empty()) {
		for (auto& [id, rp] : remote_players) {
			targets.push_back(id);
		}
	}
	if (targets.empty()) return;

	// Find current index
	int cur_idx = 0;
	for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
		if (targets[i] == spectate_target_id) {
			cur_idx = i;
			break;
		}
	}

	// Cycle
	cur_idx += direction;
	if (cur_idx < 0) cur_idx = static_cast<int>(targets.size()) - 1;
	if (cur_idx >= static_cast<int>(targets.size())) cur_idx = 0;

	spectate_target_id = targets[cur_idx];

	// Update overlay text
	auto* target = GetRemotePlayer(spectate_target_id);
	if (spectator_window && target) {
		spectator_window->SetText(fmt::format("Spectating: {} (Left/Right to switch)", target->GetPlayerName()));
	}
}

void MultiplayerState::UpdateSpectator() {
	auto* target = GetRemotePlayer(spectate_target_id);
	if (!target) {
		CycleSpectateTarget(1);
		target = GetRemotePlayer(spectate_target_id);
		if (!target) {
			ExitSpectatorMode();
			// All players gone — game over
			Scene::Push(std::make_shared<Scene_Gameover>());
			return;
		}
	}

	// If target is on a different map, teleport spectator to follow them
	if (!target->IsOnCurrentMap()) {
		if (Main_Data::game_player && !Main_Data::game_player->IsPendingTeleport()) {
			int target_map = target->GetMapId();
			int target_x = target->GetX();
			int target_y = target->GetY();
			if (target_map > 0) {
				Main_Data::game_player->ReserveTeleport(
					target_map, target_x, target_y, -1,
					TeleportTarget::eParallelTeleport);
			}
		}
		return;
	}

	// Center camera on spectated player
	if (Main_Data::game_player) {
		auto* player = Main_Data::game_player.get();
		Game_Map::SetPositionX(target->GetSpriteX() - player->GetPanX(), false);
		Game_Map::SetPositionY(target->GetSpriteY() - player->GetPanY(), false);
	}

	// Handle cycling input
	if (Input::IsTriggered(Input::LEFT)) {
		CycleSpectateTarget(-1);
	}
	if (Input::IsTriggered(Input::RIGHT)) {
		CycleSpectateTarget(1);
	}
}

// ---- Battle sync ----

void MultiplayerState::StartCommandBattle(int troop_id, bool announce_network) {
	if (!active || in_battle || spectating || !Main_Data::game_player || !Scene::instance) return;
	if (!lcf::ReaderUtil::GetElement(lcf::Data::troops, troop_id)) return;

	BattleArgs args;
	args.troop_id = troop_id;
	args.first_strike = false;
	args.allow_escape = true;
	args.formation = lcf::rpg::System::BattleFormation_terrain;
	args.condition = lcf::rpg::System::BattleCondition_none;
	Game_Map::SetupBattle(args);
	if (announce_network) {
		OnBattleStarted(args.troop_id, args.terrain_id, args.first_strike, args.allow_escape);
	}
	args.on_battle_end = [](BattleResult result) {
		MultiplayerState::Instance().OnBattleEnded(static_cast<int>(result));
	};
	Scene::instance->SetRequestedScene(Scene_Battle::Create(std::move(args)));
	Output::Debug("Multiplayer: Command started battle, troop={}", troop_id);
}

void MultiplayerState::OnBattleStarted(int troop_id, int terrain_id, bool first_strike, bool allow_escape) {
	auto& net = NetManager::Instance();
	auto mode = net.GetMode();
	// Only sync battles in TeamParty and Chaotix modes
	if (!IsBattleSyncMode()) {
		return;
	}

	in_battle = true;
	battle_troop_id = troop_id;
	battle_terrain_id = terrain_id;
	battle_first_strike = first_strike;
	battle_allow_escape = allow_escape;
	remote_battle_actions.clear();
	turn_sync_received = false;
	local_escape_voted = false;
	local_escape_wants = false;
	remote_escape_voted = false;
	remote_escape_wants = false;
	battle_seed = Rand::GetRandomNumber(0, 0x7FFFFFFF);
	Rand::SeedRandomNumberGenerator(battle_seed);

	PacketWriter pw(PacketType::BattleStart);
	pw.write(net.GetLocalPeerId());
	pw.write(static_cast<int32_t>(troop_id));
	pw.write(static_cast<int32_t>(terrain_id));
	pw.write(static_cast<uint8_t>(first_strike ? 1 : 0));
	pw.write(static_cast<uint8_t>(allow_escape ? 1 : 0));
	pw.write(battle_seed);

	if (net.IsHost()) {
		net.Broadcast(pw, true);

		// In Chaotix mode, also force others into battle
		if (mode == MultiplayerMode::Chaotix) {
			PacketWriter fpw(PacketType::BattleForce);
			fpw.write(static_cast<int32_t>(troop_id));
			fpw.write(static_cast<int32_t>(terrain_id));
			fpw.write(static_cast<uint8_t>(first_strike ? 1 : 0));
			fpw.write(static_cast<uint8_t>(allow_escape ? 1 : 0));
			fpw.write(battle_seed);
			net.Broadcast(fpw, true);
		}
	} else {
		net.SendToServer(pw, true);
	}

	Output::Debug("Multiplayer: Battle started, troop={}, seed={}", troop_id, battle_seed);
}

void MultiplayerState::OnBattleEnded(int result) {
	auto& net = NetManager::Instance();
	auto mode = net.GetMode();
	// Only sync battles in TeamParty and Chaotix modes
	if (!IsBattleSyncMode()) {
		return;
	}

	in_battle = false;
	remote_battle_actions.clear();
	turn_sync_received = false;
	local_escape_voted = false;
	local_escape_wants = false;
	remote_escape_voted = false;
	remote_escape_wants = false;
	remote_battle_ended = false;
	remote_battle_result = 0;

	PacketWriter pw(PacketType::BattleEnd);
	pw.write(net.GetLocalPeerId());
	pw.write(static_cast<uint8_t>(result));

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	Output::Debug("Multiplayer: Battle ended, result={}", result);
}

void MultiplayerState::HandleBattleStart(uint16_t sender_id, const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	auto mode = net.GetMode();
	// Only sync battles in TeamParty and Chaotix modes
	if (!IsBattleSyncMode()) {
		return;
	}

	PacketReader reader(data, len);
	reader.readType();

	uint16_t peer_id = reader.readU16();
	int32_t troop_id = reader.readI32();
	int32_t terrain_id = reader.readI32();
	uint8_t first_strike = reader.readU8();
	uint8_t allow_escape = reader.readU8();
	int32_t seed = reader.readI32();

	// If host, relay to other clients (with seed)
	if (net.IsHost()) {
		PacketWriter rpw(PacketType::BattleStart);
		rpw.write(peer_id);
		rpw.write(troop_id);
		rpw.write(terrain_id);
		rpw.write(first_strike);
		rpw.write(allow_escape);
		rpw.write(seed);
		net.Broadcast(rpw, true);

		// In Chaotix mode, force all into battle
		if (mode == MultiplayerMode::Chaotix) {
			PacketWriter fpw(PacketType::BattleForce);
			fpw.write(troop_id);
			fpw.write(terrain_id);
			fpw.write(first_strike);
			fpw.write(allow_escape);
			fpw.write(seed);
			net.Broadcast(fpw, true);
		}
	}

	battle_troop_id = troop_id;
	battle_terrain_id = terrain_id;
	battle_first_strike = first_strike != 0;
	battle_allow_escape = allow_escape != 0;
	battle_initiator_id = peer_id;
	battle_seed = seed;
	Rand::SeedRandomNumberGenerator(battle_seed);

	if (mode == MultiplayerMode::Single || mode == MultiplayerMode::TeamParty) {
		// Team mode: show invite prompt
		if (!in_battle) {
			pending_battle_invite = true;
			battle_invite_window = std::make_unique<Window_Help>(
				0, Player::screen_height / 2 - 16,
				Player::screen_width, 32);
			battle_invite_window->SetText("A battle started! Press [Enter] to join, [Esc] to skip");
			Output::Debug("Multiplayer: Battle invite received from peer {}, troop={}", peer_id, troop_id);
		}
	}
	// Chaotix force is handled via BattleForce packet
}

void MultiplayerState::HandleBattleJoin(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();
	uint16_t peer_id = reader.readU16();
	Output::Debug("Multiplayer: Peer {} joined the battle", peer_id);
}

void MultiplayerState::HandleBattleForce(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	auto mode = net.GetMode();
	// BattleForce is only used in Chaotix mode
	if (!IsModeActive(MultiplayerMode::Chaotix)) {
		return;
	}

	PacketReader reader(data, len);
	reader.readType();

	int32_t troop_id = reader.readI32();
	int32_t terrain_id = reader.readI32();
	uint8_t first_strike = reader.readU8();
	uint8_t allow_escape = reader.readU8();
	int32_t seed = reader.readI32();

	// Don't force if we're already in battle
	if (in_battle) return;

	// Don't overwrite a pending forced battle
	if (forced_battle) return;

	battle_troop_id = troop_id;
	battle_terrain_id = terrain_id;
	battle_first_strike = first_strike != 0;
	battle_allow_escape = allow_escape != 0;
	battle_seed = seed;
	Rand::SeedRandomNumberGenerator(battle_seed);
	forced_battle = true;
	Output::Debug("Multiplayer: Forced into battle, troop={}, seed={}", troop_id, seed);
}

void MultiplayerState::HandleBattleAction(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	uint16_t actor_id = reader.readU16();
	uint8_t action_type = reader.readU8();
	int32_t target_id = reader.readI32();
	int32_t skill_or_item_id = reader.readI32();

	RemoteBattleAction action;
	action.actor_id = actor_id;
	action.action_type = action_type;
	action.target_id = target_id;
	action.skill_or_item_id = skill_or_item_id;

	remote_battle_actions[actor_id] = action;
	Output::Debug("Multiplayer: Received battle action for actor {}: type={}", actor_id, action_type);
}

void MultiplayerState::HandleBattleEnd(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();
	uint16_t peer_id = reader.readU16();
	uint8_t result = reader.readU8();

	auto& net = NetManager::Instance();
	auto mode = net.GetMode();

// In team/chaotix modes, force local battle to end with the same result
	if (in_battle && IsBattleSyncMode()) {
		remote_battle_ended = true;
		remote_battle_result = static_cast<int>(result);
		Output::Debug("Multiplayer: Remote peer {} battle ended with result={}, forcing local end", peer_id, result);
	} else {
		Output::Debug("Multiplayer: Peer {} ended their battle, result={}", peer_id, result);
	}
}

void MultiplayerState::AcceptBattleInvite() {
	pending_battle_invite = false;
	battle_invite_window.reset();

	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleJoin);
	pw.write(net.GetLocalPeerId());
	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	// Enter battle locally
	in_battle = true;
	remote_battle_actions.clear();

	BattleArgs args;
	args.troop_id = battle_troop_id;
	args.terrain_id = battle_terrain_id;
	args.first_strike = battle_first_strike;
	args.allow_escape = battle_allow_escape;
	args.formation = lcf::rpg::System::BattleFormation_terrain;
	args.condition = lcf::rpg::System::BattleCondition_none;

	Game_Map::SetupBattle(args);
	args.on_battle_end = [](BattleResult result) {
		MultiplayerState::Instance().OnBattleEnded(static_cast<int>(result));
	};

	Scene::instance->SetRequestedScene(Scene_Battle::Create(std::move(args)));
	Output::Debug("Multiplayer: Joining battle, troop={}", battle_troop_id);
}

void MultiplayerState::DeclineBattleInvite() {
	pending_battle_invite = false;
	battle_invite_window.reset();
	Output::Debug("Multiplayer: Declined battle invite");
}

void MultiplayerState::ConsumeForcedBattle() {
	// Don't force a dead/spectating player into battle
	if (spectating) {
		forced_battle = false;
		return;
	}

	// Don't interrupt a running cutscene/event — defer until it finishes
	if (Game_Map::GetInterpreter().IsRunning()) return;

	forced_battle = false;

	in_battle = true;
	remote_battle_actions.clear();

	BattleArgs args;
	args.troop_id = battle_troop_id;
	args.terrain_id = battle_terrain_id;
	args.first_strike = battle_first_strike;
	args.allow_escape = false; // Can't escape forced battle
	args.formation = lcf::rpg::System::BattleFormation_terrain;
	args.condition = lcf::rpg::System::BattleCondition_none;

	Game_Map::SetupBattle(args);
	args.on_battle_end = [](BattleResult result) {
		MultiplayerState::Instance().OnBattleEnded(static_cast<int>(result));
	};

	Scene::instance->SetRequestedScene(Scene_Battle::Create(std::move(args)));
	Output::Debug("Multiplayer: Forced into battle, troop={}", battle_troop_id);
}

void MultiplayerState::UpdateBattleInvite() {
	if (!pending_battle_invite) return;

	if (Input::IsTriggered(Input::DECISION)) {
		if (Main_Data::game_system) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		}
		AcceptBattleInvite();
		return;
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		if (Main_Data::game_system) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		}
		DeclineBattleInvite();
		return;
	}
}

void MultiplayerState::SendBattleAction(uint16_t actor_id, uint8_t action_type, int32_t target_id, int32_t skill_or_item_id) {
	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleAction);
	pw.write(actor_id);
	pw.write(action_type);
	pw.write(target_id);
	pw.write(skill_or_item_id);

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}
}

bool MultiplayerState::HasRemoteAction(uint16_t actor_id) const {
	return remote_battle_actions.count(actor_id) > 0;
}

MultiplayerState::RemoteBattleAction MultiplayerState::ConsumeRemoteAction(uint16_t actor_id) {
	auto it = remote_battle_actions.find(actor_id);
	if (it != remote_battle_actions.end()) {
		RemoteBattleAction action = it->second;
		remote_battle_actions.erase(it);
		return action;
	}
	return {};
}

bool MultiplayerState::IsRemoteActor(Game_Actor* actor) const {
	if (!active) return false;
	auto& net = NetManager::Instance();
	auto mode = net.GetMode();
	if (!IsBattleSyncMode()) return false;

	int local_index = static_cast<int>(net.GetLocalPeerId()) - 1;
	auto actors = Main_Data::game_party->GetActors();
	for (int i = 0; i < static_cast<int>(actors.size()); ++i) {
		if (actors[i] == actor) {
			return i != local_index;
		}
	}
	return false;
}

int MultiplayerState::GetLocalActorIndex() const {
	auto& net = NetManager::Instance();
	return static_cast<int>(net.GetLocalPeerId()) - 1;
}

void MultiplayerState::SyncEventPositions() {
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;

	auto& events = Game_Map::GetEvents();
	if (events.empty()) return;

	int32_t current_map_id = Game_Map::GetMapId();

	// Pack event positions: [map_id:i32][count:u16] then per event [id:u16][x:i32][y:i32][dir:u8][facing:u8][remaining:i32]
	// Split into chunks to avoid oversized packets (max ~50 events per packet)
	static constexpr int MAX_EVENTS_PER_PACKET = 50;

	for (size_t start = 0; start < events.size(); start += MAX_EVENTS_PER_PACKET) {
		size_t end = std::min(start + MAX_EVENTS_PER_PACKET, events.size());
		uint16_t count = 0;

		// Count active events in this chunk
		for (size_t i = start; i < end; ++i) {
			auto& ev = events[i];
			if (ev.GetActivePage() != nullptr) {
				count++;
			}
		}

		if (count == 0) continue;

		PacketWriter pw(PacketType::EventSync);
		pw.write(current_map_id);
		pw.write(count);

		for (size_t i = start; i < end; ++i) {
			auto& ev = events[i];
			if (ev.GetActivePage() == nullptr) continue;

			pw.write(static_cast<uint16_t>(ev.GetId()));
			pw.write(static_cast<int32_t>(ev.GetX()));
			pw.write(static_cast<int32_t>(ev.GetY()));
			pw.write(static_cast<uint8_t>(ev.GetDirection()));
			pw.write(static_cast<uint8_t>(ev.GetFacing()));
			pw.write(static_cast<int32_t>(ev.GetRemainingStep()));
		}

		net.Broadcast(pw, false); // unreliable for performance
	}
}

void MultiplayerState::HandleEventSync(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	// Only clients apply event sync from host
	if (net.IsHost()) return;

	PacketReader reader(data, len);
	reader.readType();

	int32_t map_id = reader.readI32();

	// Ignore event sync for a different map
	if (map_id != Game_Map::GetMapId()) return;

	uint16_t count = reader.readU16();
	if (!reader.ok()) return;

	for (uint16_t i = 0; i < count; ++i) {
		uint16_t event_id = reader.readU16();
		int32_t x = reader.readI32();
		int32_t y = reader.readI32();
		uint8_t direction = reader.readU8();
		uint8_t facing = reader.readU8();
		int32_t remaining_step = reader.readI32();

		Game_Event* ev = Game_Map::GetEvent(event_id);
		if (!ev) continue;

		ev->SetX(x);
		ev->SetY(y);
		ev->SetDirection(direction);
		ev->SetFacing(facing);
		ev->SetRemainingStep(remaining_step);
	}
}

void MultiplayerState::OnEventTriggered(int event_id, bool by_decision_key) {
	auto& net = NetManager::Instance();
	if (!active || !IsModeActive(MultiplayerMode::Chaotix)) return;

	int page_id = 0;
	Game_Event* ev = Game_Map::GetEvent(event_id);
	if (ev && ev->GetActivePage()) {
		page_id = ev->GetActivePage()->ID;
	}

	PacketWriter pw(PacketType::EventTriggerSync);
	uint32_t request_id = 0;
	if (net.IsHost()) {
		const uint32_t command_id = ++next_event_command;
		applied_event_commands.insert(command_id);
		pw.write(command_id);
	} else {
		pw.write(static_cast<uint32_t>(0));
		request_id = (static_cast<uint32_t>(net.GetLocalPeerId()) << 16)
			| (++next_event_request & 0xFFFFu);
		pending_event_requests.insert(request_id);
	}
	pw.write(request_id);
	pw.write(static_cast<uint16_t>(event_id));
	pw.write(static_cast<uint8_t>(by_decision_key ? 1 : 0));
	pw.write(static_cast<int32_t>(page_id));

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}
}

void MultiplayerState::OnEventInput(uint8_t input_type) {
auto& net = NetManager::Instance();
	if (!active || !IsModeActive(MultiplayerMode::Chaotix)) return;
	if (input_type > 1) return;

	PacketWriter pw(PacketType::EventInputSync);
	pw.write(net.GetLocalPeerId());
	pw.write(input_type);
	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}
}

void MultiplayerState::HandleEventTriggerSync(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();

// Process in Chaotix mode or when spectating
	if (!IsModeActive(MultiplayerMode::Chaotix) && !spectating) return;

	PacketReader reader(data, len);
	reader.readType();

	uint32_t command_id = reader.readU32();
	uint32_t request_id = reader.readU32();
	uint16_t event_id = reader.readU16();
	uint8_t by_decision_key = reader.readU8();
	int32_t page_id = reader.readI32();
	if (!reader.ok() || event_id == 0 || page_id < 0) return;

	if (net.IsHost() && command_id == 0) {
		command_id = ++next_event_command;
		PacketWriter rpw(PacketType::EventTriggerSync);
		rpw.write(command_id);
		rpw.write(request_id);
		rpw.write(event_id);
		rpw.write(by_decision_key);
		rpw.write(page_id);
		net.Broadcast(rpw, true);
	}

	if (command_id == 0 || !applied_event_commands.insert(command_id).second) return;
	if (request_id != 0 && pending_event_requests.erase(request_id) != 0) return;

	// Schedule the event for execution locally
	Game_Event* ev = Game_Map::GetEvent(event_id);
	if (!ev) return;

	if (spectating) {
		// Spectators: push directly into interpreter with the specific page,
		// bypassing page condition checks that would fail for the spectator
		auto& interp = Game_Map::GetInterpreter();
		const lcf::rpg::EventPage* ev_page = (page_id > 0) ? ev->GetPage(page_id) : ev->GetActivePage();
		if (ev_page && !ev_page->event_commands.empty()) {
			interp.Push<InterpreterExecutionType::Action>(ev, ev_page);
		}
	} else {
		// Use the page selected by the host. The client's switch/variable
		// refresh can arrive a frame later, so scheduling only the local
		// active page can silently drop the event.
		if (ev->IsWaitingForegroundExecution()) return;
		const lcf::rpg::EventPage* ev_page = (page_id > 0)
			? ev->GetPage(page_id) : ev->GetActivePage();
		if (!ev_page || ev_page->event_commands.empty()) return;

		ev->SetPaused(true);
		Game_Map::GetInterpreter().Push<InterpreterExecutionType::Action>(ev, ev_page);
		Output::Debug("Multiplayer: Pushed host event page {} for event {}", page_id, event_id);
	}
	Output::Debug("Multiplayer: Remote event trigger applied for event {} page {}", event_id, page_id);
}

void MultiplayerState::HandleEventInputSync(uint16_t sender_id, const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	if (!IsModeActive(MultiplayerMode::Chaotix)) return;

	PacketReader reader(data, len);
	reader.readType();
	uint16_t origin_peer_id = reader.readU16();
	uint8_t input_type = reader.readU8();
	if (!reader.ok() || origin_peer_id == 0 || input_type > 1) return;

	if (net.IsHost()) {
		// Only relay input from the peer that sent it. The origin ID lets
		// the sending client ignore the host's echoed broadcast.
		if (sender_id == 0 || sender_id != origin_peer_id) return;
		PacketWriter pw(PacketType::EventInputSync);
		pw.write(origin_peer_id);
		pw.write(input_type);
		net.Broadcast(pw, true);
		return;
	}

	if (origin_peer_id == net.GetLocalPeerId()) return;

	// Network updates run after the map update. Queue the input so it is
	// injected before the next map update instead of being lost by Input::Update.
	pending_event_inputs.push_back(input_type);
}

void MultiplayerState::ApplyPendingEventInput() {
	if (pending_event_inputs.empty()) return;

	auto& net = NetManager::Instance();
	if (!active || net.IsHost() || !IsModeActive(MultiplayerMode::Chaotix)) {
		pending_event_inputs.clear();
		return;
	}

	const uint8_t input_type = pending_event_inputs.front();
	pending_event_inputs.pop_front();
	Input::SimulateButtonPress(input_type == 0 ? Input::DECISION : Input::CANCEL);
}

void MultiplayerState::BroadcastTurnSync(int32_t seed) {
	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleTurnSync);
	pw.write(seed);

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	// Host also applies the sync locally
	turn_sync_seed = seed;
	turn_sync_received = true;
	Output::Debug("Multiplayer: Broadcast turn sync, seed={}", seed);
}

int32_t MultiplayerState::ConsumeTurnSync() {
	turn_sync_received = false;
	return turn_sync_seed;
}

void MultiplayerState::HandleBattleTurnSync(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t seed = reader.readI32();

	auto& net = NetManager::Instance();
	// If host, relay to other clients
	if (net.IsHost()) {
		PacketWriter rpw(PacketType::BattleTurnSync);
		rpw.write(seed);
		net.Broadcast(rpw, true);
	}

	turn_sync_seed = seed;
	turn_sync_received = true;
	Output::Debug("Multiplayer: Received turn sync, seed={}", seed);
}

void MultiplayerState::VoteEscape(bool wants_escape) {
	local_escape_voted = true;
	local_escape_wants = wants_escape;

	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::BattleEscapeVote);
	pw.write(static_cast<uint8_t>(wants_escape ? 1 : 0));

	if (net.IsHost()) {
		net.Broadcast(pw, true);
	} else {
		net.SendToServer(pw, true);
	}

	Output::Debug("Multiplayer: Voted escape={}", wants_escape);
}

bool MultiplayerState::HasEscapeVoteResult() const {
	return local_escape_voted && remote_escape_voted;
}

bool MultiplayerState::IsEscapeApproved() const {
	// In Chaotix/Custom-Chaotix mode, a single escape vote triggers escape
	// for everyone. This prevents a softlock when one player is on auto
	// battle and never casts a vote while the other wants to flee.
	if (IsModeActive(MultiplayerMode::Chaotix)) {
		return local_escape_wants || remote_escape_wants;
	}
	return local_escape_wants && remote_escape_wants;
}

void MultiplayerState::ResetEscapeVote() {
	local_escape_voted = false;
	local_escape_wants = false;
	remote_escape_voted = false;
	remote_escape_wants = false;
}

void MultiplayerState::HandleBattleEscapeVote(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	uint8_t wants = reader.readU8();

	auto& net = NetManager::Instance();
	// If host, relay to other clients
	if (net.IsHost()) {
		PacketWriter rpw(PacketType::BattleEscapeVote);
		rpw.write(wants);
		net.Broadcast(rpw, true);
	}

	remote_escape_voted = true;
	remote_escape_wants = (wants != 0);
	Output::Debug("Multiplayer: Received escape vote={}", wants != 0);
}

void MultiplayerState::HandleMapChangeForce(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	int32_t map_id = reader.readI32();
	int32_t x = reader.readI32();
	int32_t y = reader.readI32();

	// Don't force if we're already on that map or a force is pending, or invalid map
	if (map_id <= 0 || Game_Map::GetMapId() == map_id || forced_map_change) return;

	forced_map_change = true;
	forced_map_id = map_id;
	forced_map_x = x;
	forced_map_y = y;
	Output::Debug("Multiplayer: Forced map change to map={} x={} y={}", map_id, x, y);
}

void MultiplayerState::ConsumeForcedMapChange() {
	if (!forced_map_change) return;

	// When spectating, UpdateSpectator handles map following — skip forced teleport
	if (spectating) {
		forced_map_change = false;
		return;
	}

	// Don't teleport while a cutscene/event is running — defer until it finishes
	if (Game_Map::GetInterpreter().IsRunning()) return;

	if (Main_Data::game_player && !Main_Data::game_player->IsPendingTeleport()) {
		forced_map_change = false;
		Main_Data::game_player->ReserveTeleport(
			forced_map_id, forced_map_x, forced_map_y, -1,
			TeleportTarget::eParallelTeleport);
		Output::Debug("Multiplayer: Consuming forced map change to map={}", forced_map_id);
	}
}

void MultiplayerState::CheckAndSyncActorStates() {
	if (!Main_Data::game_party) return;
	auto& net = NetManager::Instance();
	auto actors = Main_Data::game_party->GetActors();

	for (auto* actor : actors) {
		if (!actor) continue;
		int id = actor->GetId();
		int hp = actor->GetHp();
		int sp = actor->GetSp();
		const auto& states = actor->GetStates();

		auto it = last_actor_states.find(id);
		if (it != last_actor_states.end()) {
			auto& snap = it->second;
			if (snap.hp == hp && snap.sp == sp && snap.states == states) {
				continue; // No change
			}
			snap.hp = hp;
			snap.sp = sp;
			snap.states = states;
		} else {
			last_actor_states[id] = { hp, sp, states };
		}

		// Build packet: type, actor_id, hp, sp, state_count, [state values...]
		PacketWriter pw(PacketType::ActorStateSync);
		pw.write(static_cast<uint16_t>(id));
		pw.write(static_cast<int32_t>(hp));
		pw.write(static_cast<int32_t>(sp));
		pw.write(static_cast<uint16_t>(states.size()));
		for (auto s : states) {
			pw.write(static_cast<uint16_t>(static_cast<uint16_t>(s)));
		}
		net.Broadcast(pw, true);
	}
}

void MultiplayerState::HandleActorStateSync(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	// Only clients apply this (host is the source of truth)
	if (net.IsHost()) return;

	PacketReader reader(data, len);
	reader.readType();

	uint16_t actor_id = reader.readU16();
	int32_t hp = reader.readI32();
	int32_t sp = reader.readI32();
	uint16_t state_count = reader.readU16();
	std::vector<int16_t> states(state_count);
	for (uint16_t i = 0; i < state_count; i++) {
		states[i] = static_cast<int16_t>(reader.readU16());
	}

	if (!Main_Data::game_party) return;

	Game_Actor* actor = Main_Data::game_actors->GetActor(actor_id);
	if (!actor) return;

	actor->SetHp(hp);
	actor->SetSp(sp);

	// Overwrite the states vector directly
	auto& actor_states = actor->GetStates();
	actor_states = states;

	// Ensure death state consistency: if HP is 0, add death state; if HP > 0, remove it
	if (hp <= 0 && !actor->IsDead()) {
		actor->Kill();
	} else if (hp > 0 && actor->IsDead()) {
		actor->RemoveState(1, false); // State 1 is death/KO
	}

	Output::Debug("Multiplayer: Synced actor {} state: hp={} sp={} states={}", actor_id, hp, sp, state_count);
}

void MultiplayerState::CreateRubberBandDrawable() {
	if (rubber_band) return;
	rubber_band = std::make_unique<Drawable_RubberBand>();
	rubber_band->SetVisible(false);
}

void MultiplayerState::DestroyRubberBandDrawable() {
	rubber_band.reset();
}

void MultiplayerState::EnsureDarknessOverlay() {
	if (!darkness_overlay) {
		darkness_overlay = std::make_unique<Drawable_DarknessOverlay>();
		// Restore persistent settings
		darkness_overlay->SetEnabled(lighting_enabled);
		darkness_overlay->SetDarknessLevel(lighting_darkness_level);
		darkness_overlay->SetPlayerLight(lighting_player_radius);
	}
}

void MultiplayerState::SetLightingEnabled(bool v) {
	lighting_enabled = v;
	if (darkness_overlay) {
		darkness_overlay->SetEnabled(v);
	}
}

void MultiplayerState::SetLightingDarknessLevel(uint8_t v) {
	lighting_darkness_level = v;
	if (darkness_overlay) {
		darkness_overlay->SetDarknessLevel(v);
	}
}

void MultiplayerState::SetLightingPlayerRadius(int v) {
	lighting_player_radius = v;
	if (darkness_overlay) {
		darkness_overlay->SetPlayerLight(v);
	}
}

void MultiplayerState::UpdateRubberBand() {
	if (!rubber_band) return;

	// Don't show rubber band or pull when spectating (player is dead/hidden)
	if (spectating) {
		rubber_band->SetVisible(false);
		return;
	}

	auto* hero = Main_Data::game_player.get();
	if (!hero) {
		rubber_band->SetVisible(false);
		return;
	}

	// Find the closest remote player on the same map
	Game_RemotePlayer* closest = nullptr;
	int min_dist_sq = 0;
	for (auto& [id, rp] : remote_players) {
		if (!rp->IsOnCurrentMap()) continue;
		int dx = rp->GetX() - hero->GetX();
		int dy = rp->GetY() - hero->GetY();
		int dist_sq = dx * dx + dy * dy;
		if (!closest || dist_sq < min_dist_sq) {
			closest = rp.get();
			min_dist_sq = dist_sq;
		}
	}

	if (!closest) {
		rubber_band->SetVisible(false);
		return;
	}

	float distance = std::sqrt(static_cast<float>(min_dist_sq));

	// Always show the band between connected players
	rubber_band->SetVisible(true);
	rubber_band->SetEndpoints(hero, closest);

	// Calculate stretch ratio: 0 when close, ramps up past RUBBER_BAND_RANGE
	if (distance <= RUBBER_BAND_RANGE) {
		rubber_band->SetStretchRatio(0.0f);
	} else {
		float range = static_cast<float>(RUBBER_BAND_MAX - RUBBER_BAND_RANGE);
		float stretch = (distance - RUBBER_BAND_RANGE) / range;
		rubber_band->SetStretchRatio(stretch);
	}

	// Apply snap-back pull when beyond max range
	if (distance >= RUBBER_BAND_MAX) {
		ApplyRubberBandPull();
	}
}

bool MultiplayerState::ShouldBlockMovement(int dir) const {
	if (!active) return false;
	if (spectating) return false;

	auto& net = NetManager::Instance();
	if (!net.IsConnected()) return false;
	{
		auto props = (net.GetMode() == MultiplayerMode::Custom)
			? CustomMode::Instance().GetEffectiveProperties()
			: GetModeProperties(net.GetMode());
		if (!props.proximity_required) return false;
	}
	if (in_battle) return false;

	auto* hero = Main_Data::game_player.get();
	if (!hero) return false;

	// Find the closest remote player on the same map
	const Game_RemotePlayer* closest = nullptr;
	int min_dist_sq = 0;
	for (auto& [id, rp] : remote_players) {
		if (!rp->IsOnCurrentMap()) continue;
		int dx = rp->GetX() - hero->GetX();
		int dy = rp->GetY() - hero->GetY();
		int dist_sq = dx * dx + dy * dy;
		if (!closest || dist_sq < min_dist_sq) {
			closest = rp.get();
			min_dist_sq = dist_sq;
		}
	}

	if (!closest) return false;

	float distance = std::sqrt(static_cast<float>(min_dist_sq));
	if (distance < RUBBER_BAND_MAX) return false;

	// At max range: only block movement that increases distance
	int hx = hero->GetX();
	int hy = hero->GetY();
	int nx = hx;
	int ny = hy;

	// Calculate the tile position after the move
	switch (dir) {
		case Game_Character::Up:    ny--; break;
		case Game_Character::Down:  ny++; break;
		case Game_Character::Left:  nx--; break;
		case Game_Character::Right: nx++; break;
		case Game_Character::UpRight:   nx++; ny--; break;
		case Game_Character::DownRight: nx++; ny++; break;
		case Game_Character::DownLeft:  nx--; ny++; break;
		case Game_Character::UpLeft:    nx--; ny--; break;
		default: return false;
	}

	int rdx = closest->GetX() - nx;
	int rdy = closest->GetY() - ny;
	int new_dist_sq = rdx * rdx + rdy * rdy;

	// Block only if the new position is further away than current
	return new_dist_sq > min_dist_sq;
}

void MultiplayerState::ApplyRubberBandPull() {
	auto* hero = Main_Data::game_player.get();
	if (!hero || !hero->IsStopping()) return;

	// Find the closest remote player
	Game_RemotePlayer* closest = nullptr;
	int min_dist_sq = 0;
	for (auto& [id, rp] : remote_players) {
		if (!rp->IsOnCurrentMap()) continue;
		int dx = rp->GetX() - hero->GetX();
		int dy = rp->GetY() - hero->GetY();
		int dist_sq = dx * dx + dy * dy;
		if (!closest || dist_sq < min_dist_sq) {
			closest = rp.get();
			min_dist_sq = dist_sq;
		}
	}

	if (!closest) return;

	int dx = closest->GetX() - hero->GetX();
	int dy = closest->GetY() - hero->GetY();

	// Determine direction toward partner
	int pull_dir = -1;
	if (std::abs(dx) >= std::abs(dy)) {
		// Primarily horizontal
		if (dx > 0 && dy < 0) pull_dir = Game_Character::UpRight;
		else if (dx > 0 && dy > 0) pull_dir = Game_Character::DownRight;
		else if (dx < 0 && dy < 0) pull_dir = Game_Character::UpLeft;
		else if (dx < 0 && dy > 0) pull_dir = Game_Character::DownLeft;
		else if (dx > 0) pull_dir = Game_Character::Right;
		else if (dx < 0) pull_dir = Game_Character::Left;
	} else {
		// Primarily vertical
		if (dy < 0 && dx > 0) pull_dir = Game_Character::UpRight;
		else if (dy < 0 && dx < 0) pull_dir = Game_Character::UpLeft;
		else if (dy > 0 && dx > 0) pull_dir = Game_Character::DownRight;
		else if (dy > 0 && dx < 0) pull_dir = Game_Character::DownLeft;
		else if (dy < 0) pull_dir = Game_Character::Up;
		else if (dy > 0) pull_dir = Game_Character::Down;
	}

	if (pull_dir >= 0) {
		// Force the player to move toward partner (snap-back)
		hero->Move(pull_dir);
	}
}

// ========== Horror Mode ==========

bool MultiplayerState::IsHorrorMode() const {
	auto& net = NetManager::Instance();
	return active && net.IsConnected() && net.GetMode() == MultiplayerMode::DeprecatedHorror;
}

// ========== ASYM Mode ==========

bool MultiplayerState::IsAsymMode() const {
	auto& net = NetManager::Instance();
	return active && net.IsConnected() && net.GetMode() == MultiplayerMode::Asym;
}

void MultiplayerState::InitHorrorMode() {
	// Enable pitch-black darkness with flashlight
	if (darkness_overlay) {
		darkness_overlay->SetDarknessLevel(HORROR_DARKNESS_LEVEL);
		darkness_overlay->SetPlayerLight(HORROR_FLASHLIGHT_RADIUS);
		darkness_overlay->SetEnabled(true);
	}

	horror_flashlight_on = true;
	horror_battery_drain_counter = 0;
	horror_jumpscare_active = false;
	horror_jumpscare_timer = 0;
	horror_jumpscare_sprite.reset();

	// Create battery meter HUD
	if (!horror_battery_window) {
		horror_battery_window = std::make_unique<Window_Help>(
			Player::screen_width - 130, 0, 130, 32);
	}
	horror_battery_window->SetText(fmt::format("Battery: {}%", horror_battery_percent));

	// Spawn Rawberry enemy (host drives AI, clients get position from sync)
	auto& net_init = NetManager::Instance();
	if (net_init.IsHost()) {
		SpawnRawberry();
	} else {
		// Clients create Rawberry for display, but don't spawn yet (position comes from sync)
		rawberry = std::make_unique<Game_Rawberry>();
		CreateRawberrySprite();
	}

	// Play map-based horror music (replaces BGM)
	horror_last_map_id = -1; // Force music update on first frame
	horror_overridden_bgm.clear();
	horror_music_override_delay = 0;
	ApplyHorrorMusicEffect();

	Output::Debug("Horror: Mode initialized, battery={}%", horror_battery_percent);
}

void MultiplayerState::CleanupHorrorMode() {
	horror_battery_percent = 100;
	horror_flashlight_on = true;
	horror_battery_drain_counter = 0;
	horror_battery_window.reset();
	horror_current_map_music.clear();
	horror_last_map_id = -1;
	horror_overridden_bgm.clear();
	horror_music_override_delay = 0;

	// Clean up jumpscare
	horror_jumpscare_active = false;
	horror_jumpscare_timer = 0;
	horror_jumpscare_sprite.reset();

	// Destroy Rawberry (stops BGS)
	if (rawberry) {
		rawberry->StopBGS();
	}
	rawberry.reset();

	// Stop horror music and restore the game's BGM
	if (Main_Data::game_system) {
		Audio().BGM_Stop();
		// Re-play the game's current BGM (restores normal music)
		auto& bgm = Main_Data::game_system->GetCurrentBGM();
		if (!bgm.name.empty() && bgm.name != "(OFF)") {
			Main_Data::game_system->BgmPlay(bgm);
		}
	}
}

void MultiplayerState::ApplyHorrorMusicEffect() {
	int map_id = Game_Map::GetMapId();

	// Determine which horror music to play based on map name
	std::string map_name{Game_Map::GetMapName(map_id)};

	std::string music_name;
	if (map_name.find("House") != std::string::npos) {
		music_name = "HouseBGS";
	} else if (map_name.find("Flame") != std::string::npos ||
	           map_name.find("Fire") != std::string::npos ||
	           map_name.find("Lava") != std::string::npos) {
		music_name = "FlameBGS";
	} else {
		music_name = "OtherBGS";
	}

	// Don't restart if same track is already set and map hasn't changed
	if (music_name == horror_current_map_music && map_id == horror_last_map_id) return;

	// Open from assets/MapMusic/ folder
	auto chaos_fs = FileFinder::ChaosAssets();
	if (!chaos_fs) {
		Output::Debug("Horror: ChaosAssets not available for MapMusic");
		return;
	}

	DirectoryTree::Args args = { FileFinder::MakePath("MapMusic", music_name), FileFinder::MUSIC_TYPES, 1, false };
	auto stream = chaos_fs.OpenFile(args);
	if (!stream) {
		Output::Debug("Horror: Could not find MapMusic/{}", music_name);
		return;
	}

	// Stop whatever is currently on the BGM channel, then play our horror music
	Audio().BGM_Stop();
	Audio().BGM_Play(std::move(stream), 100, 100, 0, 50);
	horror_current_map_music = music_name;
	horror_last_map_id = map_id;

	// Track the current game BGM so we can detect when the game tries to override us
	if (Main_Data::game_system) {
		horror_overridden_bgm = Main_Data::game_system->GetCurrentBGM().name;
	}

	Output::Debug("Horror: Playing map music '{}' for map '{}' (id={})", music_name, map_name, map_id);
}

void MultiplayerState::UpdateHorror() {
	// Handle active jumpscare
	if (horror_jumpscare_active) {
		horror_jumpscare_timer--;
		if (horror_jumpscare_timer <= 0) {
			// Jumpscare over — enter spectator mode or game over
			horror_jumpscare_active = false;
			horror_jumpscare_sprite.reset();
			if (!ShouldInterceptGameOver()) {
				Scene::Push(std::make_shared<Scene_Gameover>());
			}
		}
		return; // Don't update anything else during jumpscare
	}

	// Don't update horror gameplay while spectating (player is dead)
	if (spectating) return;

	// Switch horror music when entering a different map
	if (Game_Map::GetMapId() != horror_last_map_id) {
		horror_current_map_music.clear(); // Force re-play for new map
		ApplyHorrorMusicEffect();
	}

	// Detect game trying to change BGM (via events/map transitions) and re-override
	if (Main_Data::game_system) {
		auto& bgm = Main_Data::game_system->GetCurrentBGM();
		if (!bgm.name.empty() && bgm.name != "(OFF)" && bgm.name != horror_overridden_bgm) {
			horror_overridden_bgm = bgm.name;
			// Game changed BGM — schedule re-override after async load completes
			horror_music_override_delay = 5;
		}
	}
	if (horror_music_override_delay > 0) {
		horror_music_override_delay--;
		if (horror_music_override_delay == 0) {
			// Force re-play our horror music over the game's BGM
			horror_current_map_music.clear();
			ApplyHorrorMusicEffect();
		}
	}

	// Flashlight toggle on key 2
	if (Input::IsTriggered(Input::N2) && horror_battery_percent > 0) {
		horror_flashlight_on = !horror_flashlight_on;
		if (darkness_overlay) {
			if (horror_flashlight_on) {
				darkness_overlay->SetPlayerLight(HORROR_FLASHLIGHT_RADIUS);
			} else {
				darkness_overlay->SetPlayerLight(15);
			}
		}
		Output::Debug("Horror: Flashlight toggled {}", horror_flashlight_on ? "ON" : "OFF");
	}

	// Drain battery over time (only when flashlight is on)
	if (horror_battery_percent > 0 && horror_flashlight_on) {
		horror_battery_drain_counter++;
		if (horror_battery_drain_counter >= HORROR_BATTERY_DRAIN_INTERVAL) {
			horror_battery_drain_counter = 0;
			horror_battery_percent--;

			// Update flashlight radius based on battery
			if (darkness_overlay) {
				if (horror_battery_percent > 20) {
					// Full flashlight
					darkness_overlay->SetPlayerLight(HORROR_FLASHLIGHT_RADIUS);
				} else if (horror_battery_percent > 0) {
					// Flickering / shrinking flashlight
					int reduced_radius = HORROR_FLASHLIGHT_RADIUS * horror_battery_percent / 20;
					// Add flicker when low
					if (horror_battery_percent <= 10 && (horror_battery_drain_counter % 30 < 15)) {
						reduced_radius = reduced_radius / 2;
					}
					darkness_overlay->SetPlayerLight(std::max(reduced_radius, 8));
				}
			}

			// Update HUD
			if (horror_battery_window) {
				horror_battery_window->SetText(fmt::format("Battery: {}%", horror_battery_percent));
			}
		}
	}

	// Flashlight flicker effect when battery is very low (1-10%)
	if (horror_flashlight_on && horror_battery_percent > 0 && horror_battery_percent <= 10 && darkness_overlay) {
		// Random flicker
		int frame_mod = horror_battery_drain_counter % 60;
		if (frame_mod < 5 || (frame_mod > 20 && frame_mod < 23) || (frame_mod > 45 && frame_mod < 47)) {
			darkness_overlay->SetPlayerLight(4); // Nearly off
		} else {
			int reduced = HORROR_FLASHLIGHT_RADIUS * horror_battery_percent / 20;
			darkness_overlay->SetPlayerLight(std::max(reduced, 10));
		}
	}

	// Battery dead — flashlight off but keep minimal ambient visibility
	if (horror_battery_percent <= 0 && horror_flashlight_on) {
		horror_flashlight_on = false;
		if (darkness_overlay) {
			darkness_overlay->SetPlayerLight(15);
		}
		if (horror_battery_window) {
			horror_battery_window->SetText("Battery: DEAD");
		}
		Output::Debug("Horror: Flashlight died!");
	}

	// Add flashlight lights for remote players on the same map
	if (darkness_overlay) {
		darkness_overlay->ClearFixedLights();
		for (auto& [id, rp] : remote_players) {
			if (rp && rp->IsOnCurrentMap()) {
				darkness_overlay->AddFixedLight(
					rp->GetScreenX(), rp->GetScreenY() - 8,
					HORROR_FLASHLIGHT_RADIUS, 255, 255, 245, 220);
			}
		}
	}

	// Update Rawberry enemy (host only runs AI and checks collision)
	auto& net_horror = NetManager::Instance();
	if (rawberry) {
		if (net_horror.IsHost()) {
			// Host runs full AI
			if (rawberry->IsSpawned()) {
				rawberry->UpdateChase();

				// Send Rawberry position to clients periodically
				rawberry_sync_counter++;
				if (rawberry_sync_counter >= RAWBERRY_SYNC_INTERVAL) {
					rawberry_sync_counter = 0;
					SendRawberrySync();
				}

				// Only host checks for collision (client position is latency-delayed)
				if (rawberry->HasCaughtPlayer()) {
					rawberry->ResetCaught();
					Output::Debug("Horror: Rawberry ate the player! Triggering jumpscare.");

					// Play scare sound effect
					auto scare_stream = FileFinder::OpenSound("RawberryScare");
					if (scare_stream) {
						auto se_cache = AudioSeCache::Create(std::move(scare_stream), "RawberryScare");
						if (se_cache) {
							se_cache->GetSeData();
							Audio().SE_Play(std::move(se_cache), 100, 100, 50);
						}
					}

					// Show jumpscare image fullscreen
					horror_jumpscare_sprite = std::make_unique<Sprite>();
					horror_jumpscare_sprite->SetZ(Priority_Maximum);
					auto img_stream = FileFinder::OpenImage("Picture", "RAWBERRY_SCARE");
					if (img_stream) {
						auto bitmap = Bitmap::Create(std::move(img_stream), false);
						if (bitmap) {
							horror_jumpscare_sprite->SetBitmap(bitmap);
							horror_jumpscare_sprite->SetX((Player::screen_width - bitmap->GetWidth()) / 2);
							horror_jumpscare_sprite->SetY((Player::screen_height - bitmap->GetHeight()) / 2);
						}
					}

					// Stop Rawberry BGS and horror music during jumpscare
					rawberry->StopBGS();
					Audio().BGM_Stop();

					// Destroy rawberry so it can't re-trigger the jumpscare
					rawberry.reset();

					horror_jumpscare_active = true;
					horror_jumpscare_timer = HORROR_JUMPSCARE_DURATION;
				}
			}
		} else {
			// Client: just update animation and audio (position from sync, no collision check)
			if (rawberry->IsSpawned()) {
				rawberry->UpdateDisplay();
			}
		}
	}
}

void MultiplayerState::SendRawberrySync() {
	if (!rawberry) return;
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;

	PacketWriter packet(PacketType::RawberrySync);
	packet.write(static_cast<int32_t>(Game_Map::GetMapId()));
	packet.write(static_cast<int32_t>(rawberry->GetX()));
	packet.write(static_cast<int32_t>(rawberry->GetY()));
	packet.write(static_cast<int32_t>(rawberry->GetDirection()));
	packet.write(static_cast<uint8_t>(rawberry->IsSpawned() ? 1 : 0));
	net.Broadcast(packet, false);
}

void MultiplayerState::HandleRawberrySync(const uint8_t* data, size_t len) {
	// Don't process during jumpscare or spectating (rawberry was already destroyed locally)
	if (horror_jumpscare_active || spectating) return;

	PacketReader reader(data, len);
	reader.readType(); // Skip type byte

	int32_t map_id = reader.readI32();
	int32_t x = reader.readI32();
	int32_t y = reader.readI32();
	int32_t direction = reader.readI32();
	uint8_t spawned = reader.readU8();

	if (spawned == 0) {
		// Rawberry was destroyed on host
		if (rawberry) {
			rawberry->StopBGS();
			rawberry.reset();
		}
		return;
	}

	// Only apply if we're on the same map
	if (Game_Map::GetMapId() != map_id) return;

	// Create Rawberry if it doesn't exist yet
	if (!rawberry) {
		rawberry = std::make_unique<Game_Rawberry>();
		CreateRawberrySprite();
	}

	rawberry->SetSyncPosition(x, y, direction);
}

void MultiplayerState::SpawnRawberry() {
	rawberry = std::make_unique<Game_Rawberry>();
	rawberry->SpawnFarFromPlayer();
	CreateRawberrySprite();
}

void MultiplayerState::CreateRawberrySprite() {
	if (!current_spriteset || !rawberry) return;
	current_spriteset->AddCharacterSprite(rawberry.get());
}

// ========== ASYM Mode Implementation ==========

void MultiplayerState::AssignRandomHunter() {
	auto& net = NetManager::Instance();
	if (net.GetMode() != MultiplayerMode::Asym) return;

	// Build list of all peer IDs including host
	std::vector<uint16_t> all_ids;
	all_ids.push_back(net.GetLocalPeerId()); // host
	for (auto& p : net.GetPeers()) {
		all_ids.push_back(p.peer_id);
	}

	// Pick a random hunter
	int idx = Rand::GetRandomNumber(0, static_cast<int32_t>(all_ids.size()) - 1);
	uint16_t chosen = all_ids[idx];

	asym_hunter_id = chosen;
	asym_is_local_hunter = (chosen == net.GetLocalPeerId());

	// Broadcast AsymHunterAssign to all clients
	PacketWriter packet(PacketType::AsymHunterAssign);
	packet.write(chosen);
	net.Broadcast(packet, true);

	Output::Debug("ASYM: Assigned hunter to peer {}", chosen);
}

void MultiplayerState::HandleAsymHunterAssign(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	if (net.GetMode() != MultiplayerMode::Asym) return;

	PacketReader reader(data, len);
	reader.readType();

	uint16_t assigned_id = reader.readU16();
	asym_hunter_id = assigned_id;
	asym_is_local_hunter = (assigned_id == net.GetLocalPeerId());

	if (asym_is_local_hunter) {
		Output::Debug("ASYM: You are the HUNTER!");
	} else {
		Output::Debug("ASYM: Hunter is peer {}. You are a SURVIVOR.", assigned_id);
	}
}

void MultiplayerState::HandleAsymKill(const uint8_t* data, size_t len) {
	auto& net = NetManager::Instance();
	if (net.GetMode() != MultiplayerMode::Asym) return;

	PacketReader reader(data, len);
	reader.readType();

	uint16_t victim_id = reader.readU16();

	// If we are the victim, trigger jumpscare and spectator mode
	if (victim_id == net.GetLocalPeerId()) {
		Output::Debug("ASYM: You were caught by the hunter!");

		// Play scare sound
		auto scare_stream = FileFinder::OpenSound("RawberryScare");
		if (scare_stream) {
			auto se_cache = AudioSeCache::Create(std::move(scare_stream), "RawberryScare");
			if (se_cache) {
				se_cache->GetSeData();
				Audio().SE_Play(std::move(se_cache), 100, 100, 50);
			}
		}

		// Show jumpscare image fullscreen
		horror_jumpscare_sprite = std::make_unique<Sprite>();
		horror_jumpscare_sprite->SetZ(Priority_Maximum);
		auto img_stream = FileFinder::OpenImage("Picture", "RAWBERRY_SCARE");
		if (img_stream) {
			auto bitmap = Bitmap::Create(std::move(img_stream), false);
			if (bitmap) {
				horror_jumpscare_sprite->SetBitmap(bitmap);
				horror_jumpscare_sprite->SetX((Player::screen_width - bitmap->GetWidth()) / 2);
				horror_jumpscare_sprite->SetY((Player::screen_height - bitmap->GetHeight()) / 2);
			}
		}

		// Stop horror music during jumpscare
		Audio().BGM_Stop();

		horror_jumpscare_active = true;
		horror_jumpscare_timer = HORROR_JUMPSCARE_DURATION;
	} else {
		Output::Debug("ASYM: Peer {} was caught by the hunter", victim_id);
	}
}

void MultiplayerState::HandleChatMessage(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType(); // skip packet type
	if (!reader.ok()) return;

	uint16_t message_sender_id = reader.readU16();
	uint8_t chat_type = reader.readU8(); // 0 = normal, 1 = dialogue
	std::string text = reader.readString();
	if (!reader.ok() || chat_type > 1 || text.size() > 80) return;

	// Relayed client packets arrive from the server, so the network callback's
	// sender ID is not the player who originally sent the message.
	if (sender_id != 0 && sender_id != message_sender_id) {
		message_sender_id = sender_id;
	}

	// Look up sender name
	std::string sender_name;
	auto* peer = NetManager::Instance().FindPeer(message_sender_id);
	if (peer && !peer->player_name.empty()) {
		sender_name = peer->player_name;
	} else {
		auto* remote = GetRemotePlayer(message_sender_id);
		if (remote) sender_name = remote->GetPlayerName();
	}
	if (sender_name.empty()) {
		sender_name = "Peer " + std::to_string(message_sender_id);
	}

	MultiplayerChat::Instance().OnChatMessageReceived(
		message_sender_id, sender_name, text, chat_type == 1);
}

void MultiplayerState::HandleMultiplayerCommand(uint16_t sender_id, const uint8_t* data, size_t len) {
	MultiplayerCommands::HandlePacket(sender_id, data, len);
}

void MultiplayerState::InitAsymMode() {
	// Re-apply hunter sprite every map load (map changes reset the graphic)
	if (asym_is_local_hunter && Main_Data::game_player) {
		Main_Data::game_player->SetSpriteGraphic("Chara1", 7);
	}

	// Hunter: always ensure darkness is off
	if (asym_is_local_hunter && darkness_overlay) {
		darkness_overlay->SetEnabled(false);
	}

	// One-time initialization
	if (asym_initialized) return;
	asym_initialized = true;

	// Survivors get darkness + flashlight (same as horror)
	if (!asym_is_local_hunter) {
		if (darkness_overlay) {
			darkness_overlay->SetDarknessLevel(HORROR_DARKNESS_LEVEL);
			darkness_overlay->SetPlayerLight(HORROR_FLASHLIGHT_RADIUS);
			darkness_overlay->SetEnabled(true);
		}

		horror_flashlight_on = true;
		horror_battery_drain_counter = 0;

		// Battery HUD
		if (!horror_battery_window) {
			horror_battery_window = std::make_unique<Window_Help>(
				Player::screen_width - 130, 0, 130, 32);
		}
		horror_battery_window->SetText(fmt::format("Battery: {}%", horror_battery_percent));
	}

	// Play horror music for everyone
	horror_last_map_id = -1;
	horror_overridden_bgm.clear();
	horror_music_override_delay = 0;
	ApplyHorrorMusicEffect();

	Output::Debug("ASYM: Mode initialized (hunter={})", asym_is_local_hunter ? "true" : "false");
}

void MultiplayerState::CleanupAsymMode() {
	asym_is_local_hunter = false;
	asym_hunter_id = 0;
	asym_initialized = false;

	// Clean up shared horror state used by ASYM survivors
	horror_battery_percent = 100;
	horror_flashlight_on = true;
	horror_battery_drain_counter = 0;
	horror_battery_window.reset();
	horror_current_map_music.clear();
	horror_last_map_id = -1;
	horror_overridden_bgm.clear();
	horror_music_override_delay = 0;
	horror_jumpscare_active = false;
	horror_jumpscare_timer = 0;
	horror_jumpscare_sprite.reset();

	// Restore BGM
	if (Main_Data::game_system) {
		Audio().BGM_Stop();
		auto& bgm = Main_Data::game_system->GetCurrentBGM();
		if (!bgm.name.empty() && bgm.name != "(OFF)") {
			Main_Data::game_system->BgmPlay(bgm);
		}
	}
}

void MultiplayerState::UpdateAsym() {
	// Handle active jumpscare (survivor caught)
	if (horror_jumpscare_active) {
		horror_jumpscare_timer--;
		if (horror_jumpscare_timer <= 0) {
			horror_jumpscare_active = false;
			horror_jumpscare_sprite.reset();
			if (!ShouldInterceptGameOver()) {
				Scene::Push(std::make_shared<Scene_Gameover>());
			}
		}
		return;
	}

	// Don't update while spectating
	if (spectating) return;

	// Switch horror music when entering a different map
	if (Game_Map::GetMapId() != horror_last_map_id) {
		horror_current_map_music.clear();
		ApplyHorrorMusicEffect();
	}

	// Detect game trying to change BGM and re-override
	if (Main_Data::game_system) {
		auto& bgm = Main_Data::game_system->GetCurrentBGM();
		if (!bgm.name.empty() && bgm.name != "(OFF)" && bgm.name != horror_overridden_bgm) {
			horror_overridden_bgm = bgm.name;
			horror_music_override_delay = 5;
		}
	}
	if (horror_music_override_delay > 0) {
		horror_music_override_delay--;
		if (horror_music_override_delay == 0) {
			horror_current_map_music.clear();
			ApplyHorrorMusicEffect();
		}
	}

	// === Survivor-only logic ===
	if (!asym_is_local_hunter) {
		// Flashlight toggle
		if (Input::IsTriggered(Input::N2) && horror_battery_percent > 0) {
			horror_flashlight_on = !horror_flashlight_on;
			if (darkness_overlay) {
				darkness_overlay->SetPlayerLight(horror_flashlight_on ? HORROR_FLASHLIGHT_RADIUS : 15);
			}
		}

		// Battery drain
		if (horror_battery_percent > 0 && horror_flashlight_on) {
			horror_battery_drain_counter++;
			if (horror_battery_drain_counter >= HORROR_BATTERY_DRAIN_INTERVAL) {
				horror_battery_drain_counter = 0;
				horror_battery_percent--;

				if (darkness_overlay) {
					if (horror_battery_percent > 20) {
						darkness_overlay->SetPlayerLight(HORROR_FLASHLIGHT_RADIUS);
					} else if (horror_battery_percent > 0) {
						int reduced_radius = HORROR_FLASHLIGHT_RADIUS * horror_battery_percent / 20;
						darkness_overlay->SetPlayerLight(std::max(reduced_radius, 8));
					}
				}

				if (horror_battery_window) {
					horror_battery_window->SetText(fmt::format("Battery: {}%", horror_battery_percent));
				}
			}
		}

		// Flicker effect
		if (horror_flashlight_on && horror_battery_percent > 0 && horror_battery_percent <= 10 && darkness_overlay) {
			int frame_mod = horror_battery_drain_counter % 60;
			if (frame_mod < 5 || (frame_mod > 20 && frame_mod < 23) || (frame_mod > 45 && frame_mod < 47)) {
				darkness_overlay->SetPlayerLight(4);
			} else {
				int reduced = HORROR_FLASHLIGHT_RADIUS * horror_battery_percent / 20;
				darkness_overlay->SetPlayerLight(std::max(reduced, 10));
			}
		}

		// Battery dead
		if (horror_battery_percent <= 0 && horror_flashlight_on) {
			horror_flashlight_on = false;
			if (darkness_overlay) {
				darkness_overlay->SetPlayerLight(15);
			}
			if (horror_battery_window) {
				horror_battery_window->SetText("Battery: DEAD");
			}
		}

		// Add remote player flashlight lights (survivors see each other's lights)
		if (darkness_overlay) {
			darkness_overlay->ClearFixedLights();
			for (auto& [id, rp] : remote_players) {
				if (rp && rp->IsOnCurrentMap() && id != asym_hunter_id) {
					darkness_overlay->AddFixedLight(
						rp->GetScreenX(), rp->GetScreenY() - 8,
						HORROR_FLASHLIGHT_RADIUS, 255, 255, 245, 220);
				}
			}
		}
	}

	// === Hunter collision check (host validates) ===
	auto& net = NetManager::Instance();
	if (net.IsHost()) {
		AsymCheckHunterCollision();
	}
}

void MultiplayerState::AsymCheckHunterCollision() {
	auto& net = NetManager::Instance();

	// Get hunter position
	int hunter_x, hunter_y, hunter_map;

	if (asym_is_local_hunter) {
		// Host is the hunter
		auto* hero = Main_Data::game_player.get();
		if (!hero) return;
		hunter_x = hero->GetX();
		hunter_y = hero->GetY();
		hunter_map = Game_Map::GetMapId();
	} else {
		// Hunter is a remote player
		auto* hunter = GetRemotePlayer(asym_hunter_id);
		if (!hunter || !hunter->IsOnCurrentMap()) return;
		hunter_x = hunter->GetX();
		hunter_y = hunter->GetY();
		hunter_map = Game_Map::GetMapId();
	}

	// Check each survivor (non-hunter player)
	// Check local player if they're a survivor
	if (!asym_is_local_hunter && !spectating) {
		auto* hero = Main_Data::game_player.get();
		if (hero && hero->GetX() == hunter_x && hero->GetY() == hunter_y) {
			// Local player caught!
			Output::Debug("ASYM: Hunter caught local player!");
			PacketWriter packet(PacketType::AsymKill);
			packet.write(net.GetLocalPeerId());
			net.Broadcast(packet, true);
			// Also trigger locally
			HandleAsymKill(packet.data(), packet.size());
			return;
		}
	}

	// Check remote survivors
	for (auto& [id, rp] : remote_players) {
		if (!rp || !rp->IsOnCurrentMap()) continue;
		if (id == asym_hunter_id) continue; // Skip hunter

		if (rp->GetX() == hunter_x && rp->GetY() == hunter_y) {
			// Remote survivor caught!
			Output::Debug("ASYM: Hunter caught peer {}!", id);
			PacketWriter packet(PacketType::AsymKill);
			packet.write(id);
			net.Broadcast(packet, true);
			return;
		}
	}
}

// ==================== Player Skins ====================

void MultiplayerState::SetSkin(const std::string& charset_name, int char_index, const std::vector<uint8_t>& image_data) {
	skin_charset_name = charset_name;
	skin_char_index = char_index;
	skin_image_data = image_data;
	skin_sent_this_session = false;

	// Register in cache for local rendering
	if (!image_data.empty()) {
		BitmapRef bmp = Bitmap::Create(image_data.data(), static_cast<unsigned>(image_data.size()), true);
		if (bmp) {
			Cache::RegisterCharset(kLocalSkinCacheName, bmp);
			Output::Debug("Skin: Registered local skin '{}' index {} as '{}'", charset_name, char_index, kLocalSkinCacheName);
		}
	}

	if (Main_Data::game_player) {
		Main_Data::game_player->SetSpriteGraphic(kLocalSkinCacheName, skin_char_index);
	}
}

void MultiplayerState::ClearSkin() {
	if (!skin_charset_name.empty()) {
		Cache::UnregisterCharset(kLocalSkinCacheName);
	}
	skin_charset_name.clear();
	skin_char_index = 0;
	skin_image_data.clear();
	skin_sent_this_session = false;
	if (Main_Data::game_player) {
		Main_Data::game_player->ResetGraphic();
	}
}

void MultiplayerState::BroadcastSkin() {
	if (!active || skin_image_data.empty()) return;

	auto& net = NetManager::Instance();
	PacketWriter packet(PacketType::SkinSet);
	packet.write(net.GetLocalPeerId());
	packet.write(skin_charset_name);
	packet.write(static_cast<int32_t>(skin_char_index));
	packet.write(static_cast<int32_t>(skin_image_data.size()));
	packet.writeBytes(skin_image_data.data(), skin_image_data.size());

	if (net.IsHost()) {
		net.Broadcast(packet, true);
	} else {
		net.SendToServer(packet, true);
	}
	skin_sent_this_session = true;
	Output::Debug("Skin: Broadcast skin '{}' index {} ({} bytes)", skin_charset_name, skin_char_index, skin_image_data.size());
}

void MultiplayerState::HandleSkinSet(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	uint16_t peer_id = reader.readU16();
	std::string charset_name = reader.readString();
	int32_t char_index = reader.readI32();
	int32_t data_size = reader.readI32();

	if (data_size <= 0 || data_size > 1024 * 1024) {
		Output::Debug("Skin: Invalid skin data size {} from peer {}", data_size, peer_id);
		return;
	}

	const uint8_t* img_data = reader.readBytes(static_cast<size_t>(data_size));
	if (!img_data) {
		Output::Debug("Skin: Incomplete skin data from peer {}", peer_id);
		return;
	}

	uint16_t actual_id = (sender_id != 0) ? sender_id : peer_id;

	// Decode bitmap
	BitmapRef bmp = Bitmap::Create(img_data, static_cast<unsigned>(data_size), true);
	if (!bmp) {
		Output::Debug("Skin: Failed to decode skin bitmap from peer {}", actual_id);
		return;
	}

	// Register in cache with the expected name
	std::string cache_name = "__skin_" + std::to_string(actual_id);
	Cache::RegisterCharset(cache_name, bmp);
	peer_skin_names[actual_id] = cache_name;
	auto* rp = GetRemotePlayer(actual_id);
	if (rp) {
		rp->SetCharacterSprite(cache_name, char_index);
	}

	Output::Debug("Skin: Received skin '{}' index {} from peer {} ({} bytes)",
		charset_name, char_index, actual_id, data_size);

	// If we're the host, forward to all other clients
	auto& net = NetManager::Instance();
	if (net.IsHost()) {
		PacketWriter fwd(PacketType::SkinSet);
		fwd.write(actual_id);
		fwd.write(charset_name);
		fwd.write(static_cast<int32_t>(char_index));
		fwd.write(static_cast<int32_t>(data_size));
		fwd.writeBytes(img_data, static_cast<size_t>(data_size));
		net.Broadcast(fwd, true);
	}
}

} // namespace Chaos
