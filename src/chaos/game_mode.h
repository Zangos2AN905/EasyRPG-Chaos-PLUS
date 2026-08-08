/*
 * Chaos Fork: Singleplayer Game Mode Definitions
 * Defines game modes available in singleplayer (Normal, Horror, Undertale).
 */

#ifndef EP_CHAOS_GAME_MODE_H
#define EP_CHAOS_GAME_MODE_H

#include <string>

namespace Chaos {

struct ScriptedGamemodeDef;

enum class GameMode {
	Normal = 0,
	Horror,
	Undertale,
	Count
};

struct GameModeInfo {
	const char* name;
	const char* description;
};

inline const GameModeInfo& GetGameModeInfo(GameMode mode) {
	static const GameModeInfo info[] = {
		{ "Normal",    "Standard RPG Maker gameplay." },
		{ "Horror",    "Darkness engulfs the world. SURVIVE." },
		{ "Undertale", "Pixel movement. Determination." },
	};
	return info[static_cast<int>(mode)];
}

/** Global singleplayer game mode. Set before game starts. */
GameMode GetCurrentGameMode();
void SetCurrentGameMode(GameMode mode);
void SetCurrentScriptedGamemode(const std::string& id);
void ClearCurrentScriptedGamemode();
bool HasCurrentScriptedGamemode();
const std::string& GetCurrentScriptedGamemodeId();
const ScriptedGamemodeDef* GetCurrentScriptedGamemode();
void NotifyCurrentGamemodeStart();
void NotifyCurrentGamemodeUpdate();
void NotifyCurrentGamemodeBattleStart();
void NotifyCurrentGamemodeBattleEnd();
bool IsUndertaleMode();
bool IsHorrorSingleplayerMode();

} // namespace Chaos

#endif
