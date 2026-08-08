/*
 * Chaos Fork: Mod Loader
 *
 * Discovers and loads mods from the mods/ directory.
 * Each mod lives in its own subfolder with:
 *   - mod.as  (AngelScript) or mod.lua (Lua) — main script
 *   - mod.json (optional) — metadata (name, author, version, description)
 *   - assets/ (optional) — mod-specific assets
 */

#ifndef EP_CHAOS_MOD_LOADER_H
#define EP_CHAOS_MOD_LOADER_H

#include "chaos/mod_api.h"
#include "chaos/mod_script_engine.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Chaos {

class ModLoader {
public:
	static ModLoader& Instance();

	/**
	 * Scan the mods/ directory for mods, detect language, and load them.
	 * Call once at startup (or when refreshing the mod list).
	 */
	void DiscoverAndLoadAll();

	/** Load mods once if discovery has not happened yet. */
	void EnsureLoaded();

	/** Unload all mods and release engines. */
	void UnloadAll();

	/** Get the script engine for a given mod (by mod id). */
	ScriptEngine* GetEngineForMod(const std::string& mod_id) const;

	/** Get all loaded mod IDs. */
	std::vector<std::string> GetLoadedModIds() const;

	/** Returns true if at least one mod is loaded. */
	bool HasMods() const { return !mod_engines.empty(); }

private:
	ModLoader() = default;

	/** Try to load a single mod from the given directory path. */
	bool LoadMod(const std::string& dir_path, const std::string& folder_name);

	/** Parse mod.json if it exists, filling in ModInfo fields. */
	void ParseModJson(const std::string& json_path, ModInfo& info);

	/** Map from mod_id -> engine that owns it */
	std::unordered_map<std::string, ScriptEngine*> mod_engines;

	/** Owned engine instances */
	std::vector<std::unique_ptr<ScriptEngine>> engines;
	bool discovery_complete = false;
};

} // namespace Chaos

#endif // EP_CHAOS_MOD_LOADER_H
