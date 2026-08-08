/*
 * Chaos Fork: AngelScript Engine
 * Full-featured scripting engine for mods.
 * Supports: gamemodes, horror enemies, undertale attacks, custom scenes, game state.
 *
 * Requires the AngelScript library (angelscript.h).
 * Compiled only when CHAOS_HAS_ANGELSCRIPT is defined.
 */

#ifndef EP_CHAOS_MOD_ANGELSCRIPT_H
#define EP_CHAOS_MOD_ANGELSCRIPT_H

#include "chaos/mod_script_engine.h"

#ifdef CHAOS_HAS_ANGELSCRIPT
#include <angelscript.h>
#endif

#include <string>
#include <unordered_map>

namespace Chaos {

/**
 * AngelScript engine implementation.
 * Has FULL mod capabilities.
 */
class AngelScriptEngine : public ScriptEngine {
public:
	AngelScriptEngine();
	~AngelScriptEngine() override;

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

	ModLanguage GetLanguage() const override { return ModLanguage::AngelScript; }

private:
#ifdef CHAOS_HAS_ANGELSCRIPT
	asIScriptEngine* as_engine = nullptr;

	void RegisterAPI();
	void MessageCallback(const asSMessageInfo* msg);
	asIScriptFunction* GetFunction(int ref);

	// Module per mod
	std::unordered_map<std::string, asIScriptModule*> modules;
	// Function reference table
	std::unordered_map<int, asIScriptFunction*> function_refs;
	int next_ref = 1;
#endif

	bool initialized = false;
};

} // namespace Chaos

#endif
