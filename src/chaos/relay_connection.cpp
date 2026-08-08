/*
 * Chaos Fork: Relay Connection Implementation
 * TCP connection to the master relay server for NAT-free multiplayer.
 */

#include "chaos/relay_connection.h"
#include "output.h"
#include <chrono>
#include <thread>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

namespace Chaos {

RelayConnection::RelayConnection() {
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

RelayConnection::~RelayConnection() {
	Disconnect();
}

bool RelayConnection::Connect(const std::string& host, uint16_t port) {
	Disconnect();

	struct addrinfo hints{}, *result = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	std::string port_str = std::to_string(port);
	int rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
	if (rc != 0 || !result) {
		Output::Warning("Relay: Failed to resolve host '{}'", host);
		return false;
	}

	sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (sock == INVALID_SOCK) {
		freeaddrinfo(result);
		Output::Warning("Relay: Failed to create socket");
		return false;
	}

	if (::connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) != 0) {
		freeaddrinfo(result);
#ifdef _WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		sock = INVALID_SOCK;
		Output::Warning("Relay: Failed to connect to {}:{}", host, port);
		return false;
	}

	freeaddrinfo(result);

	if (!SetNonBlocking(sock)) {
		Output::Warning("Relay: Failed to set non-blocking mode");
		Disconnect();
		return false;
	}

	connected = true;
	recv_buf.clear();
	room_code.clear();
	join_result_ready = false;
	room_list_ready = false;
	room_list.clear();
	keepalive_counter = 0;

	Output::Debug("Relay: Connected to {}:{}", host, port);
	return true;
}

void RelayConnection::Disconnect() {
	if (sock != INVALID_SOCK) {
#ifdef _WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		sock = INVALID_SOCK;
	}
	connected = false;
	recv_buf.clear();
	room_code.clear();
	join_result_ready = false;
	room_list_ready = false;
	room_list.clear();
}

void RelayConnection::Update() {
	if (!connected) return;

	if (!ReadAvailableData()) {
		Output::Debug("Relay: Connection lost");
		Disconnect();
		return;
	}

	ProcessMessages();

	keepalive_counter++;
	if (keepalive_counter >= KEEPALIVE_INTERVAL) {
		keepalive_counter = 0;
		SendKeepalive();
	}
}

void RelayConnection::Send(RelayMsgType type, const uint8_t* data, size_t len) {
	if (!connected) return;

	uint32_t msg_len = static_cast<uint32_t>(1 + len);
	uint8_t header[4];
	header[0] = (msg_len >> 24) & 0xFF;
	header[1] = (msg_len >> 16) & 0xFF;
	header[2] = (msg_len >> 8) & 0xFF;
	header[3] = msg_len & 0xFF;

	std::vector<uint8_t> buf;
	buf.reserve(4 + msg_len);
	buf.insert(buf.end(), header, header + 4);
	buf.push_back(static_cast<uint8_t>(type));
	if (data && len > 0) {
		buf.insert(buf.end(), data, data + len);
	}

	size_t total_sent = 0;
	while (total_sent < buf.size()) {
		int sent = send(sock, reinterpret_cast<const char*>(buf.data() + total_sent),
			static_cast<int>(buf.size() - total_sent), 0);
		if (sent <= 0) {
#ifdef _WIN32
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
#endif
			Output::Debug("Relay: Send failed");
			Disconnect();
			return;
		}
		total_sent += sent;
	}
}

void RelayConnection::Send(RelayMsgType type, const std::vector<uint8_t>& data) {
	Send(type, data.data(), data.size());
}

void RelayConnection::CreateRoom(uint8_t mode, const std::string& player_name, const std::string& game_name) {
	std::vector<uint8_t> payload;
	payload.push_back(mode);
	WriteStringLE(payload, player_name);
	WriteStringLE(payload, game_name);
	Send(RelayCreateRoom, payload);
}

void RelayConnection::JoinRoom(const std::string& code, const std::string& player_name) {
	std::vector<uint8_t> payload;
	WriteStringLE(payload, code);
	WriteStringLE(payload, player_name);
	Send(RelayJoinRoom, payload);
}

void RelayConnection::RequestRoomList() {
	Send(RelayListRooms, nullptr, 0);
}

void RelayConnection::ForwardToAll(const uint8_t* data, size_t len) {
	// Host broadcast: target peer ID = 0
	std::vector<uint8_t> payload(2 + len);
	payload[0] = 0;
	payload[1] = 0;
	std::memcpy(payload.data() + 2, data, len);
	Send(RelayForward, payload);
}

void RelayConnection::ForwardTo(uint16_t peer_id, const uint8_t* data, size_t len) {
	// Host targeted send: first 2 bytes = target peer ID
	std::vector<uint8_t> payload(2 + len);
	payload[0] = peer_id & 0xFF;
	payload[1] = (peer_id >> 8) & 0xFF;
	std::memcpy(payload.data() + 2, data, len);
	Send(RelayForward, payload);
}

void RelayConnection::ForwardToHost(const uint8_t* data, size_t len) {
	// Client send: raw data, server prepends source peer ID
	Send(RelayForward, data, len);
}

bool RelayConnection::SetNonBlocking(SocketHandle s) {
#ifdef _WIN32
	u_long mode = 1;
	return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0) return false;
	return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool RelayConnection::ReadAvailableData() {
	static constexpr size_t MAX_RECV_BUF = 4 * 1024 * 1024; // 4 MB
	char temp[4096];
	while (true) {
		int received = recv(sock, temp, sizeof(temp), 0);
		if (received > 0) {
			if (recv_buf.size() + received > MAX_RECV_BUF) {
				Output::Warning("Relay: Receive buffer exceeded {} bytes", MAX_RECV_BUF);
				return false;
			}
			recv_buf.insert(recv_buf.end(), temp, temp + received);
		} else if (received == 0) {
			// Connection closed
			return false;
		} else {
#ifdef _WIN32
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				return true; // No more data available right now
			}
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return true;
			}
#endif
			return false; // Real error
		}
	}
}

void RelayConnection::ProcessMessages() {
	while (recv_buf.size() >= 4) {
		uint32_t msg_len = (static_cast<uint32_t>(recv_buf[0]) << 24) |
			(static_cast<uint32_t>(recv_buf[1]) << 16) |
			(static_cast<uint32_t>(recv_buf[2]) << 8) |
			static_cast<uint32_t>(recv_buf[3]);

		if (msg_len == 0 || msg_len > (1 << 20)) {
			Output::Warning("Relay: Invalid message length {}", msg_len);
			Disconnect();
			return;
		}

		if (recv_buf.size() < 4 + msg_len) {
			break; // Incomplete message, wait for more data
		}

		uint8_t type = recv_buf[4];
		const uint8_t* payload = (msg_len > 1) ? &recv_buf[5] : nullptr;
		size_t payload_len = msg_len - 1;

		HandleMessage(type, payload, payload_len);

		recv_buf.erase(recv_buf.begin(), recv_buf.begin() + 4 + msg_len);
	}
}

void RelayConnection::HandleMessage(uint8_t type, const uint8_t* data, size_t len) {
	switch (type) {
	case RelayRoomCreated: {
		if (data && len >= 2) {
			size_t bytes_read = 0;
			room_code = ReadStringLE(data, len, bytes_read);
			Output::Debug("Relay: Room created with code: {}", room_code);
		}
		break;
	}
	case RelayJoinOK: {
		join_result_ready = true;
		join_success = true;
		Output::Debug("Relay: Join succeeded");
		break;
	}
	case RelayJoinFail: {
		join_result_ready = true;
		join_success = false;
		if (data && len >= 2) {
			size_t bytes_read = 0;
			join_fail_reason = ReadStringLE(data, len, bytes_read);
		} else {
			join_fail_reason = "Unknown error";
		}
		Output::Debug("Relay: Join failed: {}", join_fail_reason);
		break;
	}
	case RelayForward: {
		if (!data || len < 2) break;
		uint16_t source_id = ReadU16LE(data);
		if (forward_callback && len > 2) {
			forward_callback(source_id, data + 2, len - 2);
		}
		break;
	}
	case RelayPeerConnected: {
		if (!data || len < 2) break;
		uint16_t peer_id = ReadU16LE(data);
		std::string name;
		if (len > 2) {
			size_t bytes_read = 0;
			name = ReadStringLE(data + 2, len - 2, bytes_read);
		}
		Output::Debug("Relay: Peer connected: id={} name='{}'", peer_id, name);
		if (peer_connected_callback) {
			peer_connected_callback(peer_id, name);
		}
		break;
	}
	case RelayPeerDisconnected: {
		if (!data || len < 2) break;
		uint16_t peer_id = ReadU16LE(data);
		Output::Debug("Relay: Peer disconnected: id={}", peer_id);
		if (peer_disconnected_callback) {
			peer_disconnected_callback(peer_id);
		}
		break;
	}
	case RelayKeepalive:
		// Response to our keepalive
		break;
	case RelayRoomList: {
		room_list.clear();
		if (data && len >= 2) {
			uint16_t count = ReadU16LE(data);
			size_t offset = 2;
			for (uint16_t i = 0; i < count && offset < len; ++i) {
				RoomListEntry entry;
				size_t br = 0;
				entry.code = ReadStringLE(data + offset, len - offset, br);
				offset += br;
				entry.host_name = ReadStringLE(data + offset, len - offset, br);
				offset += br;
				entry.game_name = ReadStringLE(data + offset, len - offset, br);
				offset += br;
				if (offset < len) {
					entry.mode = data[offset++];
				}
				if (offset + 1 < len) {
					entry.players = ReadU16LE(data + offset);
					offset += 2;
				}
				room_list.push_back(entry);
			}
		}
		room_list_ready = true;
		Output::Debug("Relay: Received room list with {} rooms", room_list.size());
		break;
	}
	default:
		Output::Debug("Relay: Unknown message type 0x{:02x}", type);
		break;
	}
}

void RelayConnection::SendKeepalive() {
	Send(RelayKeepalive, nullptr, 0);
}

void RelayConnection::WriteU16LE(std::vector<uint8_t>& buf, uint16_t val) {
	buf.push_back(val & 0xFF);
	buf.push_back((val >> 8) & 0xFF);
}

void RelayConnection::WriteStringLE(std::vector<uint8_t>& buf, const std::string& s) {
	WriteU16LE(buf, static_cast<uint16_t>(s.size()));
	buf.insert(buf.end(), s.begin(), s.end());
}

uint16_t RelayConnection::ReadU16LE(const uint8_t* data) {
	return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

std::string RelayConnection::ReadStringLE(const uint8_t* data, size_t max_len, size_t& bytes_read) {
	bytes_read = 0;
	if (max_len < 2) return "";
	uint16_t slen = ReadU16LE(data);
	bytes_read = 2;
	if (2 + slen > max_len) return "";
	bytes_read = 2 + slen;
	return std::string(reinterpret_cast<const char*>(data + 2), slen);
}

} // namespace Chaos
