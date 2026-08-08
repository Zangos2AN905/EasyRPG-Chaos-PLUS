/*
 * Chaos Fork: Script Engine Abstraction
 * Abstract base for AngelScript and Lua script engines.
 * Each engine loads mod scripts and provides callback execution.
 */

#ifndef EP_CHAOS_MOD_SCRIPT_ENGINE_H
#define EP_CHAOS_MOD_SCRIPT_ENGINE_H

#include "chaos/mod_api.h"
#include <string>
#include <vector>
#include <memory>

namespace Chaos {

/**
 * Context passed to script callbacks for bullet pattern updates.
 * Scripts use this to spawn bullets, query box dimensions, etc.
 */
struct ScriptBulletContext {
	int box_x = 0;
	int box_y = 0;
	int box_w = 0;
	int box_h = 0;
	int frame = 0;
	bool is_tough = false;

	// Bullet spawn requests from scripts
	struct BulletSpawn {
		float x, y, vx, vy;
		int hitbox_w, hitbox_h;
		std::string sprite_name; // From mod assets
	};
	std::vector<BulletSpawn> pending_spawns;

	void RequestBullet(float x, float y, float vx, float vy,
					   int hw = 8, int hh = 8,
					   const std::string& sprite = "") {
		pending_spawns.push_back({x, y, vx, vy, hw, hh, sprite});
	}

	void ClearSpawns() { pending_spawns.clear(); }
};

/**
 * Abstract script engine interface.
 * Implementations exist for AngelScript and Lua.
 */
class ScriptEngine {
public:
	virtual ~ScriptEngine() = default;

	/** Initialize the engine */
	virtual bool Init() = 0;

	/** Shut down and release resources */
	virtual void Shutdown() = 0;

	/** Load and execute a mod's main script file */
	virtual bool LoadMod(const ModInfo& mod) = 0;

	/** Call a registered attack's on_start callback */
	virtual void CallAttackStart(const ScriptedAttackDef& def,
								 ScriptBulletContext& ctx) = 0;

	/** Call a registered attack's on_update callback. Returns true when done. */
	virtual bool CallAttackUpdate(const ScriptedAttackDef& def,
								  ScriptBulletContext& ctx) = 0;

	/** Call a registered enemy's on_spawn callback */
	virtual void CallEnemySpawn(const ScriptedEnemyDef& def) = 0;

	/** Call a registered enemy's on_update callback */
	virtual void CallEnemyUpdate(const ScriptedEnemyDef& def) = 0;

	/** Call a registered gamemode's on_start callback */
	virtual void CallGamemodeStart(const ScriptedGamemodeDef& def) = 0;

	/** Call a registered gamemode's on_update callback */
	virtual void CallGamemodeUpdate(const ScriptedGamemodeDef& def) = 0;

	/** Call a registered gamemode's on_battle_start callback */
	virtual void CallGamemodeBattleStart(const ScriptedGamemodeDef& def) = 0;

	/** Call a registered gamemode's on_battle_end callback */
	virtual void CallGamemodeBattleEnd(const ScriptedGamemodeDef& def) = 0;

	/** Get the language this engine handles */
	virtual ModLanguage GetLanguage() const = 0;
};

} // namespace Chaos

#endif
