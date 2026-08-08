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

#ifndef EP_CHAOS_AI_CHARACTERS_H
#define EP_CHAOS_AI_CHARACTERS_H

#include <string>
#include <vector>

namespace Chaos {

/**
 * AI Characters module.
 * Lets you create custom AI-driven characters with their own personalities,
 * writing styles, and assets (charsets, facesets, portraits).
 * Characters can be added alongside the protagonist or replace them.
 *
 * Character data is stored as JSON in:
 *   ~/.config/EasyRPG/Player/Characters/
 * Character assets are stored in:
 *   ~/.config/EasyRPG/Player/Characters/Assets/
 *     - Charsets/
 *     - Facesets/
 *     - Portraits/
 *     - Extra/
 *
 * Up to 4 characters can be active at once.
 */
namespace AICharacters {

	// ── Mode ───────────────────────────────────────────────────────────────
	enum class Mode {
		Off,      // Feature disabled
		Add,      // AI characters join alongside the protagonist
		Replace   // AI characters replace the protagonist
	};

	Mode GetMode();
	void SetMode(Mode mode);

	/** Cycle through modes: Off -> Add -> Replace -> Off */
	void CycleMode();

	/** @return display string for the current mode */
	const char* GetModeString();

	// ── Personality Types ──────────────────────────────────────────────────
	enum class Personality {
		Kind,
		Shy,
		Sensitive,
		Bold,
		Sarcastic,
		Chaotic,
		Mysterious,
		Cheerful,
		Grumpy,
		Dramatic,
		Custom
	};

	const char* PersonalityToString(Personality p);
	Personality StringToPersonality(const std::string& s);

	// ── Writing Styles ─────────────────────────────────────────────────────
	enum class WritingStyle {
		Cute,
		Discord,
		Undertale,
		Deranged,
		Okegom,
		Formal,
		Poetic,
		Edgy,
		Valley,
		Custom
	};

	const char* WritingStyleToString(WritingStyle w);
	WritingStyle StringToWritingStyle(const std::string& s);

	// ── Safety Settings ────────────────────────────────────────────────────
	struct SafetySettings {
		bool can_swear = false;
		bool fourth_wall_breaking = false;
	};

	// ── Character Definition ───────────────────────────────────────────────
	struct CharacterDef {
		std::string id;            // unique filename-safe identifier
		std::string name;          // display name
		std::string description;   // short bio

		Personality personality = Personality::Kind;
		WritingStyle writing_style = WritingStyle::Cute;
		SafetySettings safety;

		std::string custom_system_prompt; // optional override prompt

		// Asset filenames (relative to Characters/Assets/<type>/)
		std::string charset_file;
		std::string faceset_file;
		std::string portrait_file;

		bool enabled = false;      // whether this character is in the active party
	};

	// ── Character Management ───────────────────────────────────────────────
	static constexpr int MAX_ACTIVE_CHARACTERS = 4;

	/** Load all character definitions from disk */
	void LoadCharacters();

	/** Save all character definitions to disk */
	void SaveCharacters();

	/** @return all known character definitions */
	std::vector<CharacterDef>& GetAllCharacters();

	/** @return only the enabled characters (up to MAX_ACTIVE_CHARACTERS) */
	std::vector<const CharacterDef*> GetActiveCharacters();

	/** @return the number of currently enabled characters */
	int GetActiveCount();

	/**
	 * Enable a character by index. Fails if MAX_ACTIVE already reached.
	 * @return true if successfully enabled
	 */
	bool EnableCharacter(int index);

	/** Disable a character by index */
	void DisableCharacter(int index);

	/**
	 * Create a new character with default values.
	 * @return index of the new character, or -1 on failure
	 */
	int CreateCharacter(const std::string& name);

	/** Delete a character by index (removes JSON file too) */
	void DeleteCharacter(int index);

	// ── Path Helpers ───────────────────────────────────────────────────────
	std::string GetCharactersDir();
	std::string GetAssetsDir();
	void EnsureDirectories();

	/**
	 * Build the system prompt snippet for a given character.
	 * Combines personality, writing style, safety and custom prompt.
	 */
	std::string BuildCharacterPrompt(const CharacterDef& ch);

} // namespace AICharacters
} // namespace Chaos

#endif
