/*
 * Chaos Fork: Lua Script Engine
 *
 * Provides LIMITED modding capabilities through Lua:
 *  - Undertale-mode attacks
 *  - Horror-mode enemies
 *  - Basic gamemodes
 *
 * Requires CHAOS_HAS_LUA to be defined for actual Lua support.
 */

#ifndef EP_CHAOS_MOD_LUA_H
#define EP_CHAOS_MOD_LUA_H

#include "chaos/mod_script_engine.h"

#ifdef CHAOS_HAS_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

#include <string>
#include <unordered_map>

namespace Chaos {

class LuaEngine : public ScriptEngine {
public:
	LuaEngine();
	~LuaEngine() override;

	bool Init() override;
	void Shutdown() override;
	bool LoadMod(const ModInfo& mod) override;

	void CallAttackStart(const ScriptedAttackDef& def, ScriptBulletContext& ctx) override;
	bool CallAttackUpdate(const ScriptedAttackDef& def, ScriptBulletContext& ctx) override;
	void CallEnemySpawn(const ScriptedEnemyDef& def) override;
	void CallEnemyUpdate(const ScriptedEnemyDef& def) override;
	void CallGamemodeStart(const ScriptedGamemodeDef& def) override;
	void CallGamemodeUpdate(const ScriptedGamemodeDef& def) override;
	void CallGamemodeBattleStart(const ScriptedGamemodeDef& def) override;
	void CallGamemodeBattleEnd(const ScriptedGamemodeDef& def) override;

	ModLanguage GetLanguage() const override { return ModLanguage::Lua; }

private:
#ifdef CHAOS_HAS_LUA
	lua_State* L = nullptr;

	/** Push the Lua function named `func_name` from the mod's table onto stack.
	 *  Returns true if a function was found and pushed. */
	bool PushModFunction(const std::string& mod_id, const std::string& func_name);

	/** Call a Lua function that takes no args and returns nothing. */
	void CallSimple(const std::string& mod_id, const std::string& func_name);

	/** Register the C API into Lua globals. */
	void RegisterAPI();
#endif

	bool initialized = false;

	/** Map from mod_id -> Lua table reference (in registry) */
	std::unordered_map<std::string, int> mod_tables;
};

} // namespace Chaos

#endif // EP_CHAOS_MOD_LUA_H
