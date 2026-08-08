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

#ifndef EP_CHAOS_AI_REWRITER_H
#define EP_CHAOS_AI_REWRITER_H

#include <cstdint>
#include <string>
#include <vector>

namespace Chaos {

/**
 * AI Rewriter module.
 * Rewrites RPG dialogue lines in the style of Snapcube fandubs
 * using the Inworld AI chat completions API (via libcurl).
 *
 * Works at the event level: scans ahead to collect ALL ShowMessage
 * blocks in the current event, rewrites them in one API call, and
 * caches the results. Individual messages then pop from the cache.
 */
namespace AIRewriter {

	/** @return whether the AI rewriter feature is enabled */
	bool IsEnabled();

	/** Enable or disable the AI rewriter */
	void SetEnabled(bool enabled);

	/**
	 * Collect all dialogue lines from an entire event's ShowMessage
	 * commands and rewrite them in a single API call.
	 * The rewritten lines are cached internally, grouped by message.
	 *
	 * @param all_messages vector of messages; each message is a vector of lines
	 */
	void CollectAndRewriteEvent(uintptr_t event_id, const std::vector<std::vector<std::string>>& all_messages, bool has_face);

	/**
	 * Pop the rewritten message matching the given original lines.
	 * Verifies the cached entry matches to handle conditional branches.
	 *
	 * @param original_lines the current message's original dialogue lines
	 * @return the rewritten lines, or empty if no match
	 */
	std::vector<std::string> PopRewrittenMessage(const std::vector<std::string>& original_lines);

	/**
	 * @return true if there are cached rewritten messages for this event
	 */
	bool HasCachedMessages(uintptr_t event_id);

	/**
	 * @return true if there are overflow message boxes waiting to be displayed
	 */
	bool HasOverflow();

	/**
	 * Pop the next overflow message box (max 4 lines).
	 * Should be displayed before processing the next real ShowMessage.
	 */
	std::vector<std::string> PopOverflow();

	/**
	 * Load long-term memory from the JSON file on disk.
	 * Called automatically on first use, but can be called explicitly.
	 * Path: ~/.config/EasyRPG/Player/ai_memory.json (respects XDG_CONFIG_HOME)
	 */
	void LoadMemory();

	/**
	 * Save long-term memory to the JSON file on disk.
	 */
	void SaveMemory();

	/**
	 * Consolidate memory: send recent entries to the AI for summarization
	 * and store the result as persistent notes. Called automatically every
	 * 10 rewrites, but can be triggered manually.
	 */
	void ConsolidateMemory();

} // namespace AIRewriter
} // namespace Chaos

#endif
