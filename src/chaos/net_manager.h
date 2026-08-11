/*
 * Chaos Fork: ENet Network Manager
 */

#ifndef EP_CHAOS_NET_MANAGER_H
#define EP_CHAOS_NET_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include "chaos/multiplayer_mode.h"
#include "chaos/net_packet.h"
#include "chaos/relay_connection.h"
#include "chaos/game_file_transfer.h"

// Forward declare ENet types to avoid exposing header
typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;
typedef struct _ENetEvent ENetEvent;

namespace Chaos {

struct PeerInfo {
	uint16_t peer_id = 0;
	std::string player_name;
	std::string discord_user_id;
	std::string discord_avatar_hash;
	ENetPeer* peer = nullptr;
	bool is_god = false;  // For God Mode
};

struct HostPartyMember {
	uint16_t actor_id = 0;
	int32_t level = 1;
	int32_t hp = -1;
	int32_t sp = -1;
};

class NetManager {
public:
	using PacketCallback = std::function<void(uint16_t peer_id, const uint8_t* data, size_t len)>;

	static NetManager& Instance();

	// Lifecycle
	bool Initialize();
	void Shutdown();

	// Host a game
	bool HostGame(uint16_t port, MultiplayerMode mode, const std::string& player_name);

	// Join a game
	bool JoinGame(const std::string& host, uint16_t port, const std::string& player_name);

	// Host a game via relay server (no port forwarding needed)
	bool HostViaRelay(const std::string& relay_host, uint16_t relay_port,
		MultiplayerMode mode, const std::string& player_name);

	// Join a game via relay server room code
	bool JoinViaRelay(const std::string& relay_host, uint16_t relay_port,
		const std::string& room_code, const std::string& player_name);

	// Process network events (call each frame)
	void Update();

	// Send packet to all connected peers
	void Broadcast(const PacketWriter& packet, bool reliable = true);

	// Send packet to a specific peer
	void SendTo(uint16_t peer_id, const PacketWriter& packet, bool reliable = true);

	// Send packet to server (client only)
	void SendToServer(const PacketWriter& packet, bool reliable = true);

	// Disconnect
	void Disconnect();

	// State queries
	bool IsHost() const { return is_host; }
	bool IsClient() const { return is_client; }
	bool IsConnected() const { return is_host || is_client; }
	bool IsRelay() const { return is_relay; }
	bool IsRelayConnected() const { return relay && relay->IsConnected(); }
	bool HasRelayRoomCode() const { return relay && relay->HasRoomCode(); }
	std::string GetRelayRoomCode() const { return relay ? relay->GetRoomCode() : ""; }
	bool HasRelayJoinResult() const { return relay && relay->HasJoinResult(); }
	bool IsRelayJoinSuccess() const { return relay && relay->IsJoinSuccess(); }
	std::string GetRelayJoinFailReason() const { return relay ? relay->GetJoinFailReason() : ""; }

	// Server browser (relay only)
	bool ConnectToRelayForBrowse(const std::string& relay_host, uint16_t relay_port);
	void RequestRoomList();
	bool HasRoomList() const { return browse_relay && browse_relay->HasRoomList(); }
	std::vector<RoomListEntry> GetRoomList() const { return browse_relay ? browse_relay->GetRoomList() : std::vector<RoomListEntry>{}; }
	void ClearRoomList() { if (browse_relay) browse_relay->ClearRoomList(); }
	void UpdateBrowse();
	void DisconnectBrowse();

	uint16_t GetLocalPeerId() const { return local_peer_id; }
	const std::string& GetLocalPlayerName() const { return local_player_name; }
	void SetLocalPlayerName(const std::string& name) { local_player_name = name; }
	MultiplayerMode GetMode() const { return current_mode; }
	const std::vector<PeerInfo>& GetPeers() const { return peers; }
	bool IsGameStarted() const { return game_started; }
	void MarkGameStarted() { game_started = true; }
	void ClearGameStarted() { game_started = false; }
	bool IsJoinSnapshotReady() const {
		return game_started && host_map_ready && host_switches_ready && host_variables_ready;
	}
	void ClearJoinSnapshot() {
		game_started = false;
		host_map_ready = false;
		host_switches_ready = false;
		host_variables_ready = false;
	}
	bool IsHostDisconnected() const { return host_disconnected; }
	void ClearHostDisconnected() { host_disconnected = false; }

	// Host map snapshot (client only). The ready flags are consumed together
	// via IsJoinSnapshotReady()/ClearJoinSnapshot(); the data below survives.
	int GetHostMapId() const { return host_map_id; }
	int GetHostMapX() const { return host_map_x; }
	int GetHostMapY() const { return host_map_y; }
	const std::vector<HostPartyMember>& GetHostParty() const { return host_party; }
	const std::vector<bool>& GetHostSwitches() const { return host_switches; }
	const std::vector<int32_t>& GetHostVariables() const { return host_variables; }

	// Game name (set from relay room info or JoinAccept)
	const std::string& GetHostGameName() const { return host_game_name; }
	void SetHostGameName(const std::string& name) { host_game_name = name; }

	// Game file transfer
	void RequestGameFiles();
	void HandleGameFileRequest(uint16_t peer_id);
	void HandleGameFileInfo(PacketReader& reader);
	void HandleGameFileData(PacketReader& reader);
	void HandleGameFileDone();
	void UpdateFileTransfers();
	GameFileTransferClient* GetClientTransfer() { return client_transfer.get(); }
	bool IsDownloadingGame() const { return client_transfer != nullptr; }

	// Callbacks
	void SetPacketCallback(PacketCallback cb) { packet_callback = std::move(cb); }
	void SetConnectCallback(std::function<void(uint16_t)> cb) { connect_callback = std::move(cb); }
	void SetDisconnectCallback(std::function<void(uint16_t)> cb) { disconnect_callback = std::move(cb); }

	// Peer lookup
	PeerInfo* FindPeer(uint16_t peer_id);

private:
	NetManager() = default;
	~NetManager();
	NetManager(const NetManager&) = delete;
	NetManager& operator=(const NetManager&) = delete;

	void ProcessEvent(ENetEvent& event);
	void HandleServerEvent(ENetEvent& event);
	void HandleClientEvent(ENetEvent& event);
	void HandleRelayForward(uint16_t source_id, const uint8_t* data, size_t len);
	void HandleRelayPeerConnected(uint16_t peer_id, const std::string& name);
	void HandleRelayPeerDisconnected(uint16_t peer_id);
	uint16_t AllocatePeerId();
	PeerInfo* FindPeerByENet(ENetPeer* peer);
	void RemovePeer(ENetPeer* peer);

	ENetHost* enet_host = nullptr;
	ENetPeer* server_peer = nullptr;  // Client: connection to server

	// Relay connection
	std::unique_ptr<RelayConnection> relay;
	std::unique_ptr<RelayConnection> browse_relay;
	bool is_relay = false;

	bool initialized = false;
	bool is_host = false;
	bool is_client = false;
	bool game_started = false;
	bool host_disconnected = false;
	bool host_map_ready = false;
	bool host_switches_ready = false;
	bool host_variables_ready = false;
	int host_map_id = 0;
	int host_map_x = 0;
	int host_map_y = 0;
	std::vector<HostPartyMember> host_party;
	std::vector<bool> host_switches;
	std::vector<int32_t> host_variables;
	std::string host_game_name;
	uint16_t local_peer_id = 0;
	uint16_t next_peer_id = 1;
	std::string local_player_name;
	MultiplayerMode current_mode = MultiplayerMode::Normal;

	std::vector<PeerInfo> peers;

	PacketCallback packet_callback;
	std::function<void(uint16_t)> connect_callback;
	std::function<void(uint16_t)> disconnect_callback;

	// File transfer state
	std::unordered_map<uint16_t, std::unique_ptr<GameFileTransferHost>> host_transfers;
	std::unique_ptr<GameFileTransferClient> client_transfer;
};

} // namespace Chaos

#endif
