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

#ifndef EP_CHAOS_DISCORD_INTEGRATION_H
#define EP_CHAOS_DISCORD_INTEGRATION_H

#include <string>
#include <functional>

namespace Chaos {

/**
 * Discord Rich Presence integration.
 * Shows the current game state in the user's Discord profile.
 * Supports join/invite via Discord and retrieving the Discord username.
 */
namespace DiscordIntegration {

#ifdef HAVE_DISCORD_RPC

	void Initialize();
	void Shutdown();
	void Update();
	bool IsEnabled();
	void SetEnabled(bool enabled);
	bool HasDiscordUser();
	const std::string& GetDiscordUsername();
	const std::string& GetDiscordUserId();
	void SetJoinSecret(const std::string& secret);
	void SetPartyInfo(const std::string& party_id, int current_size, int max_size);
	void ClearMultiplayerPresence();
	void SetJoinCallback(std::function<void(const std::string&)> callback);

#else

	// No-op stubs when Discord RPC is not available
	inline void Initialize() {}
	inline void Shutdown() {}
	inline void Update() {}
	inline bool IsEnabled() { return false; }
	inline void SetEnabled(bool) {}
	inline bool HasDiscordUser() { return false; }
	inline const std::string& GetDiscordUsername() { static const std::string empty; return empty; }
	inline const std::string& GetDiscordUserId() { static const std::string empty; return empty; }
	inline void SetJoinSecret(const std::string&) {}
	inline void SetPartyInfo(const std::string&, int, int) {}
	inline void ClearMultiplayerPresence() {}
	inline void SetJoinCallback(std::function<void(const std::string&)>) {}

#endif

} // namespace DiscordIntegration
} // namespace Chaos

#endif
