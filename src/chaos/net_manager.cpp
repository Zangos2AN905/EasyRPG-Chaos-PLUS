/*
 * Chaos Fork: ENet Network Manager Implementation
 */

#include "chaos/net_manager.h"
#include "output.h"
#include "player.h"
#include "filefinder.h"

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <enet/enet.h>

namespace Chaos {

static constexpr uint16_t DEFAULT_PORT = 6510;
static constexpr int MAX_CHANNELS = 2;
static constexpr int CHANNEL_RELIABLE = 0;
static constexpr int CHANNEL_UNRELIABLE = 1;
static constexpr uint16_t MAX_PEER_COUNT = 256;
static constexpr uint16_t MAX_PARTY_COUNT = 256;
static constexpr uint16_t MAX_SWITCH_VAR_COUNT = 65535;

NetManager& NetManager::Instance() {
	static NetManager instance;
	return instance;
}

NetManager::~NetManager() {
	Shutdown();
}

bool NetManager::Initialize() {
	if (initialized) return true;

	if (enet_initialize() != 0) {
		Output::Warning("Multiplayer: Failed to initialize ENet");
		return false;
	}

	initialized = true;
	Output::Debug("Multiplayer: ENet initialized");
	return true;
}

void NetManager::Shutdown() {
	Disconnect();

	if (initialized) {
		enet_deinitialize();
		initialized = false;
		Output::Debug("Multiplayer: ENet shutdown");
	}
}

bool NetManager::HostGame(uint16_t port, MultiplayerMode mode, const std::string& player_name) {
	if (!IsSupportedMultiplayerMode(mode)) {
		Output::Warning("Multiplayer: Unsupported mode {}", static_cast<int>(mode));
		return false;
	}
	if (!initialized && !Initialize()) return false;
	Disconnect();

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;

	int max_clients = 32;
	auto& props = GetModeProperties(mode);
	if (props.max_players > 0) {
		max_clients = props.max_players - 1; // -1 for host
	}

	enet_host = enet_host_create(&address, max_clients, MAX_CHANNELS, 0, 0);
	if (!enet_host) {
		Output::Warning("Multiplayer: Failed to create ENet host on port {}", port);
		return false;
	}

	is_host = true;
	is_client = false;
	current_mode = mode;
	local_player_name = player_name;
	local_peer_id = AllocatePeerId();  // Host is peer 1
	peers.clear();

	Output::Debug("Multiplayer: Hosting game on port {} (mode: {})", port, props.name);
	return true;
}

bool NetManager::JoinGame(const std::string& host, uint16_t port, const std::string& player_name) {
	if (!initialized && !Initialize()) return false;
	Disconnect();

	enet_host = enet_host_create(nullptr, 1, MAX_CHANNELS, 0, 0);
	if (!enet_host) {
		Output::Warning("Multiplayer: Failed to create ENet client");
		return false;
	}

	ENetAddress address;
	enet_address_set_host(&address, host.c_str());
	address.port = port;

	server_peer = enet_host_connect(enet_host, &address, MAX_CHANNELS, 0);
	if (!server_peer) {
		Output::Warning("Multiplayer: Failed to initiate connection to {}:{}", host, port);
		enet_host_destroy(enet_host);
		enet_host = nullptr;
		return false;
	}

	is_host = false;
	is_client = true;
	local_player_name = player_name;
	peers.clear();

	Output::Debug("Multiplayer: Connecting to {}:{}", host, port);
	return true;
}

bool NetManager::HostViaRelay(const std::string& relay_host, uint16_t relay_port,
	MultiplayerMode mode, const std::string& player_name) {
	if (!IsSupportedMultiplayerMode(mode)) {
		Output::Warning("Multiplayer: Unsupported mode {}", static_cast<int>(mode));
		return false;
	}
	if (!initialized && !Initialize()) return false;
	Disconnect();

	relay = std::make_unique<RelayConnection>();
	if (!relay->Connect(relay_host, relay_port)) {
		Output::Warning("Multiplayer: Failed to connect to relay server");
		relay.reset();
		return false;
	}

	is_host = true;
	is_client = false;
	is_relay = true;
	current_mode = mode;
	local_player_name = player_name;
	local_peer_id = AllocatePeerId();  // Host is peer 1
	peers.clear();

	// Set up relay callbacks
	relay->SetPeerConnectedCallback([this](uint16_t peer_id, const std::string& name) {
		HandleRelayPeerConnected(peer_id, name);
	});
	relay->SetPeerDisconnectedCallback([this](uint16_t peer_id) {
		HandleRelayPeerDisconnected(peer_id);
	});
	relay->SetForwardCallback([this](uint16_t source_id, const uint8_t* data, size_t len) {
		HandleRelayForward(source_id, data, len);
	});

	relay->CreateRoom(static_cast<uint8_t>(mode), player_name,
		Player::game_title.empty() ? "Unknown Game" : Player::game_title);

	auto& props = GetModeProperties(mode);
	Output::Debug("Multiplayer: Hosting via relay (mode: {})", props.name);
	return true;
}

bool NetManager::JoinViaRelay(const std::string& relay_host, uint16_t relay_port,
	const std::string& room_code, const std::string& player_name) {
	if (!initialized && !Initialize()) return false;
	Disconnect();

	relay = std::make_unique<RelayConnection>();
	if (!relay->Connect(relay_host, relay_port)) {
		Output::Warning("Multiplayer: Failed to connect to relay server");
		relay.reset();
		return false;
	}

	is_host = false;
	is_client = true;
	is_relay = true;
	local_player_name = player_name;
	peers.clear();

	// Set up relay callbacks
	relay->SetForwardCallback([this](uint16_t source_id, const uint8_t* data, size_t len) {
		HandleRelayForward(source_id, data, len);
	});
	relay->SetPeerDisconnectedCallback([this](uint16_t peer_id) {
		// Host disconnected
		if (peer_id == 1) {
			Output::Debug("Multiplayer: Host disconnected from relay");
			is_client = false;
			host_disconnected = true;
			peers.clear();
			if (disconnect_callback) {
				disconnect_callback(0);
			}
		}
	});

	relay->JoinRoom(room_code, player_name);

	Output::Debug("Multiplayer: Joining relay room '{}'", room_code);
	return true;
}

void NetManager::Update() {
	if (is_relay && relay) {
		relay->Update();

		// Check if relay lost connection
		if (!relay->IsConnected()) {
			Output::Debug("Multiplayer: Relay connection lost");
			if (is_client) {
				host_disconnected = true;
			}
			is_host = false;
			is_client = false;
			game_started = false;
			host_map_ready = false;
			host_switches_ready = false;
			host_variables_ready = false;
			local_peer_id = 0;
			peers.clear();
			if (disconnect_callback) {
				disconnect_callback(0);
			}
		}

		// For client: check join result
		if (is_client && relay->HasJoinResult()) {
			if (relay->IsJoinSuccess()) {
				Output::Debug("Multiplayer: Relay join succeeded, sending Join packet");
				// Send game-level Join via relay
				PacketWriter join(PacketType::Join);
				join.write(local_player_name);
				relay->ForwardToHost(join.data(), join.size());
			} else {
				Output::Warning("Multiplayer: Relay join failed: {}", relay->GetJoinFailReason());
				is_client = false;
			}
			relay->ClearJoinResult();
		}
		UpdateFileTransfers();
		return;
	}

	if (!enet_host) return;

	ENetEvent event;
	while (enet_host_service(enet_host, &event, 0) > 0) {
		ProcessEvent(event);
	}

	// Ensure all queued outgoing packets are flushed
	enet_host_flush(enet_host);
	UpdateFileTransfers();
}

void NetManager::ProcessEvent(ENetEvent& event) {
	if (is_host) {
		HandleServerEvent(event);
	} else {
		HandleClientEvent(event);
	}
}

void NetManager::HandleServerEvent(ENetEvent& event) {
	switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			Output::Debug("Multiplayer: Client connected from {}:{}",
				event.peer->address.host, event.peer->address.port);
			// Wait for Join packet with player name
			break;
		}
		case ENET_EVENT_TYPE_RECEIVE: {
			if (event.packet->dataLength < 1) {
				enet_packet_destroy(event.packet);
				break;
			}

			// Check if this is a Join request from a not-yet-registered peer
			auto* pi = FindPeerByENet(event.peer);
			PacketReader reader(event.packet->data, event.packet->dataLength);
			PacketType ptype = reader.readType();

			if (!pi && ptype == PacketType::Join) {
				std::string name = reader.readString();

				// Check max players
				auto& props = GetModeProperties(current_mode);
				if (props.max_players > 0 &&
					static_cast<int>(peers.size()) + 1 >= props.max_players) {
					PacketWriter reject(PacketType::JoinReject);
					reject.write(std::string("Server is full"));
					ENetPacket* pkt = enet_packet_create(reject.data(), reject.size(),
						ENET_PACKET_FLAG_RELIABLE);
					enet_peer_send(event.peer, CHANNEL_RELIABLE, pkt);
					enet_packet_destroy(event.packet);
					break;
				}

				// Accept the player
				uint16_t new_id = AllocatePeerId();
				PeerInfo info;
				info.peer_id = new_id;
				info.player_name = name;
				info.peer = event.peer;
				peers.push_back(info);

				// Send accept with assigned ID and mode
				PacketWriter accept(PacketType::JoinAccept);
				accept.write(new_id);
				accept.write(static_cast<uint8_t>(current_mode));
				accept.write(local_player_name);  // Host name
				accept.write(Player::game_title.empty() ? std::string("Unknown Game") : Player::game_title);  // Game name
				// Send existing peer list
				accept.write(static_cast<uint16_t>(peers.size() - 1));  // Exclude the new peer
				for (auto& p : peers) {
					if (p.peer_id != new_id) {
						accept.write(p.peer_id);
						accept.write(p.player_name);
					}
				}
				ENetPacket* apkt = enet_packet_create(accept.data(), accept.size(),
					ENET_PACKET_FLAG_RELIABLE);
				enet_peer_send(event.peer, CHANNEL_RELIABLE, apkt);

				// Notify all other peers
				PacketWriter joined(PacketType::PlayerJoined);
				joined.write(new_id);
				joined.write(name);
				for (auto& p : peers) {
					if (p.peer_id != new_id && p.peer) {
						ENetPacket* jpkt = enet_packet_create(joined.data(), joined.size(),
							ENET_PACKET_FLAG_RELIABLE);
						enet_peer_send(p.peer, CHANNEL_RELIABLE, jpkt);
					}
				}

				Output::Debug("Multiplayer: Player '{}' joined (id={})", name, new_id);

				if (connect_callback) {
					connect_callback(new_id);
				}
			} else if (pi) {
				// Handle game file transfer requests on the host
				if (ptype == PacketType::GameFileRequest) {
					HandleGameFileRequest(pi->peer_id);
					enet_packet_destroy(event.packet);
					break;
				}

				// Forward packet to callback
				if (packet_callback) {
					packet_callback(pi->peer_id, event.packet->data, event.packet->dataLength);
				}

				// Also relay to other clients (except sender)
				if (ptype == PacketType::PlayerPosition || ptype == PacketType::ChatMessage) {
					for (auto& p : peers) {
						if (p.peer != event.peer && p.peer) {
							ENetPacket* rpkt = enet_packet_create(event.packet->data,
								event.packet->dataLength,
								ptype == PacketType::PlayerPosition ? 0 : ENET_PACKET_FLAG_RELIABLE);
							enet_peer_send(p.peer, ptype == PacketType::PlayerPosition ?
								CHANNEL_UNRELIABLE : CHANNEL_RELIABLE, rpkt);
						}
					}
				}
			}

			enet_packet_destroy(event.packet);
			break;
		}
		case ENET_EVENT_TYPE_DISCONNECT: {
			auto* pi = FindPeerByENet(event.peer);
			if (pi) {
				uint16_t pid = pi->peer_id;
				std::string name = pi->player_name;

				// Notify others
				PacketWriter left(PacketType::PlayerLeft);
				left.write(pid);
				for (auto& p : peers) {
					if (p.peer_id != pid && p.peer) {
						ENetPacket* lpkt = enet_packet_create(left.data(), left.size(),
							ENET_PACKET_FLAG_RELIABLE);
						enet_peer_send(p.peer, CHANNEL_RELIABLE, lpkt);
					}
				}

				RemovePeer(event.peer);
				Output::Debug("Multiplayer: Player '{}' disconnected (id={})", name, pid);

				if (disconnect_callback) {
					disconnect_callback(pid);
				}
			}
			break;
		}
		default:
			break;
	}
}

void NetManager::HandleClientEvent(ENetEvent& event) {
	switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			Output::Debug("Multiplayer: Connected to server");
			// Send Join request
			PacketWriter join(PacketType::Join);
			join.write(local_player_name);
			ENetPacket* pkt = enet_packet_create(join.data(), join.size(),
				ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(server_peer, CHANNEL_RELIABLE, pkt);
			break;
		}
		case ENET_EVENT_TYPE_RECEIVE: {
			if (event.packet->dataLength < 1) {
				enet_packet_destroy(event.packet);
				break;
			}

			PacketReader reader(event.packet->data, event.packet->dataLength);
			PacketType ptype = reader.readType();

			if (ptype == PacketType::JoinAccept) {
				local_peer_id = reader.readU16();
				current_mode = static_cast<MultiplayerMode>(reader.readU8());
				if (!reader.ok() || !IsSupportedMultiplayerMode(current_mode)) {
					Output::Warning("Multiplayer: Rejecting invalid JoinAccept mode");
					enet_packet_destroy(event.packet);
					Disconnect();
					break;
				}
				std::string host_name = reader.readString();
				host_game_name = reader.readString();

				// Add host as peer 1
				PeerInfo host_info;
				host_info.peer_id = 1;
				host_info.player_name = host_name;
				host_info.peer = nullptr;
				peers.push_back(host_info);

			// Read existing peers
			uint16_t count = reader.readU16();
			if (count > MAX_PEER_COUNT) {
				Output::Warning("Multiplayer: JoinAccept peer count {} exceeds limit", count);
				Disconnect();
				enet_packet_destroy(event.packet);
				break;
			}
			for (uint16_t i = 0; i < count; ++i) {
				PeerInfo pi;
				pi.peer_id = reader.readU16();
				pi.player_name = reader.readString();
				pi.peer = nullptr;
				peers.push_back(pi);
			}

				Output::Debug("Multiplayer: Joined as '{}' (id={})", local_player_name, local_peer_id);

				if (connect_callback) {
					connect_callback(local_peer_id);
				}
			} else if (ptype == PacketType::JoinReject) {
				std::string reason = reader.readString();
				Output::Warning("Multiplayer: Join rejected: {}", reason);
				Disconnect();
			} else if (ptype == PacketType::PlayerJoined) {
				PeerInfo pi;
				pi.peer_id = reader.readU16();
				pi.player_name = reader.readString();
				pi.peer = nullptr;
				peers.push_back(pi);
				Output::Debug("Multiplayer: Player '{}' joined (id={})", pi.player_name, pi.peer_id);

				if (connect_callback) {
					connect_callback(pi.peer_id);
				}
			} else if (ptype == PacketType::PlayerLeft) {
				uint16_t pid = reader.readU16();
				auto* pi = FindPeer(pid);
				if (pi) {
					Output::Debug("Multiplayer: Player '{}' left (id={})", pi->player_name, pid);
				}
				peers.erase(std::remove_if(peers.begin(), peers.end(),
					[pid](const PeerInfo& p) { return p.peer_id == pid; }),
					peers.end());

				if (disconnect_callback) {
					disconnect_callback(pid);
				}
			} else if (ptype == PacketType::GameStart) {
				Output::Debug("Multiplayer: Received GameStart from server");
				game_started = true;
			} else if (ptype == PacketType::HostMapReady) {
				host_map_id = reader.readI32();
				host_map_x = reader.readI32();
				host_map_y = reader.readI32();
				uint16_t num_party = reader.readU16();
				if (!reader.ok() || num_party > MAX_PARTY_COUNT) {
					Output::Warning("Multiplayer: Invalid HostMapReady party count {}", num_party);
					enet_packet_destroy(event.packet);
					break;
				}
				host_party.clear();
				for (uint16_t i = 0; i < num_party; ++i) {
					HostPartyMember m;
					m.actor_id = reader.readU16();
					m.level = reader.readI32();
					m.hp = reader.readI32();
					m.sp = reader.readI32();
					host_party.push_back(m);
				}
				host_map_ready = true;
				Output::Debug("Multiplayer: Host entered map {} at ({}, {}), {} party members",
					host_map_id, host_map_x, host_map_y, num_party);
			} else if (ptype == PacketType::SwitchBulkSync) {
				uint16_t count = reader.readU16();
				if (!reader.ok() || count > MAX_SWITCH_VAR_COUNT) {
					Output::Warning("Multiplayer: Invalid SwitchBulkSync count {}", count);
					enet_packet_destroy(event.packet);
					break;
				}
				host_switches.clear();
				host_switches.resize(count, false);
				for (uint16_t i = 0; i < count; ++i) {
					host_switches[i] = reader.readU8() != 0;
				}
				host_switches_ready = true;
				Output::Debug("Multiplayer: Received {} switches from host", count);
			} else if (ptype == PacketType::VariableBulkSync) {
				uint16_t count = reader.readU16();
				if (!reader.ok() || count > MAX_SWITCH_VAR_COUNT) {
					Output::Warning("Multiplayer: Invalid VariableBulkSync count {}", count);
					enet_packet_destroy(event.packet);
					break;
				}
				host_variables.clear();
				host_variables.resize(count, 0);
				for (uint16_t i = 0; i < count; ++i) {
					host_variables[i] = reader.readI32();
				}
				host_variables_ready = true;
				Output::Debug("Multiplayer: Received {} variables from host", count);
			} else if (ptype == PacketType::GameFileInfo) {
				HandleGameFileInfo(reader);
			} else if (ptype == PacketType::GameFileData) {
				HandleGameFileData(reader);
			} else if (ptype == PacketType::GameFileDone) {
				HandleGameFileDone();
			} else {
				// Forward to callback
				if (packet_callback) {
					// For relayed packets, extract the sender peer_id from the packet
					// Position packets include peer_id; others come from server
					packet_callback(0, event.packet->data, event.packet->dataLength);
				}
			}

			enet_packet_destroy(event.packet);
			break;
		}
		case ENET_EVENT_TYPE_DISCONNECT: {
			Output::Debug("Multiplayer: Disconnected from server");
			server_peer = nullptr;
			is_client = false;
			host_disconnected = true;
			peers.clear();

			if (disconnect_callback) {
				disconnect_callback(0);
			}
			break;
		}
		default:
			break;
	}
}

void NetManager::Broadcast(const PacketWriter& packet, bool reliable) {
	if (is_relay && relay && is_host) {
		relay->ForwardToAll(packet.data(), packet.size());
		return;
	}

	if (!enet_host) return;

	if (is_host) {
		for (auto& p : peers) {
			if (p.peer) {
				ENetPacket* pkt = enet_packet_create(packet.data(), packet.size(),
					reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
				enet_peer_send(p.peer, reliable ? CHANNEL_RELIABLE : CHANNEL_UNRELIABLE, pkt);
			}
		}
	}
}

void NetManager::SendTo(uint16_t peer_id, const PacketWriter& packet, bool reliable) {
	if (is_relay && relay && is_host) {
		relay->ForwardTo(peer_id, packet.data(), packet.size());
		return;
	}

	auto* pi = FindPeer(peer_id);
	if (pi && pi->peer) {
		ENetPacket* pkt = enet_packet_create(packet.data(), packet.size(),
			reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
		enet_peer_send(pi->peer, reliable ? CHANNEL_RELIABLE : CHANNEL_UNRELIABLE, pkt);
	}
}

void NetManager::SendToServer(const PacketWriter& packet, bool reliable) {
	if (is_relay && relay && is_client) {
		relay->ForwardToHost(packet.data(), packet.size());
		return;
	}

	if (!is_client || !server_peer) return;

	ENetPacket* pkt = enet_packet_create(packet.data(), packet.size(),
		reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
	enet_peer_send(server_peer, reliable ? CHANNEL_RELIABLE : CHANNEL_UNRELIABLE, pkt);
}

void NetManager::Disconnect() {
	// Clean up relay connection
	if (relay) {
		relay->Disconnect();
		relay.reset();
	}

	if (is_client && server_peer) {
		enet_peer_disconnect(server_peer, 0);
		// Flush
		ENetEvent event;
		while (enet_host_service(enet_host, &event, 1000) > 0) {
			if (event.type == ENET_EVENT_TYPE_RECEIVE) {
				enet_packet_destroy(event.packet);
			} else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
				break;
			}
		}
		server_peer = nullptr;
	}

	if (is_host) {
		for (auto& p : peers) {
			if (p.peer) {
				enet_peer_disconnect(p.peer, 0);
			}
		}
	}

	if (enet_host) {
		enet_host_destroy(enet_host);
		enet_host = nullptr;
	}

	is_host = false;
	is_client = false;
	is_relay = false;
	game_started = false;
	host_disconnected = false;
	host_map_ready = false;
	host_switches_ready = false;
	host_variables_ready = false;
	host_map_id = 0;
	host_map_x = 0;
	host_map_y = 0;
	host_party.clear();
	host_switches.clear();
	host_variables.clear();
	local_peer_id = 0;
	next_peer_id = 1;
	peers.clear();
	packet_callback = nullptr;
	connect_callback = nullptr;
	disconnect_callback = nullptr;
}

uint16_t NetManager::AllocatePeerId() {
	uint16_t id = next_peer_id++;
	if (id == 0) {
		id = next_peer_id++;
	}
	if (next_peer_id == 0) {
		next_peer_id = 1;
	}
	return id;
}

PeerInfo* NetManager::FindPeer(uint16_t peer_id) {
	for (auto& p : peers) {
		if (p.peer_id == peer_id) return &p;
	}
	return nullptr;
}

PeerInfo* NetManager::FindPeerByENet(ENetPeer* peer) {
	for (auto& p : peers) {
		if (p.peer == peer) return &p;
	}
	return nullptr;
}

void NetManager::RemovePeer(ENetPeer* peer) {
	peers.erase(std::remove_if(peers.begin(), peers.end(),
		[peer](const PeerInfo& p) { return p.peer == peer; }),
		peers.end());
}

bool NetManager::ConnectToRelayForBrowse(const std::string& relay_host, uint16_t relay_port) {
	DisconnectBrowse();
	browse_relay = std::make_unique<RelayConnection>();
	if (!browse_relay->Connect(relay_host, relay_port)) {
		Output::Warning("Multiplayer: Failed to connect to relay for browsing");
		browse_relay.reset();
		return false;
	}
	return true;
}

void NetManager::RequestRoomList() {
	if (browse_relay && browse_relay->IsConnected()) {
		browse_relay->RequestRoomList();
	}
}

void NetManager::UpdateBrowse() {
	if (browse_relay) {
		browse_relay->Update();
	}
}

void NetManager::DisconnectBrowse() {
	if (browse_relay) {
		browse_relay->Disconnect();
		browse_relay.reset();
	}
}

void NetManager::HandleRelayForward(uint16_t source_id, const uint8_t* data, size_t len) {
	if (len < 1) return;

	if (is_host) {
		// Host receives data from a relay client
		auto* pi = FindPeer(source_id);

		PacketReader reader(data, len);
		PacketType ptype = reader.readType();

		if (!pi && ptype == PacketType::Join) {
			// New player joining via relay
			std::string name = reader.readString();

			auto& props = GetModeProperties(current_mode);
			if (props.max_players > 0 &&
				static_cast<int>(peers.size()) + 1 >= props.max_players) {
				PacketWriter reject(PacketType::JoinReject);
				reject.write(std::string("Server is full"));
				relay->ForwardTo(source_id, reject.data(), reject.size());
				return;
			}

			// Accept the player using the relay-assigned peer ID
			PeerInfo info;
			info.peer_id = source_id;
			info.player_name = name;
			info.peer = nullptr;  // No ENet peer for relay connections
			peers.push_back(info);

			// Update next_peer_id to stay above relay-assigned IDs
			if (source_id >= next_peer_id) {
				next_peer_id = source_id + 1;
			}

			// Send JoinAccept
			PacketWriter accept(PacketType::JoinAccept);
			accept.write(source_id);
			accept.write(static_cast<uint8_t>(current_mode));
			accept.write(local_player_name);
			accept.write(Player::game_title.empty() ? std::string("Unknown Game") : Player::game_title);
			accept.write(static_cast<uint16_t>(peers.size() - 1));
			for (auto& p : peers) {
				if (p.peer_id != source_id) {
					accept.write(p.peer_id);
					accept.write(p.player_name);
				}
			}
			relay->ForwardTo(source_id, accept.data(), accept.size());

			// Notify other relay peers
			PacketWriter joined(PacketType::PlayerJoined);
			joined.write(source_id);
			joined.write(name);
			for (auto& p : peers) {
				if (p.peer_id != source_id) {
					relay->ForwardTo(p.peer_id, joined.data(), joined.size());
				}
			}

			Output::Debug("Multiplayer: Relay player '{}' joined (id={})", name, source_id);
			if (connect_callback) {
				connect_callback(source_id);
			}
		} else if (pi) {
			// Handle game file transfer requests on the relay host
			if (ptype == PacketType::GameFileRequest) {
				HandleGameFileRequest(pi->peer_id);
				return;
			}

			// Known peer, forward to callback
			if (packet_callback) {
				packet_callback(pi->peer_id, data, len);
			}

			// Relay to other clients if needed
			if (ptype == PacketType::PlayerPosition || ptype == PacketType::ChatMessage) {
				for (auto& p : peers) {
					if (p.peer_id != source_id) {
						relay->ForwardTo(p.peer_id, data, len);
					}
				}
			}
		}
	} else {
		// Client receives forwarded data from host
		PacketReader reader(data, len);
		PacketType ptype = reader.readType();

		if (ptype == PacketType::JoinAccept) {
			local_peer_id = reader.readU16();
			current_mode = static_cast<MultiplayerMode>(reader.readU8());
			if (!reader.ok() || !IsSupportedMultiplayerMode(current_mode)) {
				Output::Warning("Multiplayer: Rejecting invalid relay JoinAccept mode");
				Disconnect();
				return;
			}
			std::string host_name = reader.readString();
			host_game_name = reader.readString();

			PeerInfo host_info;
			host_info.peer_id = 1;
			host_info.player_name = host_name;
			host_info.peer = nullptr;
			peers.push_back(host_info);

		uint16_t count = reader.readU16();
			if (count > MAX_PEER_COUNT) {
				Output::Warning("Multiplayer: Relay JoinAccept peer count {} exceeds limit", count);
				Disconnect();
				return;
			}
			for (uint16_t i = 0; i < count; ++i) {
				PeerInfo pi;
				pi.peer_id = reader.readU16();
				pi.player_name = reader.readString();
				pi.peer = nullptr;
				peers.push_back(pi);
			}
			Output::Debug("Multiplayer: Relay joined as '{}' (id={})", local_player_name, local_peer_id);
			if (connect_callback) {
				connect_callback(local_peer_id);
			}
		} else if (ptype == PacketType::JoinReject) {
			std::string reason = reader.readString();
			Output::Warning("Multiplayer: Relay join rejected: {}", reason);
			Disconnect();
		} else if (ptype == PacketType::PlayerJoined) {
			PeerInfo pi;
			pi.peer_id = reader.readU16();
			pi.player_name = reader.readString();
			pi.peer = nullptr;
			peers.push_back(pi);
			Output::Debug("Multiplayer: Relay player '{}' joined (id={})", pi.player_name, pi.peer_id);
			if (connect_callback) {
				connect_callback(pi.peer_id);
			}
		} else if (ptype == PacketType::PlayerLeft) {
			uint16_t pid = reader.readU16();
			peers.erase(std::remove_if(peers.begin(), peers.end(),
				[pid](const PeerInfo& p) { return p.peer_id == pid; }),
				peers.end());
			if (disconnect_callback) {
				disconnect_callback(pid);
			}
		} else if (ptype == PacketType::GameStart) {
			Output::Debug("Multiplayer: Received GameStart via relay");
			game_started = true;
		} else if (ptype == PacketType::HostMapReady) {
			host_map_id = reader.readI32();
			host_map_x = reader.readI32();
			host_map_y = reader.readI32();
			uint16_t num_party = reader.readU16();
			if (!reader.ok() || num_party > MAX_PARTY_COUNT) {
				Output::Warning("Multiplayer: Invalid relay HostMapReady party count {}", num_party);
				return;
			}
		host_party.clear();
		for (uint16_t i = 0; i < num_party; ++i) {
			HostPartyMember m;
			m.actor_id = reader.readU16();
			m.level = reader.readI32();
			m.hp = reader.readI32();
			m.sp = reader.readI32();
			host_party.push_back(m);
		}
		host_map_ready = true;
		Output::Debug("Multiplayer: Relay host entered map {} at ({}, {})",
			host_map_id, host_map_x, host_map_y);
	} else if (ptype == PacketType::SwitchBulkSync) {
		uint16_t count = reader.readU16();
		if (!reader.ok() || count > MAX_SWITCH_VAR_COUNT) {
			Output::Warning("Multiplayer: Invalid relay SwitchBulkSync count {}", count);
			return;
		}
			host_switches.clear();
			host_switches.resize(count, false);
			for (uint16_t i = 0; i < count; ++i) {
				host_switches[i] = reader.readU8() != 0;
			}
			host_switches_ready = true;
	} else if (ptype == PacketType::VariableBulkSync) {
		uint16_t count = reader.readU16();
		if (!reader.ok() || count > MAX_SWITCH_VAR_COUNT) {
			Output::Warning("Multiplayer: Invalid relay VariableBulkSync count {}", count);
			return;
		}
		host_variables.clear();
			host_variables.resize(count, 0);
			for (uint16_t i = 0; i < count; ++i) {
				host_variables[i] = reader.readI32();
			}
			host_variables_ready = true;
		} else if (ptype == PacketType::GameFileInfo) {
			HandleGameFileInfo(reader);
		} else if (ptype == PacketType::GameFileData) {
			HandleGameFileData(reader);
		} else if (ptype == PacketType::GameFileDone) {
			HandleGameFileDone();
		} else {
			if (packet_callback) {
				packet_callback(0, data, len);
			}
		}
	}
}

void NetManager::HandleRelayPeerConnected(uint16_t peer_id, const std::string& name) {
	// Host only: a new peer connected to our relay room
	// We'll wait for their Join game packet via Forward
	Output::Debug("Multiplayer: Relay peer connected (transport id={})", peer_id);
}

void NetManager::HandleRelayPeerDisconnected(uint16_t peer_id) {
	if (!is_host) return;

	auto* pi = FindPeer(peer_id);
	if (pi) {
		std::string name = pi->player_name;

		// Notify other peers
		PacketWriter left(PacketType::PlayerLeft);
		left.write(peer_id);
		for (auto& p : peers) {
			if (p.peer_id != peer_id) {
				relay->ForwardTo(p.peer_id, left.data(), left.size());
			}
		}

		peers.erase(std::remove_if(peers.begin(), peers.end(),
			[peer_id](const PeerInfo& p) { return p.peer_id == peer_id; }),
			peers.end());

		Output::Debug("Multiplayer: Relay player '{}' disconnected (id={})", name, peer_id);
		if (disconnect_callback) {
			disconnect_callback(peer_id);
		}
	}
}

// ============================================================
// Game File Transfer Methods
// ============================================================

void NetManager::RequestGameFiles() {
	if (!is_client) return;

	// Determine destination path for the game
	std::string games_dir;
#ifdef _WIN32
	wchar_t exe_path[MAX_PATH];
	GetModuleFileNameW(NULL, exe_path, MAX_PATH);
	std::wstring wpath(exe_path);
	size_t pos = wpath.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		wpath = wpath.substr(0, pos);
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
	games_dir.resize(len - 1);
	WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &games_dir[0], len, NULL, NULL);
#else
	games_dir = ".";
#endif

	// Create destination using the host's game name
	std::string safe_name = host_game_name;
	// Sanitize folder name
	for (char& c : safe_name) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
			c = '_';
		}
	}
	if (safe_name.empty()) safe_name = "DownloadedGame";

	std::string dest = games_dir + "/" + safe_name;

	client_transfer = std::make_unique<GameFileTransferClient>();
	client_transfer->SetDestination(dest);

	// Send request to host
	PacketWriter pw(PacketType::GameFileRequest);
	SendToServer(pw, true);

	Output::Debug("GameFileTransfer: Requesting game files from host, dest={}", dest);
}

void NetManager::HandleGameFileRequest(uint16_t peer_id) {
	if (!is_host) return;

	// Get the current game's filesystem path
	auto game_fs = FileFinder::Game();
	std::string game_path = game_fs.GetFullPath();

	if (game_path.empty()) {
		Output::Warning("GameFileTransfer: Cannot serve files - no game path");
		return;
	}

	Output::Debug("GameFileTransfer: Peer {} requested game files from path: {}", peer_id, game_path);

	auto transfer = std::make_unique<GameFileTransferHost>();
	transfer->Start(game_path);
	host_transfers[peer_id] = std::move(transfer);
}

void NetManager::HandleGameFileInfo(PacketReader& reader) {
	uint16_t file_count = reader.readU16();
	int32_t total_bytes = reader.readI32();

	if (client_transfer) {
		client_transfer->OnFileInfo(file_count, total_bytes);
	}
}

void NetManager::HandleGameFileData(PacketReader& reader) {
	std::string path = reader.readString();
	uint8_t is_last = reader.readU8();
	uint16_t data_size = reader.readU16();
	const uint8_t* data = reader.readBytes(data_size);

	if (client_transfer && data) {
		client_transfer->OnFileData(path, data, data_size, is_last != 0);
	}
}

void NetManager::HandleGameFileDone() {
	if (client_transfer) {
		client_transfer->OnTransferDone();
	}
}

void NetManager::UpdateFileTransfers() {
	// Host: send queued chunks for each active transfer
	if (is_host) {
		std::vector<uint16_t> done_peers;
		for (auto& [peer_id, transfer] : host_transfers) {
			if (!transfer->SendChunks(*this, peer_id, 4)) {
				done_peers.push_back(peer_id);
			}
		}
		for (auto pid : done_peers) {
			host_transfers.erase(pid);
		}
	}

	// Client: update the writer thread
	if (client_transfer) {
		client_transfer->Update();
	}
}

} // namespace Chaos
