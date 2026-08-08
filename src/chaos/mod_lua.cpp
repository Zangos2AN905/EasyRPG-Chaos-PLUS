/*
 * Chaos Fork: Lua Engine implementation
 *
 * When CHAOS_HAS_LUA is defined, provides a Lua 5.4 scripting engine
 * with limited capabilities (attacks, enemies, basic gamemodes).
 * Otherwise, all methods are stubs.
 */

#include "chaos/mod_lua.h"
#include "chaos/mod_api.h"
#include "output.h"

#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdlib>

namespace Chaos {

LuaEngine::LuaEngine() = default;
LuaEngine::~LuaEngine() { Shutdown(); }

#ifdef CHAOS_HAS_LUA

// ---- Global context pointer for Lua C functions ----
namespace {
	ScriptBulletContext* g_lua_bullet_ctx = nullptr;
}

// ---- Lua C API functions ----

static int lua_spawn_bullet(lua_State* L) {
	float x = static_cast<float>(luaL_checknumber(L, 1));
	float y = static_cast<float>(luaL_checknumber(L, 2));
	float vx = static_cast<float>(luaL_checknumber(L, 3));
	float vy = static_cast<float>(luaL_checknumber(L, 4));
	int hw = static_cast<int>(luaL_checkinteger(L, 5));
	int hh = static_cast<int>(luaL_checkinteger(L, 6));

	if (lua_gettop(L) >= 7 && lua_isstring(L, 7)) {
		const char* sprite = lua_tostring(L, 7);
		if (g_lua_bullet_ctx) {
			g_lua_bullet_ctx->RequestBullet(x, y, vx, vy, hw, hh, sprite);
		}
	} else {
		if (g_lua_bullet_ctx) {
			g_lua_bullet_ctx->RequestBullet(x, y, vx, vy, hw, hh);
		}
	}
	return 0;
}

static int lua_box_x(lua_State* L) {
	lua_pushinteger(L, g_lua_bullet_ctx ? g_lua_bullet_ctx->box_x : 0);
	return 1;
}

static int lua_box_y(lua_State* L) {
	lua_pushinteger(L, g_lua_bullet_ctx ? g_lua_bullet_ctx->box_y : 0);
	return 1;
}

static int lua_box_w(lua_State* L) {
	lua_pushinteger(L, g_lua_bullet_ctx ? g_lua_bullet_ctx->box_w : 0);
	return 1;
}

static int lua_box_h(lua_State* L) {
	lua_pushinteger(L, g_lua_bullet_ctx ? g_lua_bullet_ctx->box_h : 0);
	return 1;
}

static int lua_get_frame(lua_State* L) {
	lua_pushinteger(L, g_lua_bullet_ctx ? g_lua_bullet_ctx->frame : 0);
	return 1;
}

static int lua_is_tough(lua_State* L) {
	lua_pushboolean(L, g_lua_bullet_ctx ? g_lua_bullet_ctx->is_tough : false);
	return 1;
}

static int lua_log(lua_State* L) {
	const char* msg = luaL_checkstring(L, 1);
	Output::Debug("Mod: {}", msg);
	return 0;
}

static int lua_register_attack(lua_State* L) {
	const char* id = luaL_checkstring(L, 1);
	const char* name = luaL_checkstring(L, 2);
	int duration = static_cast<int>(luaL_checkinteger(L, 3));

	ScriptedAttackDef def;
	def.id = id;
	def.name = name;
	def.default_duration = duration;
	ModRegistry::Instance().RegisterAttack(std::move(def));
	return 0;
}

static int lua_register_enemy(lua_State* L) {
	const char* id = luaL_checkstring(L, 1);
	const char* name = luaL_checkstring(L, 2);
	int hp = static_cast<int>(luaL_checkinteger(L, 3));
	int atk = static_cast<int>(luaL_checkinteger(L, 4));
	int def_val = static_cast<int>(luaL_checkinteger(L, 5));
	const char* sprite = luaL_checkstring(L, 6);

	ScriptedEnemyDef edef;
	edef.id = id;
	edef.name = name;
	edef.base_hp = hp;
	edef.base_atk = atk;
	edef.base_def = def_val;
	edef.sprite_name = sprite;
	ModRegistry::Instance().RegisterEnemy(std::move(edef));
	return 0;
}

static int lua_register_gamemode(lua_State* L) {
	const char* id = luaL_checkstring(L, 1);
	const char* name = luaL_checkstring(L, 2);
	const char* desc = luaL_checkstring(L, 3);

	ScriptedGamemodeDef gdef;
	gdef.id = id;
	gdef.name = name;
	gdef.description = desc;
	gdef.is_full = false; // Lua gets basic gamemodes only
	ModRegistry::Instance().RegisterGamemode(std::move(gdef));
	return 0;
}

static int lua_math_sin(lua_State* L) {
	lua_pushnumber(L, std::sin(luaL_checknumber(L, 1)));
	return 1;
}

static int lua_math_cos(lua_State* L) {
	lua_pushnumber(L, std::cos(luaL_checknumber(L, 1)));
	return 1;
}

static int lua_rand01(lua_State* L) {
	lua_pushnumber(L, static_cast<double>(std::rand()) / RAND_MAX);
	return 1;
}

static int lua_rand_int(lua_State* L) {
	int lo = static_cast<int>(luaL_checkinteger(L, 1));
	int hi = static_cast<int>(luaL_checkinteger(L, 2));
	if (hi <= lo) {
		lua_pushinteger(L, lo);
	} else {
		lua_pushinteger(L, lo + (std::rand() % (hi - lo + 1)));
	}
	return 1;
}

void LuaEngine::RegisterAPI() {
	if (!L) return;

	// Create "chaos" table with all API functions
	lua_newtable(L);

	auto reg = [&](const char* name, lua_CFunction fn) {
		lua_pushcfunction(L, fn);
		lua_setfield(L, -2, name);
	};

	reg("log", lua_log);
	reg("spawn_bullet", lua_spawn_bullet);
	reg("box_x", lua_box_x);
	reg("box_y", lua_box_y);
	reg("box_w", lua_box_w);
	reg("box_h", lua_box_h);
	reg("frame", lua_get_frame);
	reg("is_tough", lua_is_tough);
	reg("register_attack", lua_register_attack);
	reg("register_enemy", lua_register_enemy);
	reg("register_gamemode", lua_register_gamemode);
	reg("sin", lua_math_sin);
	reg("cos", lua_math_cos);
	reg("rand01", lua_rand01);
	reg("rand_int", lua_rand_int);

	lua_setglobal(L, "chaos");
}

bool LuaEngine::Init() {
	if (initialized) return true;

	L = luaL_newstate();
	if (!L) {
		Output::Warning("Lua: Failed to create Lua state");
		return false;
	}

	// Open safe standard libraries (no os/io for security)
	luaL_requiref(L, "base", luaopen_base, 1); lua_pop(L, 1);
	luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
	luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
	luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);

	RegisterAPI();
	initialized = true;
	Output::Debug("Lua: Engine initialized");
	return true;
}

void LuaEngine::Shutdown() {
	if (L) {
		// Unref all mod tables
		for (auto& [id, ref] : mod_tables) {
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
		}
		mod_tables.clear();
		lua_close(L);
		L = nullptr;
	}
	initialized = false;
}

bool LuaEngine::LoadMod(const ModInfo& mod) {
	if (!initialized || !L) return false;

	// Read the script file
	std::ifstream file(mod.script_path);
	if (!file.is_open()) {
		Output::Warning("Lua: Could not open '{}'", mod.script_path);
		return false;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string code = buffer.str();

	// Load and execute the script
	int load_result = luaL_loadbuffer(L, code.c_str(), code.size(), mod.script_path.c_str());
	if (load_result != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		Output::Warning("Lua: Load error in '{}': {}", mod.id, err ? err : "unknown");
		lua_pop(L, 1);
		return false;
	}

	// Create an environment table for this mod (sandboxed)
	lua_newtable(L); // mod env table

	// Set metatable so it falls back to globals
	lua_newtable(L); // metatable
	lua_getglobal(L, "_G");
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);

	// Store a ref to the mod's environment
	lua_pushvalue(L, -1); // duplicate env
	int env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	mod_tables[mod.id] = env_ref;

	// Set as the chunk's upvalue (_ENV)
	lua_setupvalue(L, -2, 1); // sets _ENV of the loaded chunk

	// Execute the chunk (which runs top-level code including register_* calls)
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		Output::Warning("Lua: Runtime error in '{}': {}", mod.id, err ? err : "unknown");
		lua_pop(L, 1);
		return false;
	}

	Output::Debug("Lua: Loaded mod '{}'", mod.id);
	return true;
}

bool LuaEngine::PushModFunction(const std::string& mod_id, const std::string& func_name) {
	auto it = mod_tables.find(mod_id);
	if (it == mod_tables.end()) return false;

	lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
	lua_getfield(L, -1, func_name.c_str());
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2); // pop nil + env table
		return false;
	}
	lua_remove(L, -2); // remove env table, leave function
	return true;
}

void LuaEngine::CallSimple(const std::string& mod_id, const std::string& func_name) {
	if (!PushModFunction(mod_id, func_name)) return;
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		Output::Warning("Lua: Error calling {}.{}: {}", mod_id, func_name, err ? err : "unknown");
		lua_pop(L, 1);
	}
}

void LuaEngine::CallAttackStart(const ScriptedAttackDef& def, ScriptBulletContext& ctx) {
	g_lua_bullet_ctx = &ctx;
	CallSimple(def.mod_id, def.id + "_start");
	g_lua_bullet_ctx = nullptr;
}

bool LuaEngine::CallAttackUpdate(const ScriptedAttackDef& def, ScriptBulletContext& ctx) {
	g_lua_bullet_ctx = &ctx;

	std::string func_name = def.id + "_update";
	if (!PushModFunction(def.mod_id, func_name)) {
		g_lua_bullet_ctx = nullptr;
		return true; // No update function = done
	}

	bool done = false;
	if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
		done = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
	} else {
		const char* err = lua_tostring(L, -1);
		Output::Warning("Lua: Error in {}: {}", func_name, err ? err : "unknown");
		lua_pop(L, 1);
		done = true;
	}

	g_lua_bullet_ctx = nullptr;
	return done;
}

void LuaEngine::CallEnemySpawn(const ScriptedEnemyDef& def) {
	CallSimple(def.mod_id, def.id + "_spawn");
}

void LuaEngine::CallEnemyUpdate(const ScriptedEnemyDef& def) {
	CallSimple(def.mod_id, def.id + "_update");
}

void LuaEngine::CallGamemodeStart(const ScriptedGamemodeDef& def) {
	CallSimple(def.mod_id, def.id + "_gamemode_start");
}

void LuaEngine::CallGamemodeUpdate(const ScriptedGamemodeDef& def) {
	CallSimple(def.mod_id, def.id + "_gamemode_update");
}

void LuaEngine::CallGamemodeBattleStart(const ScriptedGamemodeDef& def) {
	CallSimple(def.mod_id, def.id + "_battle_start");
}

void LuaEngine::CallGamemodeBattleEnd(const ScriptedGamemodeDef& def) {
	CallSimple(def.mod_id, def.id + "_battle_end");
}

#else // !CHAOS_HAS_LUA

// ---- Stub implementation when Lua is not available ----

bool LuaEngine::Init() {
	Output::Debug("Lua: Not available (compiled without CHAOS_HAS_LUA)");
	return false;
}

void LuaEngine::Shutdown() {}

bool LuaEngine::LoadMod(const ModInfo&) {
	Output::Warning("Lua: Cannot load mod — engine not available");
	return false;
}

void LuaEngine::CallAttackStart(const ScriptedAttackDef&, ScriptBulletContext&) {}
bool LuaEngine::CallAttackUpdate(const ScriptedAttackDef&, ScriptBulletContext&) { return true; }
void LuaEngine::CallEnemySpawn(const ScriptedEnemyDef&) {}
void LuaEngine::CallEnemyUpdate(const ScriptedEnemyDef&) {}
void LuaEngine::CallGamemodeStart(const ScriptedGamemodeDef&) {}
void LuaEngine::CallGamemodeUpdate(const ScriptedGamemodeDef&) {}
void LuaEngine::CallGamemodeBattleStart(const ScriptedGamemodeDef&) {}
void LuaEngine::CallGamemodeBattleEnd(const ScriptedGamemodeDef&) {}

#endif // CHAOS_HAS_LUA

} // namespace Chaos
