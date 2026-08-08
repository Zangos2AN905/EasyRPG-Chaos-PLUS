/*
 * Chaos Fork: Mod API
 * Defines the interface that mods can use to extend the game.
 * Both AngelScript and Lua mods interact through this API layer.
 *
 * AngelScript mods have FULL access:
 *   - Create gamemodes
 *   - Create horror mode enemies
 *   - Create undertale mode attacks (bullet patterns)
 *   - Register custom scenes
 *   - Access game state
 *
 * Lua mods have LIMITED access:
 *   - Create undertale mode attacks (bullet patterns)
 *   - Create horror mode enemies
 *   - Create basic gamemodes (name + description + hooks)
 */

#ifndef EP_CHAOS_MOD_API_H
#define EP_CHAOS_MOD_API_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Chaos {

/** Capabilities a mod language supports */
enum class ModCapability {
	UndertaleAttacks,    // Create bullet patterns
	HorrorEnemies,       // Create horror mode enemies
	BasicGamemodes,      // Register simple gamemodes (name/description/hooks)
	FullGamemodes,       // Full gamemode control (custom scenes, UI)
	CustomScenes,        // Register custom scene types
	GameStateAccess,     // Read/write game variables, switches, etc.
};

/** Mod language type */
enum class ModLanguage {
	AngelScript,
	Lua,
};

/** Metadata about a loaded mod */
struct ModInfo {
	std::string id;            // Unique mod identifier (folder name)
	std::string name;          // Display name
	std::string author;        // Author
	std::string version;       // Version string
	std::string description;   // Description
	ModLanguage language;      // Script language
	std::string script_path;   // Path to main script file
	std::string assets_path;   // Path to mod assets folder
	bool enabled = true;
};

/**
 * Scripted bullet pattern definition.
 * Mods register these; the engine creates ScriptedBulletPattern instances from them.
 */
struct ScriptedAttackDef {
	std::string id;                // Unique attack ID (e.g., "mymod_spiral")
	std::string name;              // Display name
	std::string mod_id;            // Owning mod
	int default_duration = 300;    // Duration in frames

	// Script callback indices/references (opaque to the API, used by language bindings)
	int on_start_ref = -1;
	int on_update_ref = -1;
};

/**
 * Scripted horror enemy definition.
 * Mods register these; the engine can spawn them during horror mode.
 */
struct ScriptedEnemyDef {
	std::string id;                // Unique enemy ID
	std::string name;              // Display name
	std::string mod_id;            // Owning mod
	std::string sprite_name;       // Sprite filename in mod assets
	int base_hp = 100;
	int base_atk = 10;
	int base_def = 5;

	int on_spawn_ref = -1;
	int on_update_ref = -1;
	int on_death_ref = -1;
};

/**
 * Scripted gamemode definition.
 * Lua gets basic gamemodes (hooks only), AngelScript gets full control.
 */
struct ScriptedGamemodeDef {
	std::string id;
	std::string name;
	std::string description;
	std::string mod_id;
	bool is_full = false;          // true = AngelScript full gamemode

	int on_start_ref = -1;
	int on_update_ref = -1;
	int on_battle_start_ref = -1;
	int on_battle_end_ref = -1;
};

/**
 * Registry that holds all mod-registered content.
 * Singleton — all mod loaders register into this.
 */
class ModRegistry {
public:
	static ModRegistry& Instance();

	// Attack patterns
	void RegisterAttack(ScriptedAttackDef def);
	const ScriptedAttackDef* FindAttack(const std::string& id) const;
	std::vector<const ScriptedAttackDef*> GetAllAttacks() const;
	std::vector<const ScriptedAttackDef*> GetAttacksByMod(const std::string& mod_id) const;

	// Horror enemies
	void RegisterEnemy(ScriptedEnemyDef def);
	const ScriptedEnemyDef* FindEnemy(const std::string& id) const;
	std::vector<const ScriptedEnemyDef*> GetAllEnemies() const;

	// Gamemodes
	void RegisterGamemode(ScriptedGamemodeDef def);
	const ScriptedGamemodeDef* FindGamemode(const std::string& id) const;
	std::vector<const ScriptedGamemodeDef*> GetAllGamemodes() const;

	// Loaded mods
	void RegisterMod(ModInfo info);
	const ModInfo* FindMod(const std::string& id) const;
	const std::vector<ModInfo>& GetLoadedMods() const { return loaded_mods; }

	void Clear();

private:
	ModRegistry() = default;

	std::vector<ModInfo> loaded_mods;
	std::vector<ScriptedAttackDef> attacks;
	std::vector<ScriptedEnemyDef> enemies;
	std::vector<ScriptedGamemodeDef> gamemodes;
};

/**
 * Check if a language supports a given capability.
 */
inline bool LanguageHasCapability(ModLanguage lang, ModCapability cap) {
	switch (lang) {
		case ModLanguage::AngelScript:
			return true; // AngelScript has all capabilities
		case ModLanguage::Lua:
			switch (cap) {
				case ModCapability::UndertaleAttacks:
				case ModCapability::HorrorEnemies:
				case ModCapability::BasicGamemodes:
					return true;
				default:
					return false;
			}
	}
	return false;
}

} // namespace Chaos

#endif
