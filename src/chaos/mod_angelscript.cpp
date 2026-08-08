/*
 * Chaos Fork: AngelScript Engine implementation
 *
 * When CHAOS_HAS_ANGELSCRIPT is defined, this provides a full AngelScript
 * scripting engine. Otherwise, all methods are stubs that log warnings.
 */

#include "chaos/mod_angelscript.h"
#include "chaos/mod_api.h"
#include "output.h"
#include "filefinder.h"
#include "utils.h"

#include <fstream>
#include <sstream>

#ifdef CHAOS_HAS_ANGELSCRIPT
// Provided by as_string_addon.cpp
void RegisterStdString(asIScriptEngine* engine);
#endif

namespace Chaos {

AngelScriptEngine::AngelScriptEngine() = default;
AngelScriptEngine::~AngelScriptEngine() { Shutdown(); }

#ifdef CHAOS_HAS_ANGELSCRIPT

// ---- AngelScript API registration helpers ----

namespace {
	// Global pointer set during script callbacks
	ScriptBulletContext* g_bullet_ctx = nullptr;
	ModRegistry* g_registry = nullptr;

	// Script-callable: spawn a bullet
	void AS_SpawnBullet(float x, float y, float vx, float vy, int hw, int hh) {
		if (g_bullet_ctx) {
			g_bullet_ctx->RequestBullet(x, y, vx, vy, hw, hh);
		}
	}

	void AS_SpawnBulletSprite(float x, float y, float vx, float vy,
							  int hw, int hh, const std::string& sprite) {
		if (g_bullet_ctx) {
			g_bullet_ctx->RequestBullet(x, y, vx, vy, hw, hh, sprite);
		}
	}

	// Script-callable: get box dimensions
	int AS_GetBoxX() { return g_bullet_ctx ? g_bullet_ctx->box_x : 0; }
	int AS_GetBoxY() { return g_bullet_ctx ? g_bullet_ctx->box_y : 0; }
	int AS_GetBoxW() { return g_bullet_ctx ? g_bullet_ctx->box_w : 0; }
	int AS_GetBoxH() { return g_bullet_ctx ? g_bullet_ctx->box_h : 0; }
	int AS_GetFrame() { return g_bullet_ctx ? g_bullet_ctx->frame : 0; }
	bool AS_IsTough() { return g_bullet_ctx ? g_bullet_ctx->is_tough : false; }

	// Script-callable: register an attack
	void AS_RegisterAttack(const std::string& id, const std::string& name, int duration) {
		// The actual function refs are set by the engine after script registers
		// This is a simplified registration point
		ScriptedAttackDef def;
		def.id = id;
		def.name = name;
		def.default_duration = duration;
		ModRegistry::Instance().RegisterAttack(std::move(def));
	}

	// Script-callable: register a horror enemy
	void AS_RegisterEnemy(const std::string& id, const std::string& name,
						  int hp, int atk, int def_val, const std::string& sprite) {
		ScriptedEnemyDef edef;
		edef.id = id;
		edef.name = name;
		edef.base_hp = hp;
		edef.base_atk = atk;
		edef.base_def = def_val;
		edef.sprite_name = sprite;
		ModRegistry::Instance().RegisterEnemy(std::move(edef));
	}

	// Script-callable: register a gamemode
	void AS_RegisterGamemode(const std::string& id, const std::string& name,
							 const std::string& desc) {
		ScriptedGamemodeDef gdef;
		gdef.id = id;
		gdef.name = name;
		gdef.description = desc;
		gdef.is_full = true; // AngelScript gets full gamemodes
		ModRegistry::Instance().RegisterGamemode(std::move(gdef));
	}

	// Script-callable: log output
	void AS_Log(const std::string& msg) {
		Output::Debug("Mod: {}", msg);
	}

	// Script-callable: math helpers
	float AS_Sin(float v) { return std::sin(v); }
	float AS_Cos(float v) { return std::cos(v); }
	float AS_Rand01() { return static_cast<float>(std::rand()) / RAND_MAX; }
	int AS_RandInt(int lo, int hi) {
		if (hi <= lo) return lo;
		return lo + (std::rand() % (hi - lo + 1));
	}
}

void AngelScriptEngine::MessageCallback(const asSMessageInfo* msg) {
	const char* type = "ERR ";
	if (msg->type == asMSGTYPE_WARNING) type = "WARN";
	else if (msg->type == asMSGTYPE_INFORMATION) type = "INFO";
	Output::Debug("AngelScript {} ({}:{}) : {}", type, msg->section, msg->row, msg->message);
}

void AngelScriptEngine::RegisterAPI() {
	if (!as_engine) return;

	// Register global functions available to scripts
	as_engine->RegisterGlobalFunction("void log(const string &in)", asFUNCTION(AS_Log), asCALL_CDECL);

	// Bullet context API
	as_engine->RegisterGlobalFunction("void spawn_bullet(float, float, float, float, int, int)",
		asFUNCTION(AS_SpawnBullet), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("void spawn_bullet(float, float, float, float, int, int, const string &in)",
		asFUNCTION(AS_SpawnBulletSprite), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("int box_x()", asFUNCTION(AS_GetBoxX), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("int box_y()", asFUNCTION(AS_GetBoxY), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("int box_w()", asFUNCTION(AS_GetBoxW), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("int box_h()", asFUNCTION(AS_GetBoxH), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("int frame()", asFUNCTION(AS_GetFrame), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("bool is_tough()", asFUNCTION(AS_IsTough), asCALL_CDECL);

	// Registration API
	as_engine->RegisterGlobalFunction("void register_attack(const string &in, const string &in, int)",
		asFUNCTION(AS_RegisterAttack), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("void register_enemy(const string &in, const string &in, int, int, int, const string &in)",
		asFUNCTION(AS_RegisterEnemy), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("void register_gamemode(const string &in, const string &in, const string &in)",
		asFUNCTION(AS_RegisterGamemode), asCALL_CDECL);

	// Math helpers
	as_engine->RegisterGlobalFunction("float sin(float)", asFUNCTION(AS_Sin), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("float cos(float)", asFUNCTION(AS_Cos), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("float rand01()", asFUNCTION(AS_Rand01), asCALL_CDECL);
	as_engine->RegisterGlobalFunction("int rand_int(int, int)", asFUNCTION(AS_RandInt), asCALL_CDECL);
}

bool AngelScriptEngine::Init() {
	if (initialized) return true;

	as_engine = asCreateScriptEngine();
	if (!as_engine) {
		Output::Warning("AngelScript: Failed to create script engine");
		return false;
	}

	as_engine->SetMessageCallback(asMETHOD(AngelScriptEngine, MessageCallback), this, asCALL_THISCALL);

	// Register string type (addon)
	// Note: requires angelscript add-on scriptstdstring
	RegisterStdString(as_engine);

	RegisterAPI();
	initialized = true;
	Output::Debug("AngelScript: Engine initialized");
	return true;
}

void AngelScriptEngine::Shutdown() {
	if (as_engine) {
		as_engine->ShutDownAndRelease();
		as_engine = nullptr;
	}
	modules.clear();
	function_refs.clear();
	initialized = false;
}

bool AngelScriptEngine::LoadMod(const ModInfo& mod) {
	if (!initialized || !as_engine) return false;

	// Read the script file
	std::ifstream file(mod.script_path);
	if (!file.is_open()) {
		Output::Warning("AngelScript: Could not open '{}'", mod.script_path);
		return false;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string code = buffer.str();

	// Create a module for this mod
	asIScriptModule* module = as_engine->GetModule(mod.id.c_str(), asGM_ALWAYS_CREATE);
	if (!module) {
		Output::Warning("AngelScript: Could not create module for '{}'", mod.id);
		return false;
	}

	module->AddScriptSection(mod.script_path.c_str(), code.c_str(), code.size());

	int r = module->Build();
	if (r < 0) {
		Output::Warning("AngelScript: Build failed for mod '{}'", mod.id);
		return false;
	}

	modules[mod.id] = module;

	// Look for well-known entry points and store refs
	// Convention: attack_start()/attack_update() for attacks,
	// enemy_spawn()/enemy_update() for enemies, etc.

	// Execute the module's init (runs top-level code which calls register_* functions)
	asIScriptFunction* initFunc = module->GetFunctionByDecl("void init()");
	if (initFunc) {
		asIScriptContext* ctx = as_engine->CreateContext();
		ctx->Prepare(initFunc);
		ctx->Execute();
		ctx->Release();
	}

	// Now find and store callback function references for registered content
	auto mod_attacks = ModRegistry::Instance().GetAttacksByMod(mod.id);
	for (auto* atk : mod_attacks) {
		std::string start_name = atk->id + "_start";
		std::string update_name = atk->id + "_update";
		asIScriptFunction* start_fn = module->GetFunctionByName(start_name.c_str());
		asIScriptFunction* update_fn = module->GetFunctionByName(update_name.c_str());
		if (start_fn) {
			int ref = next_ref++;
			function_refs[ref] = start_fn;
			const_cast<ScriptedAttackDef*>(atk)->on_start_ref = ref;
		}
		if (update_fn) {
			int ref = next_ref++;
			function_refs[ref] = update_fn;
			const_cast<ScriptedAttackDef*>(atk)->on_update_ref = ref;
		}
	}

	Output::Debug("AngelScript: Loaded mod '{}'", mod.id);
	return true;
}

asIScriptFunction* AngelScriptEngine::GetFunction(int ref) {
	auto it = function_refs.find(ref);
	return it != function_refs.end() ? it->second : nullptr;
}

void AngelScriptEngine::CallAttackStart(const ScriptedAttackDef& def, ScriptBulletContext& ctx) {
	auto* fn = GetFunction(def.on_start_ref);
	if (!fn) return;

	g_bullet_ctx = &ctx;
	asIScriptContext* as_ctx = as_engine->CreateContext();
	as_ctx->Prepare(fn);
	as_ctx->Execute();
	as_ctx->Release();
	g_bullet_ctx = nullptr;
}

bool AngelScriptEngine::CallAttackUpdate(const ScriptedAttackDef& def, ScriptBulletContext& ctx) {
	auto* fn = GetFunction(def.on_update_ref);
	if (!fn) return true; // No update = immediately done

	g_bullet_ctx = &ctx;
	asIScriptContext* as_ctx = as_engine->CreateContext();
	as_ctx->Prepare(fn);
	int r = as_ctx->Execute();
	bool done = false;
	if (r == asEXECUTION_FINISHED) {
		done = as_ctx->GetReturnByte() != 0;
	}
	as_ctx->Release();
	g_bullet_ctx = nullptr;
	return done;
}

void AngelScriptEngine::CallEnemySpawn(const ScriptedEnemyDef& def) {
	auto* fn = GetFunction(def.on_spawn_ref);
	if (!fn) return;
	asIScriptContext* ctx = as_engine->CreateContext();
	ctx->Prepare(fn);
	ctx->Execute();
	ctx->Release();
}

void AngelScriptEngine::CallEnemyUpdate(const ScriptedEnemyDef& def) {
	auto* fn = GetFunction(def.on_update_ref);
	if (!fn) return;
	asIScriptContext* ctx = as_engine->CreateContext();
	ctx->Prepare(fn);
	ctx->Execute();
	ctx->Release();
}

void AngelScriptEngine::CallGamemodeStart(const ScriptedGamemodeDef& def) {
	auto* fn = GetFunction(def.on_start_ref);
	if (!fn) return;
	asIScriptContext* ctx = as_engine->CreateContext();
	ctx->Prepare(fn);
	ctx->Execute();
	ctx->Release();
}

void AngelScriptEngine::CallGamemodeUpdate(const ScriptedGamemodeDef& def) {
	auto* fn = GetFunction(def.on_update_ref);
	if (!fn) return;
	asIScriptContext* ctx = as_engine->CreateContext();
	ctx->Prepare(fn);
	ctx->Execute();
	ctx->Release();
}

void AngelScriptEngine::CallGamemodeBattleStart(const ScriptedGamemodeDef& def) {
	auto* fn = GetFunction(def.on_battle_start_ref);
	if (!fn) return;
	asIScriptContext* ctx = as_engine->CreateContext();
	ctx->Prepare(fn);
	ctx->Execute();
	ctx->Release();
}

void AngelScriptEngine::CallGamemodeBattleEnd(const ScriptedGamemodeDef& def) {
	auto* fn = GetFunction(def.on_battle_end_ref);
	if (!fn) return;
	asIScriptContext* ctx = as_engine->CreateContext();
	ctx->Prepare(fn);
	ctx->Execute();
	ctx->Release();
}

#else // !CHAOS_HAS_ANGELSCRIPT

// ---- Stub implementation when AngelScript is not available ----

bool AngelScriptEngine::Init() {
	Output::Debug("AngelScript: Not available (compiled without CHAOS_HAS_ANGELSCRIPT)");
	return false;
}

void AngelScriptEngine::Shutdown() {}

bool AngelScriptEngine::LoadMod(const ModInfo&) {
	Output::Warning("AngelScript: Cannot load mod — engine not available");
	return false;
}

void AngelScriptEngine::CallAttackStart(const ScriptedAttackDef&, ScriptBulletContext&) {}
bool AngelScriptEngine::CallAttackUpdate(const ScriptedAttackDef&, ScriptBulletContext&) { return true; }
void AngelScriptEngine::CallEnemySpawn(const ScriptedEnemyDef&) {}
void AngelScriptEngine::CallEnemyUpdate(const ScriptedEnemyDef&) {}
void AngelScriptEngine::CallGamemodeStart(const ScriptedGamemodeDef&) {}
void AngelScriptEngine::CallGamemodeUpdate(const ScriptedGamemodeDef&) {}
void AngelScriptEngine::CallGamemodeBattleStart(const ScriptedGamemodeDef&) {}
void AngelScriptEngine::CallGamemodeBattleEnd(const ScriptedGamemodeDef&) {}

#endif // CHAOS_HAS_ANGELSCRIPT

} // namespace Chaos
