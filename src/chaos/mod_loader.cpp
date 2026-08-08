/*
 * Chaos Fork: Mod Loader implementation
 *
 * Scans the mods/ directory relative to the executable/game directory.
 * Each subfolder is a potential mod:
 *   - Contains mod.as => AngelScript mod
 *   - Contains mod.lua => Lua mod
 *   - Optional mod.json for metadata
 */

#include "chaos/mod_loader.h"
#include "chaos/mod_angelscript.h"
#include "chaos/mod_lua.h"
#include "output.h"
#include "filefinder.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Chaos {

ModLoader& ModLoader::Instance() {
	static ModLoader instance;
	return instance;
}

void ModLoader::EnsureLoaded() {
	if (!discovery_complete) {
		DiscoverAndLoadAll();
	}
}

void ModLoader::DiscoverAndLoadAll() {
	UnloadAll();
	discovery_complete = true;

	// Look for mods/ directory relative to current working directory
	// (which is typically the game directory)
	const fs::path mods_dir = "mods";

	std::error_code ec;
	if (!fs::exists(mods_dir, ec) || !fs::is_directory(mods_dir, ec)) {
		Output::Debug("ModLoader: No mods/ directory found");
		return;
	}

	Output::Debug("ModLoader: Scanning mods/ directory...");

	for (const auto& entry : fs::directory_iterator(mods_dir, ec)) {
		if (!entry.is_directory(ec)) continue;

		std::string folder_name = entry.path().filename().string();
		std::string dir_path = entry.path().string();

		if (!LoadMod(dir_path, folder_name)) {
			Output::Warning("ModLoader: Failed to load mod '{}'", folder_name);
		}
	}

	auto& registry = ModRegistry::Instance();
	Output::Debug("ModLoader: Loaded {} mod(s), {} attack(s), {} enemy(ies), {} gamemode(s)",
		registry.GetLoadedMods().size(),
		registry.GetAllAttacks().size(),
		registry.GetAllEnemies().size(),
		registry.GetAllGamemodes().size());
}

bool ModLoader::LoadMod(const std::string& dir_path, const std::string& folder_name) {
	fs::path mod_dir(dir_path);

	// Detect language by checking for mod.as or mod.lua
	fs::path as_path = mod_dir / "mod.as";
	fs::path lua_path = mod_dir / "mod.lua";

	std::error_code ec;
	bool has_as = fs::exists(as_path, ec);
	bool has_lua = fs::exists(lua_path, ec);

	if (!has_as && !has_lua) {
		Output::Debug("ModLoader: Skipping '{}' — no mod.as or mod.lua found", folder_name);
		return false;
	}

	// Prefer AngelScript if both exist
	ModLanguage lang = has_as ? ModLanguage::AngelScript : ModLanguage::Lua;
	std::string script_path = has_as ? as_path.string() : lua_path.string();

	// Build ModInfo
	ModInfo info;
	info.id = folder_name;
	info.name = folder_name; // Default, may be overridden by mod.json
	info.language = lang;
	info.script_path = script_path;

	fs::path assets_dir = mod_dir / "assets";
	if (fs::exists(assets_dir, ec) && fs::is_directory(assets_dir, ec)) {
		info.assets_path = assets_dir.string();
	}

	// Try to parse mod.json for metadata
	fs::path json_path = mod_dir / "mod.json";
	if (fs::exists(json_path, ec)) {
		ParseModJson(json_path.string(), info);
	}

	// Check if the script file has content
	std::ifstream check_file(script_path);
	if (!check_file.is_open()) {
		Output::Warning("ModLoader: Could not open script file for '{}'", folder_name);
		return false;
	}
	// Check if file is empty or whitespace-only
	std::string content;
	std::getline(check_file, content);
	check_file.seekg(0, std::ios::end);
	auto size = check_file.tellg();
	check_file.close();

	if (size <= 0) {
		Output::Debug("ModLoader: Skipping '{}' — script file is empty", folder_name);
		return false;
	}

	// Get or create the appropriate engine
	ScriptEngine* engine = nullptr;

	if (lang == ModLanguage::AngelScript) {
		// Find existing AngelScript engine or create one
		for (auto& e : engines) {
			auto* as_eng = dynamic_cast<AngelScriptEngine*>(e.get());
			if (as_eng) { engine = as_eng; break; }
		}
		if (!engine) {
			auto new_engine = std::make_unique<AngelScriptEngine>();
			if (!new_engine->Init()) {
				Output::Warning("ModLoader: AngelScript engine init failed");
				return false;
			}
			engine = new_engine.get();
			engines.push_back(std::move(new_engine));
		}
	} else {
		// Find existing Lua engine or create one
		for (auto& e : engines) {
			auto* lua_eng = dynamic_cast<LuaEngine*>(e.get());
			if (lua_eng) { engine = lua_eng; break; }
		}
		if (!engine) {
			auto new_engine = std::make_unique<LuaEngine>();
			if (!new_engine->Init()) {
				Output::Warning("ModLoader: Lua engine init failed");
				return false;
			}
			engine = new_engine.get();
			engines.push_back(std::move(new_engine));
		}
	}

	// Load the mod
	if (!engine->LoadMod(info)) {
		return false;
	}

	mod_engines[info.id] = engine;
	ModRegistry::Instance().RegisterMod(std::move(info));

	Output::Debug("ModLoader: Loaded mod '{}' ({})", folder_name,
		lang == ModLanguage::AngelScript ? "AngelScript" : "Lua");
	return true;
}

void ModLoader::ParseModJson(const std::string& json_path, ModInfo& info) {
	// Simple JSON parser for mod metadata
	// Expected format:
	// {
	//   "name": "My Mod",
	//   "author": "Author Name",
	//   "version": "1.0",
	//   "description": "A cool mod"
	// }
	std::ifstream file(json_path);
	if (!file.is_open()) return;

	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string json = buffer.str();

	// Very basic JSON value extraction (no dependency on a JSON library)
	auto extract = [&](const std::string& key) -> std::string {
		std::string search = "\"" + key + "\"";
		auto pos = json.find(search);
		if (pos == std::string::npos) return {};

		// Find the colon after the key
		pos = json.find(':', pos + search.size());
		if (pos == std::string::npos) return {};

		// Find opening quote of value
		pos = json.find('"', pos + 1);
		if (pos == std::string::npos) return {};

		// Find closing quote
		auto end = json.find('"', pos + 1);
		if (end == std::string::npos) return {};

		return json.substr(pos + 1, end - pos - 1);
	};

	auto val = extract("name");
	if (!val.empty()) info.name = val;

	val = extract("author");
	if (!val.empty()) info.author = val;

	val = extract("version");
	if (!val.empty()) info.version = val;

	val = extract("description");
	if (!val.empty()) info.description = val;
}

void ModLoader::UnloadAll() {
	mod_engines.clear();
	for (auto& e : engines) {
		e->Shutdown();
	}
	engines.clear();
	ModRegistry::Instance().Clear();
	discovery_complete = false;
}

ScriptEngine* ModLoader::GetEngineForMod(const std::string& mod_id) const {
	auto it = mod_engines.find(mod_id);
	return it != mod_engines.end() ? it->second : nullptr;
}

std::vector<std::string> ModLoader::GetLoadedModIds() const {
	std::vector<std::string> ids;
	ids.reserve(mod_engines.size());
	for (const auto& [id, engine] : mod_engines) {
		ids.push_back(id);
	}
	return ids;
}

} // namespace Chaos
