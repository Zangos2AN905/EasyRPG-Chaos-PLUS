/*
 * Chaos Fork: Custom Mode
 * Host-built mix of two game modes with objectives and chaos toggles.
 */

#ifndef EP_CHAOS_CUSTOM_MODE_H
#define EP_CHAOS_CUSTOM_MODE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <lcf/rpg/music.h>
#include "chaos/multiplayer_mode.h"

class Window_Help;

namespace Chaos {

struct CustomModeSettings {
	uint8_t mode_a = 0;      // MultiplayerMode
	uint8_t mode_b = 0;      // MultiplayerMode
	uint8_t objective = 0;   // CustomObjective
	bool random_events = false;
	bool force_skin = false;
	bool turbo_movement = false;
	bool random_start = false;
	std::string force_skin_charset;
	int force_skin_index = 0;
	std::vector<uint8_t> force_skin_data;
	bool host_via_relay = false; // lobby-only, not serialized
};

enum class ChaosEventType : uint8_t {
	ReversedControls = 0,
	SlowMotion,
	ScreenShake,
	ScreenFlash,
	Darkness,
	RandomMusic,
	RandomChipset,
	RandomTeleport,
	RandomMapTeleport,
	SwapPositions,
	FullHeal,
	GoldRush,
	SpinCycle,
	SpeedUp,
	Silence,
	Count,
};

const char* GetChaosEventName(ChaosEventType ev);

class CustomMode {
public:
	static CustomMode& Instance();

	/** Whether a custom mode session is currently running */
	bool IsActive() const;

	/** Whether the given base mode is part of the custom mix */
	bool IsModeInMix(MultiplayerMode m) const;

	/** Merged properties of both base modes */
	ModeProperties GetEffectiveProperties() const;

	const CustomModeSettings& GetSettings() const { return settings; }

	/** Lobby setup flow */
	void SetPendingSettings(CustomModeSettings s);
	bool HasPendingSettings() const { return has_pending; }
	CustomModeSettings TakePendingSettings();

	/** Multiplayer lifecycle */
	void Start();
	void Stop();
	void Update();
	void OnMapLoaded();

	/** Networking */
	void BroadcastSettings();
	void SendSettingsTo(uint16_t peer_id);
	void HandleSettingsPacket(const uint8_t* data, size_t len);
	void HandleEventPacket(const uint8_t* data, size_t len);

	/** Forced skin (all players wear the same skin) */
	bool IsForcedSkinActive() const { return started && settings.force_skin && force_skin_registered; }
	const std::string& GetForcedSkinName() const;
	int GetForcedSkinIndex() const { return settings.force_skin_index; }

	/** Player movement hooks */
	int ReverseDir4(int dir4) const;

private:
	CustomMode() = default;

	void TriggerRandomEvent();
	void ApplyEvent(ChaosEventType ev, const std::vector<int32_t>& args, const std::string& str_arg);
	void ApplyForcedSkin();
	void ApplySpeedAdjust();
	void ShowBanner(const std::string& text);
	void UpdateObjectiveWindow();

	CustomModeSettings settings;
	bool has_pending = false;

	bool started = false;
	bool force_skin_registered = false;

	int event_timer = 0;
	int reversed_timer = 0;
	int slow_timer = 0;
	int fast_timer = 0;
	int dark_timer = 0;
	int spin_timer = 0;
	int spin_counter = 0;
	int silence_timer = 0;
	int last_speed_adjust = 0;
	bool random_start_done = false;
	bool objective_announced_end = false;

	int objective_frames_left = -1;
	std::unique_ptr<Window_Help> objective_window;
	std::unique_ptr<Window_Help> banner_window;
	int banner_timer = 0;

	lcf::rpg::Music saved_bgm;
	bool has_saved_bgm = false;
};

} // namespace Chaos

#endif
