/*
 * Chaos Fork: Multiplayer State Manager
 * Central coordinator for multiplayer game state synchronization.
 */

#ifndef EP_CHAOS_MULTIPLAYER_STATE_H
#define EP_CHAOS_MULTIPLAYER_STATE_H

#include <map>
#include <memory>
#include <deque>
#include <set>
#include <vector>
#include "chaos/multiplayer_mode.h"
#include "chaos/game_multiplayer.h"
#include "chaos/game_rawberry.h"
#include "chaos/drawable_rubber_band.h"
#include "chaos/drawable_darkness_overlay.h"
#include "chaos/gamemode_split_mode.h"
#include "chaos/gamemode_underwater_mode.h"

class Game_Actor;
class Spriteset_Map;
class Window_Help;
class Sprite;

namespace Chaos {

class MultiplayerState {
public:
	static MultiplayerState& Instance();

	/** Called each frame from the main loop when multiplayer is active */
	void Update();

	/** Called when entering a map scene */
	void OnMapLoaded(Spriteset_Map* spriteset);

	/** Called when leaving a map scene */
	void OnMapUnloaded();

	/** Enable multiplayer (called when game starts in MP mode) */
	void StartMultiplayer();

	/** Disable multiplayer */
	void StopMultiplayer();

	/** Whether multiplayer is currently active */
	bool IsActive() const { return active; }

	/** Whether the host has disconnected (for clients) */
	bool IsHostDisconnected() const { return host_lost; }
	void ClearHostDisconnected() { host_lost = false; }

	/** Get a remote player by peer ID */
	Game_RemotePlayer* GetRemotePlayer(uint16_t peer_id);

	/** Get all remote players */
	const std::map<uint16_t, std::unique_ptr<Game_RemotePlayer>>& GetRemotePlayers() const {
		return remote_players;
	}

	/** Spectator mode */
	bool IsSpectating() const { return spectating; }
	void EnterSpectatorMode();
	void ExitSpectatorMode();
	void CycleSpectateTarget(int direction);

	/** Called from game over triggers - returns true if multiplayer is active and spectator mode was entered */
	bool ShouldInterceptGameOver();

	/** Battle sync */
	void OnBattleStarted(int troop_id, int terrain_id, bool first_strike, bool allow_escape);
	void OnBattleEnded(int result);
	bool IsInMultiplayerBattle() const { return in_battle; }
	bool HasPendingBattleInvite() const { return pending_battle_invite; }
	bool HasForcedBattle() const { return forced_battle; }

	/** Chaotix map sync */
	bool HasForcedMapChange() const { return forced_map_change; }
	void ConsumeForcedMapChange();
	int GetBattleTroopId() const { return battle_troop_id; }
	int GetBattleTerrainId() const { return battle_terrain_id; }
	bool GetBattleFirstStrike() const { return battle_first_strike; }
	bool GetBattleAllowEscape() const { return battle_allow_escape; }
	void AcceptBattleInvite();
	void DeclineBattleInvite();
	void ConsumeForcedBattle();
	int32_t GetBattleSeed() const { return battle_seed; }

	/** Battle action sync */
	struct RemoteBattleAction {
		uint16_t actor_id = 0;
		uint8_t action_type = 0; // 0=none, 1=attack, 2=skill, 3=item, 4=defend
		int32_t target_id = 0;
		int32_t skill_or_item_id = 0;
	};
	void SendBattleAction(uint16_t actor_id, uint8_t action_type, int32_t target_id, int32_t skill_or_item_id);
	bool HasRemoteAction(uint16_t actor_id) const;
	RemoteBattleAction ConsumeRemoteAction(uint16_t actor_id);
	bool IsRemoteActor(Game_Actor* actor) const;
	int GetLocalActorIndex() const;

	/** Battle turn sync - ensures both clients enter execution phase together */
	void BroadcastTurnSync(int32_t seed);
	bool HasTurnSync() const { return turn_sync_received; }
	int32_t ConsumeTurnSync();

	/** Escape voting - both players must choose escape for it to proceed */
	void VoteEscape(bool wants_escape);
	bool HasEscapeVoteResult() const;
	bool IsEscapeApproved() const;
	void ResetEscapeVote();

	/** Remote battle end sync - force local battle to end when remote ends */
	bool HasRemoteBattleEnded() const { return remote_battle_ended; }
	int GetRemoteBattleResult() const { return remote_battle_result; }
	void ConsumeRemoteBattleEnd() { remote_battle_ended = false; }

	/** Chaotix rubber band - returns true if player movement should be blocked (too far from partner) */
	bool ShouldBlockMovement(int dir) const;

	/** Chaotix event trigger sync - called when local player triggers an event */
	void OnEventTriggered(int event_id, bool by_decision_key);

	/** Sync a host confirmation/cancel input while a Chaotix event message is open */
	void OnEventInput(uint8_t input_type);

	/** Apply queued host event input before the next map update */
	void ApplyPendingEventInput();

	/** God Mode - whether local player is the god */
	bool IsLocalGod() const { return is_local_god; }
	uint16_t GetGodPlayerId() const { return god_player_id; }
	void AssignRandomGod();
	void SendGodCommand(uint8_t cmd_type, const std::vector<int32_t>& args);

	/** Horror Mode */
	bool IsHorrorMode() const;
	int GetBatteryPercent() const { return horror_battery_percent; }
	bool IsFlashlightOn() const { return horror_flashlight_on; }
	void ApplyHorrorMusicEffect();
	Game_Rawberry* GetRawberry() { return rawberry.get(); }

	/** ASYM Mode - asymmetric horror: one player is the hunter */
	bool IsAsymMode() const;
	bool IsLocalHunter() const { return asym_is_local_hunter; }
	uint16_t GetHunterPlayerId() const { return asym_hunter_id; }
	void AssignRandomHunter();

	/** Darkness/lighting overlay - accessible for game-driven configuration */
	Drawable_DarknessOverlay* GetDarknessOverlay() { return darkness_overlay.get(); }

	/** Ensure the darkness overlay exists (called from Spriteset_Map even in singleplayer) */
	void EnsureDarknessOverlay();

	/** Persistent lighting settings (work even when overlay doesn't exist yet) */
	bool IsLightingEnabled() const { return lighting_enabled; }
	void SetLightingEnabled(bool v);
	uint8_t GetLightingDarknessLevel() const { return lighting_darkness_level; }
	void SetLightingDarknessLevel(uint8_t v);
	int GetLightingPlayerRadius() const { return lighting_player_radius; }
	void SetLightingPlayerRadius(int v);

	/** Player Skins - custom multiplayer appearance */
	bool HasSkin() const { return !skin_charset_name.empty(); }
	const std::string& GetSkinCharsetName() const { return skin_charset_name; }
	int GetSkinCharIndex() const { return skin_char_index; }
	void SetSkin(const std::string& charset_name, int char_index, const std::vector<uint8_t>& image_data);
	void ClearSkin();
	void BroadcastSkin();
	const std::vector<uint8_t>& GetSkinImageData() const { return skin_image_data; }

private:
	MultiplayerState() = default;

	void SetupCallbacks();

	// Network event handlers
	void OnPlayerConnected(uint16_t peer_id);
	void OnPlayerDisconnected(uint16_t peer_id);
	void OnPacketReceived(uint16_t sender_id, const uint8_t* data, size_t len);

	// Packet handlers
	void HandlePlayerPosition(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleSwitchSync(const uint8_t* data, size_t len);
	void HandleVariableSync(const uint8_t* data, size_t len);
	void HandleGodCommand(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleGodAssign(const uint8_t* data, size_t len);
	void HandleBattleStart(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleBattleJoin(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleBattleForce(const uint8_t* data, size_t len);
	void HandleBattleAction(const uint8_t* data, size_t len);
	void HandleBattleEnd(const uint8_t* data, size_t len);
	void HandleEventSync(const uint8_t* data, size_t len);
	void HandleBattleTurnSync(const uint8_t* data, size_t len);
	void HandleBattleEscapeVote(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleMapChangeForce(const uint8_t* data, size_t len);
	void HandleActorStateSync(const uint8_t* data, size_t len);
	void HandleEventTriggerSync(const uint8_t* data, size_t len);
	void HandleEventInputSync(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleRawberrySync(const uint8_t* data, size_t len);
	void HandleAsymHunterAssign(const uint8_t* data, size_t len);
	void HandleAsymKill(const uint8_t* data, size_t len);
	void HandleChatMessage(uint16_t sender_id, const uint8_t* data, size_t len);
	void HandleSkinSet(uint16_t sender_id, const uint8_t* data, size_t len);

	// Sync functions
	void SendLocalPlayerPosition();
	void CheckAndSyncSwitches();
	void CheckAndSyncVariables();
	void SyncEventPositions();
	void CheckAndSyncActorStates();
	void SendRawberrySync();

	// Sprite management
	void CreateRemotePlayerSprite(Game_RemotePlayer* player);
	void RemoveRemotePlayerSprite(uint16_t peer_id);

	// Spectator
	void UpdateSpectator();
	void UpdateBattleInvite();

	// Chaotix rubber band
	void UpdateRubberBand();
	void CreateRubberBandDrawable();
	void DestroyRubberBandDrawable();
	void ApplyRubberBandPull();

	// Horror mode
	void UpdateHorror();
	void InitHorrorMode();
	void CleanupHorrorMode();
	void SpawnRawberry();
	void CreateRawberrySprite();

	// ASYM mode
	void UpdateAsym();
	void InitAsymMode();
	void CleanupAsymMode();
	void AsymCheckHunterCollision();

	bool active = false;
	bool host_lost = false;
	Spriteset_Map* current_spriteset = nullptr;

	// Remote players indexed by peer_id
	std::map<uint16_t, std::unique_ptr<Game_RemotePlayer>> remote_players;

	// Spectator state
	bool spectating = false;
	uint16_t spectate_target_id = 0;
	std::unique_ptr<Window_Help> spectator_window;
	std::string spectator_saved_sprite_name;
	int spectator_saved_sprite_index = 0;
	int spectator_saved_transparency = 0;
	std::set<uint32_t> applied_event_commands;
	std::set<uint32_t> pending_event_requests;
	std::deque<uint8_t> pending_event_inputs;
	uint32_t next_event_command = 0;
	uint32_t next_event_request = 0;

	// Battle state
	bool in_battle = false;
	bool pending_battle_invite = false;
	bool forced_battle = false;
	int battle_troop_id = 0;
	int battle_terrain_id = 0;
	bool battle_first_strike = false;
	bool battle_allow_escape = true;
	uint16_t battle_initiator_id = 0;
	int32_t battle_seed = 0;
	std::unique_ptr<Window_Help> battle_invite_window;
	std::map<uint16_t, RemoteBattleAction> remote_battle_actions;

	// Turn sync state
	bool turn_sync_received = false;
	int32_t turn_sync_seed = 0;

	// Escape vote state
	bool local_escape_voted = false;
	bool local_escape_wants = false;
	bool remote_escape_voted = false;
	bool remote_escape_wants = false;

	// Remote battle end sync
	bool remote_battle_ended = false;
	int remote_battle_result = 0; // maps to BattleResult enum

	// Chaotix forced map change
	bool forced_map_change = false;
	int forced_map_id = 0;
	int forced_map_x = 0;
	int forced_map_y = 0;

	// Host: last map id whose snapshot was announced, to avoid re-broadcasting
	// the full snapshot on every spriteset refresh (only on actual map changes).
	int last_announced_map_id = -1;

	// For detecting switch/variable changes
	std::vector<bool> last_switches;
	std::vector<int32_t> last_variables;

	// For detecting actor state changes (chaotix sync)
	struct ActorSnapshot {
		int hp = 0;
		int sp = 0;
		std::vector<int16_t> states;
	};
	std::map<int, ActorSnapshot> last_actor_states;
	int actor_state_sync_counter = 0;
	static constexpr int ACTOR_STATE_SYNC_INTERVAL = 10; // Send every 10 frames

	// Rate limiting for position updates
	int position_send_counter = 0;
	static constexpr int POSITION_SEND_INTERVAL = 3; // Send every 3 frames
	int event_sync_counter = 0;
	static constexpr int EVENT_SYNC_INTERVAL = 6; // Send every 6 frames
	int rawberry_sync_counter = 0;
	static constexpr int RAWBERRY_SYNC_INTERVAL = 3; // Send every 3 frames

	// Chaotix rubber band
	std::unique_ptr<Drawable_RubberBand> rubber_band;
	static constexpr int RUBBER_BAND_RANGE = 5;  // tiles before band starts stretching
	static constexpr int RUBBER_BAND_MAX = 8;    // tiles max distance (blocks movement)

	// God Mode state
	bool is_local_god = false;
	uint16_t god_player_id = 0;

	// Horror Mode state
	int horror_battery_percent = 100;  // 0-100
	bool horror_flashlight_on = true;
	int horror_battery_drain_counter = 0;
	static constexpr int HORROR_BATTERY_DRAIN_INTERVAL = 180; // frames per 1% drain (~3 seconds at 60fps)
	static constexpr int HORROR_FLASHLIGHT_RADIUS = 60;       // radius when flashlight is on
	static constexpr int HORROR_DARKNESS_LEVEL = 245;         // near pitch black
	std::unique_ptr<Window_Help> horror_battery_window;
	std::string horror_current_map_music;  // Currently playing map-based horror music name
	int horror_last_map_id = -1;           // Track map changes to switch horror music (-1 = force update)
	std::string horror_overridden_bgm;     // Last game BGM we detected and overrode
	int horror_music_override_delay = 0;   // Countdown frames to re-override after game BGM change

	// Jumpscare state
	bool horror_jumpscare_active = false;
	int horror_jumpscare_timer = 0;
	static constexpr int HORROR_JUMPSCARE_DURATION = 120; // frames (~2 seconds at 60fps)
	std::unique_ptr<Sprite> horror_jumpscare_sprite;

	// Rawberry enemy
	std::unique_ptr<Game_Rawberry> rawberry;

	// ASYM mode state
	bool asym_is_local_hunter = false;
	uint16_t asym_hunter_id = 0;
	bool asym_initialized = false;

	// Darkness/lighting overlay
	std::unique_ptr<Drawable_DarknessOverlay> darkness_overlay;

	// Persistent lighting settings (survive overlay recreation across map changes)
	bool lighting_enabled = false;
	uint8_t lighting_darkness_level = 180;
	int lighting_player_radius = 80;

	// Player skin state
	std::string skin_charset_name;
	int skin_char_index = 0;
	std::vector<uint8_t> skin_image_data;          // Raw image file bytes (PNG/BMP)
	bool skin_sent_this_session = false;            // Already broadcast to current peers
	std::map<uint16_t, std::string> peer_skin_names; // peer_id -> registered cache name
};

} // namespace Chaos

#endif
