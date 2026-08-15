/*
 * Chaos Fork: Network Packet Protocol
 */

#ifndef EP_CHAOS_NET_PACKET_H
#define EP_CHAOS_NET_PACKET_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Chaos {

enum class PacketType : uint8_t {
	// Connection
	Join = 1,         // Client -> Server: request to join
	JoinAccept,       // Server -> Client: join accepted with assigned peer_id
	JoinReject,       // Server -> Client: join rejected (full, etc.)
	PlayerJoined,     // Server -> All: a new player joined
	PlayerLeft,       // Server -> All: a player left
	Disconnect,       // Client -> Server: graceful disconnect

	// Game state sync
	PlayerPosition,   // Bidirectional: player position/direction update
	SwitchSync,       // Server -> All: switch state changed
	VariableSync,     // Server -> All: variable state changed
	SwitchBulkSync,   // Server -> Client: bulk switch sync on join
	VariableBulkSync, // Server -> Client: bulk variable sync on join

	// Lobby
	LobbyInfo,        // Server -> Client: game info (mode, player list)
	GameStart,        // Server -> All: game is starting
	ChatMessage,      // Bidirectional: chat message

	// God Mode specific
	GodCommand,       // God -> All: control command (set switch, variable, teleport, stats, etc.)
	GodAssign,        // Server -> All: assign the god player (peer_id)

	// Host map ready
	HostMapReady,     // Server -> All: host entered a map (map_id, x, y)

	// Battle sync
	BattleStart,      // Initiator -> All: a battle started (troop_id, etc.)
	BattleJoin,       // Player -> Server: request to join battle
	BattleForce,      // Server -> All: force into battle (chaotix)
	BattleAction,     // Player -> All: action chosen for actor (actor_id, action_type, target, skill/item)
	BattleEnd,        // Server -> All: battle ended (result)

	// Event sync
	EventSync,        // Host -> All: event positions/directions on current map

	// Battle turn sync
	BattleTurnSync,   // Host -> All: execution phase starting (turn_seed)

	// Battle escape voting
	BattleEscapeVote, // Player -> All: escape vote (wants_escape bool)

	// Chaotix map sync
	MapChangeForce,   // Server -> All: force teleport to a map (chaotix)

	// Actor state sync (chaotix)
	ActorStateSync,   // Host -> All: actor HP/SP/states (chaotix)

	// Event trigger sync (chaotix)
	EventTriggerSync, // Player -> All: event was triggered by a player
	EventInputSync,   // Team -> All: event message input (origin peer + decision/cancel)
	SplitSideAssign,   // Host -> All: assign a player to normal or EXE side
	UnderwaterPocketSync, // Host -> All: synchronized air-pocket locations

	// Horror mode sync
	RawberrySync,     // Host -> All: Rawberry enemy position (horror mode)

	// ASYM mode sync
	AsymHunterAssign, // Host -> All: which player is the hunter
	AsymKill,         // Hunter -> All: hunter caught a survivor (victim peer_id)

	// Game file transfer
	GameFileRequest,  // Client -> Host: request game files for download
	GameFileInfo,     // Host -> Client: file_count (u16) + total_bytes (i32)
	GameFileData,     // Host -> Client: relative_path (str) + is_last (u8) + size (u16) + data
	GameFileDone,     // Host -> Client: all files transferred

	// Retained as a wire-protocol slot; voice chat is no longer implemented.
	VoiceData,

	// Player skins
	SkinSet,          // Player -> All: skin charset data (name, index, image bytes)

	// Custom mode
	CustomModeSettings, // Host -> All: custom mode mix/objective/toggles
	ChaosEvent,         // Host -> All: random chaos event to apply
	MultiplayerCommand, // Client -> Host -> All: validated admin/player command
	RadioQueueSync,     // Host -> All: authoritative radio queue
	RadioAddRequest,    // Client -> Host: add a game music track
	RadioCustomBegin,   // Client -> Host or Host -> All: custom music metadata
	RadioCustomChunk,   // Client -> Host or Host -> All: custom music data
	RadioCustomComplete,// Client -> Host or Host -> All: custom music finished
	RadioSkipRequest,   // Client -> Host: host/admin immediate skip
	RadioSkipVote,      // Client -> Host: vote to skip the current track
	SharedPlayerInput,  // Client -> Host: movement input for Single Mode
	RewindInput,        // Client -> Host: rewind key state
	RewindStatus,       // Host -> All: rewind active/inactive state
	RewindSnapshot,     // Host -> All: authoritative historical game state
	BoomboxState,       // Player -> Host -> All: player-specific music state
	BoomboxCustomBegin, // Client -> Host or Host -> All: boombox custom metadata
	BoomboxCustomChunk, // Client -> Host or Host -> All: boombox custom data
	BoomboxCustomComplete, // Client -> Host or Host -> All: boombox custom finished
};

// Simple packet buffer for serialization
class PacketWriter {
public:
	PacketWriter(PacketType type) {
		write(static_cast<uint8_t>(type));
	}

	void write(uint8_t v) { buf.push_back(v); }

	void write(uint16_t v) {
		buf.push_back(static_cast<uint8_t>(v & 0xFF));
		buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	}

	void write(int32_t v) {
		uint32_t u;
		std::memcpy(&u, &v, 4);
		buf.push_back(static_cast<uint8_t>(u & 0xFF));
		buf.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
		buf.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
		buf.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
	}

	void write(uint32_t v) {
		buf.push_back(static_cast<uint8_t>(v & 0xFF));
		buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
		buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
		buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
	}

	void write(const std::string& s) {
		write(static_cast<uint16_t>(s.size()));
		buf.insert(buf.end(), s.begin(), s.end());
	}

	void writeBytes(const uint8_t* d, size_t len) {
		buf.insert(buf.end(), d, d + len);
	}

	const uint8_t* data() const { return buf.data(); }
	size_t size() const { return buf.size(); }

private:
	std::vector<uint8_t> buf;
};

class PacketReader {
public:
	PacketReader(const uint8_t* data, size_t len)
		: buf(data), length(len), pos(0) {}

	PacketType readType() { return static_cast<PacketType>(readU8()); }

	bool ok() const { return !error; }

	uint8_t readU8() {
		if (pos >= length) { error = true; return 0; }
		return buf[pos++];
	}

	uint16_t readU16() {
		if (pos + 2 > length) { error = true; return 0; }
		uint16_t v = static_cast<uint16_t>(buf[pos]) |
		             (static_cast<uint16_t>(buf[pos + 1]) << 8);
		pos += 2;
		return v;
	}

	int32_t readI32() {
		if (pos + 4 > length) { error = true; return 0; }
		uint32_t u = static_cast<uint32_t>(buf[pos]) |
		             (static_cast<uint32_t>(buf[pos + 1]) << 8) |
		             (static_cast<uint32_t>(buf[pos + 2]) << 16) |
		             (static_cast<uint32_t>(buf[pos + 3]) << 24);
		pos += 4;
		int32_t v;
		std::memcpy(&v, &u, 4);
		return v;
	}

	uint32_t readU32() {
		if (pos + 4 > length) { error = true; return 0; }
		uint32_t v = static_cast<uint32_t>(buf[pos]) |
		             (static_cast<uint32_t>(buf[pos + 1]) << 8) |
		             (static_cast<uint32_t>(buf[pos + 2]) << 16) |
		             (static_cast<uint32_t>(buf[pos + 3]) << 24);
		pos += 4;
		return v;
	}

	std::string readString() {
		uint16_t len = readU16();
		if (pos + len > length) { error = true; return ""; }
		std::string s(reinterpret_cast<const char*>(buf + pos), len);
		pos += len;
		return s;
	}

	const uint8_t* readBytes(size_t len) {
		if (pos + len > length) { error = true; return nullptr; }
		const uint8_t* ptr = buf + pos;
		pos += len;
		return ptr;
	}

	bool hasData() const { return pos < length; }

private:
	const uint8_t* buf;
	size_t length;
	size_t pos;
	bool error = false;
};

} // namespace Chaos

#endif
