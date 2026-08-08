/*
 * Chaos Fork: Mod API implementation
 */

#include "chaos/mod_api.h"
#include "output.h"

namespace Chaos {

ModRegistry& ModRegistry::Instance() {
	static ModRegistry instance;
	return instance;
}

void ModRegistry::RegisterAttack(ScriptedAttackDef def) {
	Output::Debug("ModRegistry: Registered attack '{}' from mod '{}'", def.id, def.mod_id);
	attacks.push_back(std::move(def));
}

const ScriptedAttackDef* ModRegistry::FindAttack(const std::string& id) const {
	for (const auto& a : attacks) {
		if (a.id == id) return &a;
	}
	return nullptr;
}

std::vector<const ScriptedAttackDef*> ModRegistry::GetAllAttacks() const {
	std::vector<const ScriptedAttackDef*> result;
	result.reserve(attacks.size());
	for (const auto& a : attacks) {
		result.push_back(&a);
	}
	return result;
}

std::vector<const ScriptedAttackDef*> ModRegistry::GetAttacksByMod(const std::string& mod_id) const {
	std::vector<const ScriptedAttackDef*> result;
	for (const auto& a : attacks) {
		if (a.mod_id == mod_id) result.push_back(&a);
	}
	return result;
}

void ModRegistry::RegisterEnemy(ScriptedEnemyDef def) {
	Output::Debug("ModRegistry: Registered enemy '{}' from mod '{}'", def.id, def.mod_id);
	enemies.push_back(std::move(def));
}

const ScriptedEnemyDef* ModRegistry::FindEnemy(const std::string& id) const {
	for (const auto& e : enemies) {
		if (e.id == id) return &e;
	}
	return nullptr;
}

std::vector<const ScriptedEnemyDef*> ModRegistry::GetAllEnemies() const {
	std::vector<const ScriptedEnemyDef*> result;
	result.reserve(enemies.size());
	for (const auto& e : enemies) {
		result.push_back(&e);
	}
	return result;
}

void ModRegistry::RegisterGamemode(ScriptedGamemodeDef def) {
	Output::Debug("ModRegistry: Registered gamemode '{}' from mod '{}'", def.id, def.mod_id);
	gamemodes.push_back(std::move(def));
}

const ScriptedGamemodeDef* ModRegistry::FindGamemode(const std::string& id) const {
	for (const auto& g : gamemodes) {
		if (g.id == id) return &g;
	}
	return nullptr;
}

std::vector<const ScriptedGamemodeDef*> ModRegistry::GetAllGamemodes() const {
	std::vector<const ScriptedGamemodeDef*> result;
	result.reserve(gamemodes.size());
	for (const auto& g : gamemodes) {
		result.push_back(&g);
	}
	return result;
}

void ModRegistry::RegisterMod(ModInfo info) {
	Output::Debug("ModRegistry: Loaded mod '{}' ({}) [{}]",
		info.name, info.id,
		info.language == ModLanguage::AngelScript ? "AngelScript" : "Lua");
	loaded_mods.push_back(std::move(info));
}

const ModInfo* ModRegistry::FindMod(const std::string& id) const {
	for (const auto& m : loaded_mods) {
		if (m.id == id) return &m;
	}
	return nullptr;
}

void ModRegistry::Clear() {
	loaded_mods.clear();
	attacks.clear();
	enemies.clear();
	gamemodes.clear();
}

} // namespace Chaos
