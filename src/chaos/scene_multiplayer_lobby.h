/*
 * Chaos Fork: Multiplayer Lobby Scene
 * Host or Join a multiplayer game.
 */

#ifndef EP_CHAOS_SCENE_MULTIPLAYER_LOBBY_H
#define EP_CHAOS_SCENE_MULTIPLAYER_LOBBY_H

#include "scene.h"
#include "window_command.h"
#include "window_command_horizontal.h"
#include "window_help.h"
#include "window_selectable.h"
#include "chaos/relay_connection.h"
#include <memory>
#include <string>
#include <filesystem>

namespace Chaos {

class Scene_MultiplayerLobby : public Scene {
public:
	Scene_MultiplayerLobby();

	void Start() override;
	void vUpdate() override;

private:
	enum class LobbyState {
		NameEntry,       // Enter player name
		HostOrJoin,      // Choose Host or Join
		ModeSelect,      // Host: choose game mode
		IpEntry,         // Join: enter IP address
		RelayModeSelect, // Relay host: choose game mode
		RelayWaiting,    // Relay: waiting for room code or join result
		ServerBrowser,   // Browse online servers
		Downloading,     // Downloading game files from host
		Waiting,         // Connected, waiting to start / waiting for host
	};

	void CreateWindows();
	void UpdateNameEntry();
	void UpdateHostOrJoin();
	void UpdateModeSelect();
	void UpdateIpEntry();
	void UpdateRelayModeSelect();
	void UpdateRelayWaiting();
	void UpdateServerBrowser();
	void UpdateDownloading();
	void UpdateWaiting();

	void StartHosting();
	void StartJoining();
	void StartRelayHosting();
	void StartGame();
	void ClientStartGame();
	void RefreshPlayerList();
	void RefreshServerList();
	void RefreshLobbyActionWindow();
	void RefreshSettingsWindow();
	void SetSettingsOpen(bool open);
	std::string GetWaitingHelpText() const;

	/** Check if host's game is available locally and switch to it if found. */
	bool IsHostGameAvailable();

	/** Switch the game filesystem to a local game directory. */
	void SwitchToHostGame(const std::string& game_path);

	/** Get executable directory for game lookup. */
	std::string GetExeDirectory();

	LobbyState state = LobbyState::NameEntry;

	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_Command> hostjoin_window;
	std::unique_ptr<Window_Command> mode_window;
	std::unique_ptr<Window_Help> ip_window;
	std::unique_ptr<Window_Help> status_window;
	std::unique_ptr<Window_Help> playerlist_window;
	std::unique_ptr<Window_Command_Horizontal> start_window;
	std::unique_ptr<Window_Help> name_window;
	std::unique_ptr<Window_Command> browser_window;
	std::unique_ptr<Window_Help> browser_info_window;
	std::unique_ptr<Window_Command> settings_window;

	std::string player_name;
	int name_cursor = 0;
	std::string ip_octets[4];
	int ip_octet_idx = 0;
	int ip_digit_idx = 0;
	int connect_timer = 0;
	bool connected = false;
	bool relay_mode = false;
	bool settings_open = false;

	// Room code (used by server browser)
	std::string room_code;

	// Server browser
	std::vector<std::string> browser_room_codes;
	std::vector<std::string> browser_game_names;
	bool browser_fetching = false;
	int browser_refresh_timer = 0;

	// Download state
	int download_timer = 0;
	bool game_check_done = false;
};

} // namespace Chaos

#endif
