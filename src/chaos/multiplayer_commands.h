/*
 * Chaos Fork: Multiplayer chat commands.
 */

#ifndef EP_CHAOS_MULTIPLAYER_COMMANDS_H
#define EP_CHAOS_MULTIPLAYER_COMMANDS_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Chaos {

class MultiplayerCommands {
public:
	/** Submit a local semicolon command. Returns true when text was a command. */
	static bool Submit(const std::string& text);

	/** Handle a command packet received by the multiplayer state. */
	static void HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len);
};

} // namespace Chaos

#endif
