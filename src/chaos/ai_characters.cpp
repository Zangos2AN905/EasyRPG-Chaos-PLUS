/*
 * This file is part of EasyRPG Player (Chaos Fork).
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ai_characters.h"
#include "../output.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#include <unistd.h>
#endif
#include <filesystem>

namespace {

// ── State ──────────────────────────────────────────────────────────────────────
Chaos::AICharacters::Mode current_mode = Chaos::AICharacters::Mode::Off;
std::vector<Chaos::AICharacters::CharacterDef> all_characters;
bool characters_loaded = false;

// ── Filesystem helpers ─────────────────────────────────────────────────────────
bool MakeDirRecursive(const std::string& path) {
	size_t pos = 0;
	while (pos < path.size()) {
		pos = path.find('/', pos + 1);
		if (pos == std::string::npos) pos = path.size();
		std::string sub = path.substr(0, pos);
		if (!sub.empty()) {
			mkdir(sub.c_str(), 0755);
		}
	}
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string GetBaseDir() {
	const char* xdg = std::getenv("XDG_CONFIG_HOME");
	std::string dir;
	if (xdg && xdg[0]) {
		dir = xdg;
	} else {
		const char* home = std::getenv("HOME");
		if (home) {
			dir = std::string(home) + "/.config";
		} else {
			return {};
		}
	}
	return dir + "/EasyRPG/Player";
}

// ── Minimal JSON helpers ───────────────────────────────────────────────────────
std::string JsonEscapeChar(const std::string& s) {
	std::string out;
	out.reserve(s.size() + 16);
	for (char c : s) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
	return out;
}

std::string ReadJsonString(const std::string& json, const std::string& key) {
	std::string search = "\"" + key + "\"";
	auto pos = json.find(search);
	if (pos == std::string::npos) return {};

	auto colon = json.find(':', pos + search.size());
	if (colon == std::string::npos) return {};

	auto start = json.find('"', colon + 1);
	if (start == std::string::npos) return {};
	++start;

	std::string result;
	for (size_t i = start; i < json.size(); ++i) {
		if (json[i] == '\\' && i + 1 < json.size()) {
			char next = json[i + 1];
			switch (next) {
				case '"':  result += '"';  break;
				case '\\': result += '\\'; break;
				case 'n':  result += '\n'; break;
				case 'r':  break;
				case 't':  result += '\t'; break;
				default:   result += next;  break;
			}
			++i;
		} else if (json[i] == '"') {
			break;
		} else {
			result += json[i];
		}
	}
	return result;
}

bool ReadJsonBool(const std::string& json, const std::string& key, bool fallback = false) {
	std::string search = "\"" + key + "\"";
	auto pos = json.find(search);
	if (pos == std::string::npos) return fallback;

	auto colon = json.find(':', pos + search.size());
	if (colon == std::string::npos) return fallback;

	// skip whitespace
	size_t i = colon + 1;
	while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n')) ++i;

	if (i + 4 <= json.size() && json.substr(i, 4) == "true") return true;
	if (i + 5 <= json.size() && json.substr(i, 5) == "false") return false;
	return fallback;
}

// ── Character file I/O ────────────────────────────────────────────────────────

void SaveCharacterFile(const Chaos::AICharacters::CharacterDef& ch, const std::string& dir) {
	std::string path = dir + "/" + ch.id + ".json";
	std::ofstream file(path);
	if (!file.is_open()) {
		Output::Warning("Chaos AI Characters: failed to write {}", path);
		return;
	}

	file << "{\n";
	file << "  \"id\": \"" << JsonEscapeChar(ch.id) << "\",\n";
	file << "  \"name\": \"" << JsonEscapeChar(ch.name) << "\",\n";
	file << "  \"description\": \"" << JsonEscapeChar(ch.description) << "\",\n";
	file << "  \"personality\": \"" << Chaos::AICharacters::PersonalityToString(ch.personality) << "\",\n";
	file << "  \"writing_style\": \"" << Chaos::AICharacters::WritingStyleToString(ch.writing_style) << "\",\n";
	file << "  \"can_swear\": " << (ch.safety.can_swear ? "true" : "false") << ",\n";
	file << "  \"fourth_wall_breaking\": " << (ch.safety.fourth_wall_breaking ? "true" : "false") << ",\n";
	file << "  \"custom_system_prompt\": \"" << JsonEscapeChar(ch.custom_system_prompt) << "\",\n";
	file << "  \"charset_file\": \"" << JsonEscapeChar(ch.charset_file) << "\",\n";
	file << "  \"faceset_file\": \"" << JsonEscapeChar(ch.faceset_file) << "\",\n";
	file << "  \"portrait_file\": \"" << JsonEscapeChar(ch.portrait_file) << "\",\n";
	file << "  \"enabled\": " << (ch.enabled ? "true" : "false") << "\n";
	file << "}\n";

	file.close();
}

Chaos::AICharacters::CharacterDef LoadCharacterFile(const std::string& path) {
	Chaos::AICharacters::CharacterDef ch;

	std::ifstream file(path);
	if (!file.is_open()) return ch;

	std::string json((std::istreambuf_iterator<char>(file)),
					  std::istreambuf_iterator<char>());
	file.close();

	ch.id = ReadJsonString(json, "id");
	ch.name = ReadJsonString(json, "name");
	ch.description = ReadJsonString(json, "description");
	ch.personality = Chaos::AICharacters::StringToPersonality(ReadJsonString(json, "personality"));
	ch.writing_style = Chaos::AICharacters::StringToWritingStyle(ReadJsonString(json, "writing_style"));
	ch.safety.can_swear = ReadJsonBool(json, "can_swear");
	ch.safety.fourth_wall_breaking = ReadJsonBool(json, "fourth_wall_breaking");
	ch.custom_system_prompt = ReadJsonString(json, "custom_system_prompt");
	ch.charset_file = ReadJsonString(json, "charset_file");
	ch.faceset_file = ReadJsonString(json, "faceset_file");
	ch.portrait_file = ReadJsonString(json, "portrait_file");
	ch.enabled = ReadJsonBool(json, "enabled");

	return ch;
}

// Make a filename-safe ID from a display name
std::string SanitizeId(const std::string& name) {
	std::string id;
	for (char c : name) {
		if (std::isalnum(static_cast<unsigned char>(c))) {
			id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		} else if (c == ' ' || c == '-' || c == '_') {
			if (!id.empty() && id.back() != '_') id += '_';
		}
	}
	// trim trailing underscores
	while (!id.empty() && id.back() == '_') id.pop_back();
	if (id.empty()) id = "character";
	return id;
}

} // anonymous namespace

namespace Chaos {
namespace AICharacters {

// ── Mode ───────────────────────────────────────────────────────────────────────

Mode GetMode() {
	return current_mode;
}

void SetMode(Mode mode) {
	current_mode = mode;
	Output::Debug("Chaos AI Characters: mode set to {}", GetModeString());
}

void CycleMode() {
	switch (current_mode) {
		case Mode::Off:     SetMode(Mode::Add); break;
		case Mode::Add:     SetMode(Mode::Replace); break;
		case Mode::Replace: SetMode(Mode::Off); break;
	}
}

const char* GetModeString() {
	switch (current_mode) {
		case Mode::Off:     return "OFF";
		case Mode::Add:     return "ADD";
		case Mode::Replace: return "REPLACE";
	}
	return "OFF";
}

// ── Enums ──────────────────────────────────────────────────────────────────────

const char* PersonalityToString(Personality p) {
	switch (p) {
		case Personality::Kind:       return "Kind";
		case Personality::Shy:        return "Shy";
		case Personality::Sensitive:  return "Sensitive";
		case Personality::Bold:       return "Bold";
		case Personality::Sarcastic:  return "Sarcastic";
		case Personality::Chaotic:    return "Chaotic";
		case Personality::Mysterious: return "Mysterious";
		case Personality::Cheerful:   return "Cheerful";
		case Personality::Grumpy:     return "Grumpy";
		case Personality::Dramatic:   return "Dramatic";
		case Personality::Custom:     return "Custom";
	}
	return "Kind";
}

Personality StringToPersonality(const std::string& s) {
	if (s == "Kind")       return Personality::Kind;
	if (s == "Shy")        return Personality::Shy;
	if (s == "Sensitive")  return Personality::Sensitive;
	if (s == "Bold")       return Personality::Bold;
	if (s == "Sarcastic")  return Personality::Sarcastic;
	if (s == "Chaotic")    return Personality::Chaotic;
	if (s == "Mysterious") return Personality::Mysterious;
	if (s == "Cheerful")   return Personality::Cheerful;
	if (s == "Grumpy")     return Personality::Grumpy;
	if (s == "Dramatic")   return Personality::Dramatic;
	if (s == "Custom")     return Personality::Custom;
	return Personality::Kind;
}

const char* WritingStyleToString(WritingStyle w) {
	switch (w) {
		case WritingStyle::Cute:      return "Cute";
		case WritingStyle::Discord:   return "Discord";
		case WritingStyle::Undertale: return "Undertale";
		case WritingStyle::Deranged:  return "Deranged";
		case WritingStyle::Okegom:    return "Okegom";
		case WritingStyle::Formal:    return "Formal";
		case WritingStyle::Poetic:    return "Poetic";
		case WritingStyle::Edgy:      return "Edgy";
		case WritingStyle::Valley:    return "Valley";
		case WritingStyle::Custom:    return "Custom";
	}
	return "Cute";
}

WritingStyle StringToWritingStyle(const std::string& s) {
	if (s == "Cute")      return WritingStyle::Cute;
	if (s == "Discord")   return WritingStyle::Discord;
	if (s == "Undertale") return WritingStyle::Undertale;
	if (s == "Deranged")  return WritingStyle::Deranged;
	if (s == "Okegom")    return WritingStyle::Okegom;
	if (s == "Formal")    return WritingStyle::Formal;
	if (s == "Poetic")    return WritingStyle::Poetic;
	if (s == "Edgy")      return WritingStyle::Edgy;
	if (s == "Valley")    return WritingStyle::Valley;
	if (s == "Custom")    return WritingStyle::Custom;
	return WritingStyle::Cute;
}

// ── Paths ──────────────────────────────────────────────────────────────────────

std::string GetCharactersDir() {
	std::string base = GetBaseDir();
	if (base.empty()) return {};
	return base + "/Characters";
}

std::string GetAssetsDir() {
	return GetCharactersDir() + "/Assets";
}

void EnsureDirectories() {
	std::string chars_dir = GetCharactersDir();
	if (chars_dir.empty()) return;

	MakeDirRecursive(chars_dir);
	MakeDirRecursive(GetAssetsDir() + "/Charsets");
	MakeDirRecursive(GetAssetsDir() + "/Facesets");
	MakeDirRecursive(GetAssetsDir() + "/Portraits");
	MakeDirRecursive(GetAssetsDir() + "/Extra");
}

// ── Character Management ───────────────────────────────────────────────────────

void LoadCharacters() {
	if (characters_loaded) return;
	characters_loaded = true;

	EnsureDirectories();

	std::string dir = GetCharactersDir();
	if (dir.empty()) return;

	all_characters.clear();

	std::error_code ec;
	for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) break;
		if (!entry.is_regular_file()) continue;
		std::string name = entry.path().filename().string();
		if (name.size() < 6) continue; // need at least "x.json"
		if (name.substr(name.size() - 5) != ".json") continue;

		std::string path = entry.path().string();
		auto ch = LoadCharacterFile(path);
		if (!ch.id.empty()) {
			all_characters.push_back(std::move(ch));
			Output::Debug("Chaos AI Characters: loaded '{}'", all_characters.back().name);
		}
	}

	Output::Debug("Chaos AI Characters: loaded {} characters from {}", all_characters.size(), dir);
}

void SaveCharacters() {
	EnsureDirectories();
	std::string dir = GetCharactersDir();
	if (dir.empty()) return;

	for (auto& ch : all_characters) {
		SaveCharacterFile(ch, dir);
	}
	Output::Debug("Chaos AI Characters: saved {} characters", all_characters.size());
}

std::vector<CharacterDef>& GetAllCharacters() {
	if (!characters_loaded) LoadCharacters();
	return all_characters;
}

std::vector<const CharacterDef*> GetActiveCharacters() {
	std::vector<const CharacterDef*> active;
	for (auto& ch : GetAllCharacters()) {
		if (ch.enabled) {
			active.push_back(&ch);
			if (static_cast<int>(active.size()) >= MAX_ACTIVE_CHARACTERS) break;
		}
	}
	return active;
}

int GetActiveCount() {
	int count = 0;
	for (auto& ch : GetAllCharacters()) {
		if (ch.enabled) ++count;
	}
	return count;
}

bool EnableCharacter(int index) {
	auto& chars = GetAllCharacters();
	if (index < 0 || index >= static_cast<int>(chars.size())) return false;
	if (chars[index].enabled) return true;

	if (GetActiveCount() >= MAX_ACTIVE_CHARACTERS) {
		Output::Debug("Chaos AI Characters: max {} active characters reached", MAX_ACTIVE_CHARACTERS);
		return false;
	}

	chars[index].enabled = true;
	SaveCharacters();
	Output::Debug("Chaos AI Characters: enabled '{}'", chars[index].name);
	return true;
}

void DisableCharacter(int index) {
	auto& chars = GetAllCharacters();
	if (index < 0 || index >= static_cast<int>(chars.size())) return;

	chars[index].enabled = false;
	SaveCharacters();
	Output::Debug("Chaos AI Characters: disabled '{}'", chars[index].name);
}

int CreateCharacter(const std::string& name) {
	EnsureDirectories();

	CharacterDef ch;
	ch.id = SanitizeId(name);
	ch.name = name;
	ch.description = "A new AI character.";
	ch.personality = Personality::Kind;
	ch.writing_style = WritingStyle::Cute;

	// Check for ID collision
	for (auto& existing : GetAllCharacters()) {
		if (existing.id == ch.id) {
			// Append a number
			int suffix = 2;
			while (true) {
				std::string try_id = ch.id + "_" + std::to_string(suffix);
				bool found = false;
				for (auto& e : all_characters) {
					if (e.id == try_id) { found = true; break; }
				}
				if (!found) { ch.id = try_id; break; }
				++suffix;
			}
		}
	}

	std::string dir = GetCharactersDir();
	SaveCharacterFile(ch, dir);

	all_characters.push_back(std::move(ch));
	int idx = static_cast<int>(all_characters.size()) - 1;
	Output::Debug("Chaos AI Characters: created '{}' (id={})", all_characters[idx].name, all_characters[idx].id);
	return idx;
}

void DeleteCharacter(int index) {
	auto& chars = GetAllCharacters();
	if (index < 0 || index >= static_cast<int>(chars.size())) return;

	// Remove JSON file
	std::string path = GetCharactersDir() + "/" + chars[index].id + ".json";
	std::remove(path.c_str());

	Output::Debug("Chaos AI Characters: deleted '{}'", chars[index].name);
	chars.erase(chars.begin() + index);
}

// ── Prompt Builder ─────────────────────────────────────────────────────────────

std::string BuildCharacterPrompt(const CharacterDef& ch) {
	// If custom prompt is set, use it directly
	if (!ch.custom_system_prompt.empty()) {
		return ch.custom_system_prompt;
	}

	std::string prompt;
	prompt += "You are roleplaying as a character named " + ch.name + ".\n";

	if (!ch.description.empty()) {
		prompt += "Bio: " + ch.description + "\n";
	}

	// Personality
	prompt += "Personality: ";
	switch (ch.personality) {
		case Personality::Kind:
			prompt += "You are kind, warm, and supportive. You always try to help others and see the good in people.";
			break;
		case Personality::Shy:
			prompt += "You are timid and soft-spoken. You stammer sometimes and avoid confrontation, but you're brave when it counts.";
			break;
		case Personality::Sensitive:
			prompt += "You are highly emotional and empathetic. You feel things deeply and express your feelings openly.";
			break;
		case Personality::Bold:
			prompt += "You are confident, fearless, and always charge in headfirst. You speak your mind without hesitation.";
			break;
		case Personality::Sarcastic:
			prompt += "You are witty and sarcastic. Everything you say drips with dry humor and clever comebacks.";
			break;
		case Personality::Chaotic:
			prompt += "You are unpredictable and wild. You say random things, make bizarre connections, and live for chaos.";
			break;
		case Personality::Mysterious:
			prompt += "You are enigmatic and speak in riddles. You know more than you let on and drop cryptic hints.";
			break;
		case Personality::Cheerful:
			prompt += "You are bubbly, energetic, and relentlessly positive. Everything is exciting and you love exclamation marks!";
			break;
		case Personality::Grumpy:
			prompt += "You are irritable and complain about everything. Deep down you care, but you'd never admit it.";
			break;
		case Personality::Dramatic:
			prompt += "You are theatrical and over-the-top. Every situation is life or death. You monologue constantly.";
			break;
		case Personality::Custom:
			prompt += "Custom personality.";
			break;
	}
	prompt += "\n";

	// Writing style
	prompt += "Writing style: ";
	switch (ch.writing_style) {
		case WritingStyle::Cute:
			prompt += "Use cute expressions, emoticons in words (not actual emojis), soft language, and lots of '~' tildes.";
			break;
		case WritingStyle::Discord:
			prompt += "Write like a Discord user. Casual, uses abbreviations, 'lmao', 'ngl', 'fr fr', 'bruh', lowercase typing.";
			break;
		case WritingStyle::Undertale:
			prompt += "Write like Undertale characters. Reference determination, mercy/fight choices, and Toby Fox's style of humor.";
			break;
		case WritingStyle::Deranged:
			prompt += "Write completely unhinged. Random capitalization, bizarre tangents, stream of consciousness, fever dream energy.";
			break;
		case WritingStyle::Okegom:
			prompt += "Write in the style of Okegom/Deep-Sea Prisoner games. Cute but dark, unsettling undertones beneath sweet surfaces. "
					  "Reference cookie themes, sea creatures, angels and demons, moral ambiguity.";
			break;
		case WritingStyle::Formal:
			prompt += "Write in formal, proper English. Complete sentences, no slang, dignified and measured tone.";
			break;
		case WritingStyle::Poetic:
			prompt += "Write poetically. Use metaphors, lyrical phrasing, sometimes rhyme. Everything sounds like prose.";
			break;
		case WritingStyle::Edgy:
			prompt += "Write dark and edgy. References to shadows, pain, being misunderstood. Think early 2000s DeviantArt OCs.";
			break;
		case WritingStyle::Valley:
			prompt += "Write like a Valley girl. 'Like, totally', 'oh my god', 'literally', 'I can't even'. Dramatic about trivial things.";
			break;
		case WritingStyle::Custom:
			prompt += "Custom writing style.";
			break;
	}
	prompt += "\n";

	// Safety settings
	if (ch.safety.can_swear) {
		prompt += "You are allowed to swear freely for comedic or dramatic effect. No slurs though.\n";
	} else {
		prompt += "Do NOT swear or use profanity.\n";
	}

	if (ch.safety.fourth_wall_breaking) {
		prompt += "You are aware you're in a video game and can break the fourth wall. "
				  "Reference the player, the game engine, RPG Maker, or meta-game concepts.\n";
	}

	prompt += "Keep your dialogue short and punchy. This is an RPG text box with limited space.\n";
	prompt += "Do NOT use markdown formatting. Plain text only.\n";

	return prompt;
}

} // namespace AICharacters
} // namespace Chaos
