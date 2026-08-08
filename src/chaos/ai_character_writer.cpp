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

#include "ai_character_writer.h"
#include "ai_characters.h"
#include "../output.h"

namespace Chaos {
namespace AICharacterWriter {

std::string GenerateDialogue(
	const AICharacters::CharacterDef& character,
	const std::string& context)
{
	// TODO: Hook into the Inworld API using BuildCharacterPrompt()
	// For now, return a placeholder
	Output::Debug("Chaos AI CharacterWriter: GenerateDialogue called for '{}' (not yet implemented)",
		character.name);
	return "(AI Character dialogue not yet implemented)";
}

std::vector<std::string> RewriteAsCharacter(
	const AICharacters::CharacterDef& character,
	const std::vector<std::string>& original_lines)
{
	// TODO: Hook into the Inworld API using BuildCharacterPrompt()
	// For now, return the original lines unchanged
	Output::Debug("Chaos AI CharacterWriter: RewriteAsCharacter called for '{}' (not yet implemented)",
		character.name);
	return original_lines;
}

} // namespace AICharacterWriter
} // namespace Chaos
