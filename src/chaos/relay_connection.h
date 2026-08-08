/*
 * Chaos Fork: Relay Connection
 * TCP connection to the master relay server for NAT-free multiplayer.
 */

#ifndef EP_CHAOS_RELAY_CONNECTION_H
#define EP_CHAOS_RELAY_CONNECTION_H

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <deque>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SocketHandle;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int SocketHandle;
#define INVALID_SOCK (-1)
#endif

namespace Chaos {

// Relay message types (must match server)
enum RelayMsgType : uint8_t {
	RelayCreateRoom       = 0x01,
	RelayRoomCreated      = 0x02,
	RelayJoinRoom         = 0x03,
	RelayJoinOK           = 0x04,
	RelayJoinFail         = 0x05,
	RelayForward          = 0x06,
	RelayPeerConnected    = 0x07,
	RelayPeerDisconnected = 0x08,
	RelayKeepalive        = 0x09,
	RelayListRooms        = 0x0A,
	RelayRoomList         = 0x0B,
};

struct RoomListEntry {
	std::string code;
	std::string host_name;
	std::string game_name;
	uint8_t mode = 0;
	uint16_t players = 0;
};

class RelayConnection {
public:
	RelayConnection();
	~RelayConnection();

	// Connect to the relay server
	bool Connect(const std::string& host, uint16_t port);

	// Disconnect
	void Disconnect();

	// Process incoming data (call each frame)
	void Update();

	// Send a relay message
	void Send(RelayMsgType type, const uint8_t* data, size_t len);
	void Send(RelayMsgType type, const std::vector<uint8_t>& data);

	// Register as host
	void CreateRoom(uint8_t mode, const std::string& player_name, const std::string& game_name);

	// Join an existing room
	void JoinRoom(const std::string& code, const std::string& player_name);

	// Request room list from server
	void RequestRoomList();

	// Forward game data (host: with target; client: raw data)
	void ForwardToAll(const uint8_t* data, size_t len);
	void ForwardTo(uint16_t peer_id, const uint8_t* data, size_t len);
	void ForwardToHost(const uint8_t* data, size_t len);

	// State
	bool IsConnected() const { return connected; }
	bool HasRoomCode() const { return !room_code.empty(); }
	const std::string& GetRoomCode() const { return room_code; }
	bool HasJoinResult() const { return join_result_ready; }
	bool IsJoinSuccess() const { return join_success; }
	const std::string& GetJoinFailReason() const { return join_fail_reason; }
	void ClearJoinResult() { join_result_ready = false; }
	bool HasRoomList() const { return room_list_ready; }
	const std::vector<RoomListEntry>& GetRoomList() const { return room_list; }
	void ClearRoomList() { room_list_ready = false; room_list.clear(); }

	// Callbacks for received relay messages
	using ForwardCallback = std::function<void(uint16_t source_peer_id, const uint8_t* data, size_t len)>;
	using PeerCallback = std::function<void(uint16_t peer_id, const std::string& name)>;
	using DisconnectPeerCallback = std::function<void(uint16_t peer_id)>;

	void SetForwardCallback(ForwardCallback cb) { forward_callback = std::move(cb); }
	void SetPeerConnectedCallback(PeerCallback cb) { peer_connected_callback = std::move(cb); }
	void SetPeerDisconnectedCallback(DisconnectPeerCallback cb) { peer_disconnected_callback = std::move(cb); }

private:
	bool SetNonBlocking(SocketHandle sock);
	bool ReadAvailableData();
	void ProcessMessages();
	void HandleMessage(uint8_t type, const uint8_t* data, size_t len);
	void SendKeepalive();

	static void WriteU16LE(std::vector<uint8_t>& buf, uint16_t val);
	static void WriteStringLE(std::vector<uint8_t>& buf, const std::string& s);
	static uint16_t ReadU16LE(const uint8_t* data);
	static std::string ReadStringLE(const uint8_t* data, size_t max_len, size_t& bytes_read);

	SocketHandle sock = INVALID_SOCK;
	bool connected = false;

	// Receive buffer
	std::vector<uint8_t> recv_buf;

	// Room state
	std::string room_code;
	bool join_result_ready = false;
	bool join_success = false;
	std::string join_fail_reason;

	// Room list
	bool room_list_ready = false;
	std::vector<RoomListEntry> room_list;

	// Callbacks
	ForwardCallback forward_callback;
	PeerCallback peer_connected_callback;
	DisconnectPeerCallback peer_disconnected_callback;

	// Keepalive
	int keepalive_counter = 0;
	static constexpr int KEEPALIVE_INTERVAL = 600; // frames (~10 seconds at 60fps)
};

} // namespace Chaos

#endif
