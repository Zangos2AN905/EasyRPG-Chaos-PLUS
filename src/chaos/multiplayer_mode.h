/*
 * Chaos Fork: Multiplayer Mode Definitions
 */

#ifndef EP_CHAOS_MULTIPLAYER_MODE_H
#define EP_CHAOS_MULTIPLAYER_MODE_H

#include <cstdint>
#include <string>

namespace Chaos {

enum class MultiplayerMode {
	Normal = 0,       // Existing non-team mode
	TeamParty = 1,    // Team up, switches/variables sync, max 4 players
	Chaotix = 2,      // Like TeamParty but players must stay close
	Single = 3,       // Placeholder: each player controls one player
	Horror = 4,       // Placeholder: FNAF-inspired multiplayer mode
	Rewind = 5,       // Placeholder: shared-server rewinds
	Split = 6,        // Placeholder: normal and broken/EXE groups
	Underwater = 7,   // Placeholder: limited-air multiplayer mode
	Custom = 8,       // Host-built mix of two modes with objectives and chaos toggles
	Count = 9,

	// Deprecated modes retained only so old code paths remain identifiable.
	// They are intentionally outside Count and cannot be selected or joined.
	DeprecatedHorror = 100,
	Asym = 101,
	Undertale = 102,
	GodMode = 103,
	Pandora = 104,
};

enum class CustomObjective : uint8_t {
	BeatGame = 0,   // Beat the game normally
	Timed = 1,      // Beat the game within the time limit
	Count = 2,
};

inline const char* GetObjectiveName(CustomObjective objective) {
	switch (objective) {
		case CustomObjective::BeatGame: return "Beat the game normally";
		case CustomObjective::Timed: return "Timed (8 minutes)";
		default: return "";
	}
}

struct ModeProperties {
	const char* name;
	const char* description;
	int max_players;       // 0 = unlimited
	bool sync_switches;
	bool sync_variables;
	bool sync_actor_states; // sync HP/SP/conditions between players
	bool proximity_required;
	bool has_god_player;
};

inline const ModeProperties& GetModeProperties(MultiplayerMode mode) {
	static const ModeProperties props[] = {
		{ "Normal",         "Normal non-team multiplayer mode. Unlimited players.",              0, false, false, false, false, false },
		{ "Team Party",     "Team up! Switches and variables sync. Max 4 players.",             4, true,  true,  false, false, false },
		{ "Chaotix",        "Team mode, but players must stay close together. Max 4.",          4, true,  true,  true,  true,  false },
		{ "Single Mode",    "Twitch Plays-style multiplayer. Each player controls one player.",   0, false, false, false, false, false },
		{ "Horror Mode",    "FNAF-inspired multiplayer horror mode.",                             4, false, false, false, false, false },
		{ "Rewind Mode",    "Each player gets rewinds that rewind the whole server.",              4, false, false, false, false, false },
		{ "Split Mode",     "Two groups: normal and a broken/EXE version of the world.",          4, false, false, false, false, false },
		{ "Underwater Mode", "Classic Sonic-style underwater survival. Find air pockets.",         4, false, false, false, false, false },
		{ "Custom Mode",    "Mix two modes, pick an objective, toggle chaos features.",           0, false, false, false, false, false },
	};
	static const ModeProperties deprecated = { "Deprecated", "Deprecated mode.", 0, false, false, false, false, false };
	int idx = static_cast<int>(mode);
	if (idx < 0 || idx >= static_cast<int>(sizeof(props) / sizeof(props[0]))) return deprecated;
	return props[idx];
}

inline bool IsSupportedMultiplayerMode(MultiplayerMode mode) {
	return static_cast<int>(mode) >= 0 && static_cast<int>(mode) < static_cast<int>(MultiplayerMode::Count);
}

inline int GetModeCount() {
	return static_cast<int>(MultiplayerMode::Count);
}

} // namespace Chaos

#endif
