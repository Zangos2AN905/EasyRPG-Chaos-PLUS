/*
 * Chaos Fork: Singleplayer Game Mode Implementation
 */

#include "chaos/game_mode.h"
#include "chaos/mod_api.h"
#include "chaos/mod_loader.h"

namespace Chaos {

static GameMode current_game_mode = GameMode::Normal;
static std::string current_scripted_gamemode;
static bool scripted_gamemode_started = false;

static const ScriptedGamemodeDef* ResolveCurrentScriptedGamemode() {
	if (current_scripted_gamemode.empty()) {
		return nullptr;
	}

	return ModRegistry::Instance().FindGamemode(current_scripted_gamemode);
}

static ScriptEngine* GetCurrentScriptedGamemodeEngine(const ScriptedGamemodeDef& def) {
	return ModLoader::Instance().GetEngineForMod(def.mod_id);
}

GameMode GetCurrentGameMode() {
	return current_game_mode;
}

void SetCurrentGameMode(GameMode mode) {
	current_game_mode = mode;
	current_scripted_gamemode.clear();
	scripted_gamemode_started = false;
}

void SetCurrentScriptedGamemode(const std::string& id) {
	current_game_mode = GameMode::Normal;
	current_scripted_gamemode = id;
	scripted_gamemode_started = false;
}

void ClearCurrentScriptedGamemode() {
	current_scripted_gamemode.clear();
	scripted_gamemode_started = false;
}

bool HasCurrentScriptedGamemode() {
	return !current_scripted_gamemode.empty() && ResolveCurrentScriptedGamemode() != nullptr;
}

const std::string& GetCurrentScriptedGamemodeId() {
	return current_scripted_gamemode;
}

const ScriptedGamemodeDef* GetCurrentScriptedGamemode() {
	return ResolveCurrentScriptedGamemode();
}

void NotifyCurrentGamemodeStart() {
	auto* def = ResolveCurrentScriptedGamemode();
	if (!def || scripted_gamemode_started) {
		return;
	}

	auto* engine = GetCurrentScriptedGamemodeEngine(*def);
	if (!engine) {
		return;
	}

	engine->CallGamemodeStart(*def);
	scripted_gamemode_started = true;
}

void NotifyCurrentGamemodeUpdate() {
	auto* def = ResolveCurrentScriptedGamemode();
	if (!def) {
		return;
	}

	auto* engine = GetCurrentScriptedGamemodeEngine(*def);
	if (!engine) {
		return;
	}

	NotifyCurrentGamemodeStart();
	engine->CallGamemodeUpdate(*def);
}

void NotifyCurrentGamemodeBattleStart() {
	auto* def = ResolveCurrentScriptedGamemode();
	if (!def) {
		return;
	}

	auto* engine = GetCurrentScriptedGamemodeEngine(*def);
	if (!engine) {
		return;
	}

	NotifyCurrentGamemodeStart();
	engine->CallGamemodeBattleStart(*def);
}

void NotifyCurrentGamemodeBattleEnd() {
	auto* def = ResolveCurrentScriptedGamemode();
	if (!def) {
		return;
	}

	auto* engine = GetCurrentScriptedGamemodeEngine(*def);
	if (!engine) {
		return;
	}

	engine->CallGamemodeBattleEnd(*def);
}

bool IsUndertaleMode() {
	return current_game_mode == GameMode::Undertale;
}

bool IsHorrorSingleplayerMode() {
	return current_game_mode == GameMode::Horror;
}

} // namespace Chaos
