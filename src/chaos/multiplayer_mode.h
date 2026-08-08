/*
 * Chaos Fork: Multiplayer Mode Definitions
 */

#ifndef EP_CHAOS_MULTIPLAYER_MODE_H
#define EP_CHAOS_MULTIPLAYER_MODE_H

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
	Count = 8,

	// Deprecated modes retained only so old code paths remain identifiable.
	// They are intentionally outside Count and cannot be selected or joined.
	DeprecatedHorror = 8,
	Asym = 9,
	Undertale = 10,
	GodMode = 11,
	Pandora = 12,
};

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
		{ "Deprecated Horror", "Deprecated multiplayer horror mode.",                              0, false, false, false, false, false },
		{ "Deprecated Asym", "Deprecated asymmetric multiplayer mode.",                            0, false, false, false, false, false },
		{ "Deprecated Undertale", "Deprecated multiplayer Undertale mode.",                       0, false, false, false, false, false },
		{ "Deprecated God", "Deprecated multiplayer God mode.",                                    0, false, false, false, false, false },
		{ "Deprecated Pandora", "Deprecated multiplayer Pandora mode.",                            0, false, false, false, false, false },
	};
	return props[static_cast<int>(mode)];
}

inline bool IsSupportedMultiplayerMode(MultiplayerMode mode) {
	return static_cast<int>(mode) >= 0 && static_cast<int>(mode) < static_cast<int>(MultiplayerMode::Count);
}

inline int GetModeCount() {
	return static_cast<int>(MultiplayerMode::Count);
}

} // namespace Chaos

#endif
