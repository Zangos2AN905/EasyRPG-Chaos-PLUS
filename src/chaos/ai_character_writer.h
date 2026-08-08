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

#ifndef EP_CHAOS_AI_CHARACTER_WRITER_H
#define EP_CHAOS_AI_CHARACTER_WRITER_H

#include <string>
#include <vector>
#include "ai_characters.h"

namespace Chaos {

/**
 * AI Character Writer module.
 * Generates dialogue for AI characters using their personality,
 * writing style, and safety settings to produce in-character text.
 *
 * Future features:
 * - Scripting support
 * - Fourth-wall breaking (show message boxes, interact with computer)
 */
namespace AICharacterWriter {

	/**
	 * Generate a dialogue line for a character given a game context.
	 *
	 * @param character the character definition
	 * @param context description of the current game situation
	 * @return generated dialogue line
	 */
	std::string GenerateDialogue(
		const AICharacters::CharacterDef& character,
		const std::string& context);

	/**
	 * Rewrite existing dialogue as if spoken by this character.
	 *
	 * @param character the character definition
	 * @param original_lines the original dialogue
	 * @return rewritten lines in-character
	 */
	std::vector<std::string> RewriteAsCharacter(
		const AICharacters::CharacterDef& character,
		const std::vector<std::string>& original_lines);

} // namespace AICharacterWriter
} // namespace Chaos

#endif
