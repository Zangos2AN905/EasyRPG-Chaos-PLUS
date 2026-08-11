/*
 * Chaos Fork: Multiplayer Lobby Scene Implementation
 */

#include "chaos/scene_multiplayer_lobby.h"
#include "chaos/net_manager.h"
#include "chaos/multiplayer_mode.h"
#include "chaos/multiplayer_state.h"
#include "chaos/multiplayer_chat.h"
#include "chaos/custom_mode.h"
#include "chaos/scene_custom_mode_setup.h"
#include "chaos/discord_integration.h"
#include "chaos/scene_skin_select.h"
#include "chaos/game_file_transfer.h"
#include "input.h"
#include "player.h"
#include "scene.h"
#include "scene_logo.h"
#include "scene_title.h"
#include "game_system.h"
#include "main_data.h"
#include "game_clock.h"
#include "output.h"
#include "filefinder.h"
#include "chaos/scene_multiplayer_wait.h"
#include <fmt/format.h>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Chaos {

static constexpr uint16_t DEFAULT_PORT = 6510;
static constexpr int MAX_NAME_LEN = 12;

// Default relay server address
static const std::string RELAY_HOST = "179.198.114.183";
static constexpr uint16_t RELAY_PORT = 6510;

// Characters available for name entry
static const std::string name_chars =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-";

static const char* ModerationLevelName(ModerationLevel lv) {
	switch (lv) {
		case ModerationLevel::None:     return "None";
		case ModerationLevel::Basic:    return "Basic";
		case ModerationLevel::Moderate: return "Moderate";
		case ModerationLevel::Strict:   return "Strict";
	}
	return "Moderate";
}

static std::vector<std::string> BuildLobbySettingsOptions() {
	auto& chat = MultiplayerChat::Instance();
	return {
		std::string("Chat: ") + (chat.IsChatEnabled() ? "On" : "Off"),
		std::string("Moderation Type: ") + ModerationLevelName(chat.GetModerationLevel())
	};
}

static std::string GetLobbySettingsDescription(int index) {
	switch (index) {
		case 0:
			return "Toggle chat on or off. Press DECISION to change.";
		case 1:
			return "Cycle moderation: None, Basic, Moderate, Strict. Default: Basic.";
		default:
			return "";
	}
}

static void CycleLobbySettingValue(int index) {
	auto& chat = MultiplayerChat::Instance();
	switch (index) {
		case 0: // Chat on/off
			chat.SetChatEnabled(!chat.IsChatEnabled());
			break;
		case 1: { // Moderation level cycle
			int lv = static_cast<int>(chat.GetModerationLevel());
			lv = (lv + 1) % 4;
			chat.SetModerationLevel(static_cast<ModerationLevel>(lv));
			break;
		}
		default:
			break;
	}
}

Scene_MultiplayerLobby::Scene_MultiplayerLobby() {
	type = Scene::MultiplayerLobby;
}

void Scene_MultiplayerLobby::Start() {
	player_name = "Player";

	// If Discord RPC is enabled and we have a username, offer it as default
	if (DiscordIntegration::IsEnabled() && DiscordIntegration::HasDiscordUser()) {
		std::string discord_name = DiscordIntegration::GetDiscordUsername();
		if (!discord_name.empty() && discord_name.size() <= static_cast<size_t>(MAX_NAME_LEN)) {
			player_name = discord_name;
		} else if (!discord_name.empty()) {
			player_name = discord_name.substr(0, MAX_NAME_LEN);
		}
	}

	// Pad name to MAX_NAME_LEN with spaces for editing
	player_name.resize(MAX_NAME_LEN, ' ');
	name_cursor = 0;

	ip_octets[0] = "127";
	ip_octets[1] = "000";
	ip_octets[2] = "000";
	ip_octets[3] = "001";
	ip_octet_idx = 0;
	ip_digit_idx = 0;

	CreateWindows();
	Game_Clock::ResetFrame(Game_Clock::now());

	// Set up Discord join callback so users can join via Discord
	DiscordIntegration::SetJoinCallback([this](const std::string& room_code) {
		// Trim the player name for use
		std::string trimmed = player_name;
		size_t end = trimmed.find_last_not_of(' ');
		if (end != std::string::npos) {
			trimmed = trimmed.substr(0, end + 1);
		} else {
			trimmed = "Player";
		}

		Output::Debug("Discord: Joining room {} via Discord invite", room_code);
		auto& net = NetManager::Instance();
		if (net.IsConnected()) {
			Output::Debug("Discord: Already connected, ignoring join request");
			return;
		}

		if (!net.JoinViaRelay(RELAY_HOST, RELAY_PORT, room_code, trimmed)) {
			Output::Debug("Discord: Failed to join room via Discord invite");
			return;
		}

		// Transition to relay waiting state
		state = LobbyState::RelayWaiting;
		relay_mode = true;
		connect_timer = 0;
		name_window->SetVisible(false);
		hostjoin_window->SetVisible(false);
		mode_window->SetVisible(false);
		ip_window->SetVisible(false);
		browser_window->SetVisible(false);
		browser_info_window->SetVisible(false);
		status_window->SetText("Joining room via Discord...");
		status_window->SetVisible(true);
		playerlist_window->SetVisible(true);
		help_window->SetText(fmt::format("Joining room {}...", room_code));
	});
}

void Scene_MultiplayerLobby::CreateWindows() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	if (DiscordIntegration::IsEnabled() && DiscordIntegration::HasDiscordUser()) {
		help_window->SetText(fmt::format("Discord: {} - UP/DOWN change, L/R move",
			DiscordIntegration::GetDiscordUsername()));
	} else {
		help_window->SetText("Enter your name (UP/DOWN change, L/R move)");
	}

	// Name entry display
	name_window = std::make_unique<Window_Help>(
		Player::screen_width / 4, 48,
		Player::screen_width / 2, 32);
	name_window->SetText("Name: " + player_name);

	// Host or Join selection
	std::vector<std::string> hj_options;
	hj_options.push_back("Host (LAN)");
	hj_options.push_back("Join (LAN)");
	hj_options.push_back("Host (Online)");
	hj_options.push_back("Join (Online)");
	hostjoin_window = std::make_unique<Window_Command>(hj_options, Player::screen_width / 2);
	hostjoin_window->SetX(Player::screen_width / 4);
	hostjoin_window->SetY(48);
	hostjoin_window->SetVisible(false);

	// Mode selection (for host)
	// Custom mode is included in the enum; it is handled specially in
	// UpdateModeSelect/UpdateRelayModeSelect (opens the setup scene).
	std::vector<std::string> mode_options;
	for (int i = 0; i < GetModeCount(); ++i) {
		auto& props = GetModeProperties(static_cast<MultiplayerMode>(i));
		mode_options.push_back(props.name);
	}
	mode_window = std::make_unique<Window_Command>(mode_options, Player::screen_width / 2);
	mode_window->SetX(Player::screen_width / 4);
	mode_window->SetY(32);
	mode_window->SetHeight(static_cast<int>(mode_options.size()) * 16 + 32);
	mode_window->SetVisible(false);

	// IP entry display
	ip_window = std::make_unique<Window_Help>(
		Player::screen_width / 4, 48,
		Player::screen_width / 2, 32);
	ip_window->SetVisible(false);

	// Status window (connection status, player list)
	status_window = std::make_unique<Window_Help>(0, 32, Player::screen_width, 32);
	status_window->SetVisible(false);

	// Player list
	playerlist_window = std::make_unique<Window_Help>(
		0, 64, Player::screen_width, Player::screen_height - 96);
	playerlist_window->SetVisible(false);

	settings_window = std::make_unique<Window_Command>(
		BuildLobbySettingsOptions(),
		Player::screen_width - (Player::screen_width / 2),
		5);
	settings_window->SetX(Player::screen_width / 2);
	settings_window->SetY(64);
	settings_window->SetVisible(false);
	settings_window->SetActive(false);
	settings_window->SetIndex(0);

	// Start button for host
	std::vector<std::string> start_options;
	start_options.push_back("Start Game");
	start_window = std::make_unique<Window_Command_Horizontal>(start_options, Player::screen_width / 2);
	start_window->SetX(Player::screen_width / 3);
	start_window->SetY(Player::screen_height - 32);
	start_window->SetVisible(false);

	// Server browser - room selection list (initially empty)
	std::vector<std::string> empty_list;
	empty_list.push_back("Loading...");
	browser_window = std::make_unique<Window_Command>(empty_list, Player::screen_width);
	browser_window->SetX(0);
	browser_window->SetY(48);
	browser_window->SetVisible(false);

	// Browser info bar
	browser_info_window = std::make_unique<Window_Help>(
		0, Player::screen_height - 32, Player::screen_width, 32);
	browser_info_window->SetVisible(false);
}

void Scene_MultiplayerLobby::RefreshLobbyActionWindow() {
	std::vector<std::string> options;
	if (NetManager::Instance().IsHost()) {
		options.push_back("Start Game");
	}
	options.push_back("Settings");
	options.push_back("Skins");

	int selected = start_window ? start_window->GetIndex() : 0;
	bool visible = start_window ? start_window->IsVisible() : false;

	start_window = std::make_unique<Window_Command_Horizontal>(options, Player::screen_width / 2);
	start_window->SetX((Player::screen_width - start_window->GetWidth()) / 2);
	start_window->SetY(Player::screen_height - start_window->GetHeight());
	start_window->SetVisible(visible);
	start_window->SetActive(visible && !settings_open);
	start_window->SetIndex(std::clamp(selected, 0, static_cast<int>(options.size()) - 1));
}

void Scene_MultiplayerLobby::RefreshSettingsWindow() {
	int selected = settings_window ? settings_window->GetIndex() : 0;
	settings_window = std::make_unique<Window_Command>(
		BuildLobbySettingsOptions(),
		Player::screen_width - (Player::screen_width / 2),
		5);
	settings_window->SetX(Player::screen_width / 2);
	settings_window->SetY(64);
	settings_window->SetVisible(settings_open);
	settings_window->SetActive(settings_open);
	settings_window->SetIndex(std::clamp(selected, 0, 1));
}

std::string Scene_MultiplayerLobby::GetWaitingHelpText() const {
	auto& net = NetManager::Instance();

	if (settings_open) {
		return GetLobbySettingsDescription(settings_window ? settings_window->GetIndex() : 0);
	}

	if (net.IsHost()) {
		if (net.IsRelay() && net.HasRelayRoomCode()) {
			return fmt::format("Room Code: {} - Share with friends!", net.GetRelayRoomCode());
		}
		return "Waiting for players... Choose Start Game or Settings";
	}

	if (net.GetLocalPeerId() != 0) {
		return "Connected to lobby - choose Settings or wait for host";
	}

	return "Connecting to lobby...";
}

void Scene_MultiplayerLobby::SetSettingsOpen(bool open) {
	settings_open = open;

	bool playerlist_visible = playerlist_window ? playerlist_window->IsVisible() : false;
	int playerlist_width = open ? Player::screen_width / 2 : Player::screen_width;
	playerlist_window = std::make_unique<Window_Help>(
		0, 64, playerlist_width, Player::screen_height - 96);
	playerlist_window->SetVisible(playerlist_visible);
	RefreshPlayerList();

	RefreshSettingsWindow();
	if (start_window) {
		start_window->SetActive(start_window->IsVisible() && !settings_open);
	}

	help_window->SetText(GetWaitingHelpText());
}

void Scene_MultiplayerLobby::vUpdate() {
	switch (state) {
		case LobbyState::NameEntry:
			UpdateNameEntry();
			break;
		case LobbyState::HostOrJoin:
			UpdateHostOrJoin();
			break;
		case LobbyState::ModeSelect:
			UpdateModeSelect();
			break;
		case LobbyState::IpEntry:
			UpdateIpEntry();
			break;
		case LobbyState::RelayModeSelect:
			UpdateRelayModeSelect();
			break;
		case LobbyState::RelayWaiting:
			UpdateRelayWaiting();
			break;
		case LobbyState::ServerBrowser:
			UpdateServerBrowser();
			break;
		case LobbyState::Downloading:
			UpdateDownloading();
			break;
		case LobbyState::Waiting:
			UpdateWaiting();
			break;
	}
}

void Scene_MultiplayerLobby::UpdateNameEntry() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
		return;
	}

	if (Input::IsTriggered(Input::RIGHT)) {
		name_cursor = std::min(name_cursor + 1, MAX_NAME_LEN - 1);
	}
	if (Input::IsTriggered(Input::LEFT)) {
		name_cursor = std::max(name_cursor - 1, 0);
	}

	if (name_cursor >= 0 && name_cursor < static_cast<int>(player_name.size())) {
		char& c = player_name[name_cursor];
		size_t pos = name_chars.find(c);

		if (Input::IsRepeated(Input::UP)) {
			if (pos == std::string::npos) {
				c = name_chars[0]; // Default to 'A'
			} else {
				c = name_chars[(pos + 1) % name_chars.size()];
			}
		}
		if (Input::IsRepeated(Input::DOWN)) {
			if (pos == std::string::npos) {
				c = name_chars[name_chars.size() - 1];
			} else {
				c = name_chars[(pos + name_chars.size() - 1) % name_chars.size()];
			}
		}
	}

	// Display with cursor
	std::string display = "Name: ";
	for (int i = 0; i < static_cast<int>(player_name.size()); ++i) {
		if (i == name_cursor) {
			display += '[';
			display += player_name[i];
			display += ']';
		} else {
			display += player_name[i];
		}
	}
	name_window->SetText(display);

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));

		// Trim trailing spaces
		std::string trimmed = player_name;
		size_t end = trimmed.find_last_not_of(' ');
		if (end != std::string::npos) {
			trimmed = trimmed.substr(0, end + 1);
		} else {
			trimmed = "Player"; // All spaces, use default
		}
		player_name = trimmed;

		state = LobbyState::HostOrJoin;
		name_window->SetVisible(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
	}
}

void Scene_MultiplayerLobby::UpdateHostOrJoin() {
	hostjoin_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		// Go back to name entry
		state = LobbyState::NameEntry;
		hostjoin_window->SetVisible(false);
		// Re-pad name for editing
		player_name.resize(MAX_NAME_LEN, ' ');
		name_cursor = 0;
		name_window->SetVisible(true);
		help_window->SetText("Enter your name (UP/DOWN change, L/R move)");
		return;
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		int idx = hostjoin_window->GetIndex();
		if (idx == 0) {
			// Host (LAN) - show mode selection
			relay_mode = false;
			state = LobbyState::ModeSelect;
			hostjoin_window->SetVisible(false);
			mode_window->SetVisible(true);
			help_window->SetText("Select game mode");
		} else if (idx == 1) {
			// Join (LAN) - show IP entry
			relay_mode = false;
			state = LobbyState::IpEntry;
			hostjoin_window->SetVisible(false);
			ip_window->SetVisible(true);
			ip_octet_idx = 0;
			ip_digit_idx = 0;

			// Show initial IP
			std::string display = fmt::format("IP: {}.{}.{}.{}",
				ip_octets[0], ip_octets[1], ip_octets[2], ip_octets[3]);
			ip_window->SetText(display);
			help_window->SetText("Enter IP (UP/DOWN digit, L/R move, DECISION connect)");
		} else if (idx == 2) {
			// Host (Online) - show mode selection for relay
			relay_mode = true;
			state = LobbyState::RelayModeSelect;
			hostjoin_window->SetVisible(false);
			mode_window->SetVisible(true);
			help_window->SetText("Select game mode (Online)");
		} else if (idx == 3) {
			// Browse Servers
			relay_mode = true;
			state = LobbyState::ServerBrowser;
			hostjoin_window->SetVisible(false);
			browser_window->SetVisible(true);
			browser_info_window->SetVisible(true);
			help_window->SetText("Server Browser - DECISION to join");
			browser_fetching = true;
			browser_refresh_timer = 0;

			auto& net = NetManager::Instance();
			if (net.ConnectToRelayForBrowse(RELAY_HOST, RELAY_PORT)) {
				net.RequestRoomList();
			} else {
				help_window->SetText("Failed to connect to relay server");
				browser_fetching = false;
			}
		}
	}
}

void Scene_MultiplayerLobby::UpdateModeSelect() {
	// Returning from custom mode setup: start hosting with Custom mode
	if (Chaos::CustomMode::Instance().HasPendingSettings()) {
		mode_window->SetVisible(false);
		StartHostingCustom();
		return;
	}

	mode_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		state = LobbyState::HostOrJoin;
		mode_window->SetVisible(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
		return;
	}

	// Show mode description in help
	int idx = mode_window->GetIndex();
	int custom_idx = static_cast<int>(Chaos::MultiplayerMode::Custom);
	if (idx == custom_idx) {
		help_window->SetText("Mix two modes, pick an objective, toggle chaos features.");
	} else if (idx >= 0 && idx < GetModeCount()) {
		auto& props = GetModeProperties(static_cast<MultiplayerMode>(idx));
		help_window->SetText(props.description);
	}

	if (Input::IsTriggered(Input::DECISION)) {
		if (idx == custom_idx) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			Scene::Push(std::make_shared<Scene_CustomModeSetup>(false), false);
			return;
		}
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		StartHosting();
	}
}

void Scene_MultiplayerLobby::UpdateIpEntry() {
	// IP editing: 4 octets, each 3 digits (000-255)
	// LEFT/RIGHT moves between digits (across octets)
	// UP/DOWN increments/decrements the current digit

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		state = LobbyState::HostOrJoin;
		ip_window->SetVisible(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
		return;
	}

	// Navigate: 12 digit positions total (4 octets x 3 digits)
	int flat_pos = ip_octet_idx * 3 + ip_digit_idx;
	if (Input::IsTriggered(Input::RIGHT)) {
		flat_pos = std::min(flat_pos + 1, 11);
		ip_octet_idx = flat_pos / 3;
		ip_digit_idx = flat_pos % 3;
	}
	if (Input::IsTriggered(Input::LEFT)) {
		flat_pos = std::max(flat_pos - 1, 0);
		ip_octet_idx = flat_pos / 3;
		ip_digit_idx = flat_pos % 3;
	}

	// Modify digit
	char& c = ip_octets[ip_octet_idx][ip_digit_idx];
	if (Input::IsRepeated(Input::UP)) {
		c = (c == '9') ? '0' : c + 1;
	}
	if (Input::IsRepeated(Input::DOWN)) {
		c = (c == '0') ? '9' : c - 1;
	}

	// Clamp octet to 0-255
	int val = std::stoi(ip_octets[ip_octet_idx]);
	if (val > 255) {
		ip_octets[ip_octet_idx] = "255";
	}

	// Build display string with cursor indicator
	std::string display = "IP: ";
	for (int o = 0; o < 4; ++o) {
		if (o > 0) display += '.';
		for (int d = 0; d < 3; ++d) {
			if (o == ip_octet_idx && d == ip_digit_idx) {
				display += '[';
				display += ip_octets[o][d];
				display += ']';
			} else {
				display += ip_octets[o][d];
			}
		}
	}
	ip_window->SetText(display);

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		StartJoining();
	}
}

void Scene_MultiplayerLobby::UpdateWaiting() {
	auto& net = NetManager::Instance();
	net.Update();

	if (settings_open && Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		SetSettingsOpen(false);
		return;
	}

	if (!settings_open && Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		net.Disconnect();
		connected = false;
		settings_open = false;
		state = LobbyState::HostOrJoin;
		status_window->SetVisible(false);
		playerlist_window->SetVisible(false);
		start_window->SetVisible(false);
		settings_window->SetVisible(false);
		settings_window->SetActive(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
		// Clear Discord multiplayer presence
		DiscordIntegration::ClearMultiplayerPresence();
		return;
	}

	// Refresh player list
	RefreshPlayerList();

	// Update Discord party size
	if (DiscordIntegration::IsEnabled() && net.IsRelay() && net.HasRelayRoomCode()) {
		auto& props = GetModeProperties(net.GetMode());
		int max_players = props.max_players > 0 ? props.max_players : 32;
		DiscordIntegration::SetPartyInfo(
			"room_" + net.GetRelayRoomCode(),
			static_cast<int>(net.GetPeers().size()) + 1,
			max_players);
	}

	if (net.IsHost()) {
		// Show room code in status for relay mode
		if (net.IsRelay() && net.HasRelayRoomCode()) {
			auto& props = GetModeProperties(net.GetMode());
			status_window->SetText(fmt::format("Room: {}  Mode: {}  Players: {}",
				net.GetRelayRoomCode(), props.name, static_cast<int>(net.GetPeers().size()) + 1));
		}
	} else {
		// Client waits for GameStart packet
		connect_timer++;

		// The host started the game (fresh start or late join).
		// The wait scene consumes the join snapshot when it is complete.
		if (net.IsGameStarted() || net.IsJoinSnapshotReady()) {
			ClientStartGame();
			return;
		}

		if (!net.IsClient()) {
			status_window->SetText("Disconnected from server");
		} else if (net.GetLocalPeerId() == 0) {
			status_window->SetText(fmt::format("Connecting... ({}s)", connect_timer / 60));
		} else {
			// Got peer_id - check game on first frame
			if (!game_check_done) {
				game_check_done = true;
				if (!IsHostGameAvailable()) {
					Output::Debug("Multiplayer: Host game '{}' not found locally, starting download",
						net.GetHostGameName());
					net.RequestGameFiles();
					state = LobbyState::Downloading;
					download_timer = 0;
					status_window->SetText(fmt::format("Downloading {}...", net.GetHostGameName()));
					help_window->SetText("Game not found locally - downloading from host");
					return;
				}
			}
			status_window->SetText(fmt::format("Connected as {} (waiting for host to start)",
				net.GetLocalPlayerName()));
		}
	}

	if (settings_open) {
		settings_window->Update();
		help_window->SetText(GetWaitingHelpText());
		if (Input::IsTriggered(Input::DECISION)) {
			int idx = settings_window->GetIndex();
			// Only the host can change settings
			if (!net.IsHost()) {
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
			} else if (idx <= 1) {
				CycleLobbySettingValue(idx);
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			}
			RefreshSettingsWindow();
		}
		return;
	}

	if (start_window->IsVisible()) {
		start_window->Update();
		if (Input::IsTriggered(Input::DECISION) && start_window->GetActive()) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			int idx = start_window->GetIndex();

			// Determine which button was pressed based on layout:
			// Host:   [Start Game] [Settings] [Skins]
			// Client: [Settings] [Skins]
			int settings_idx = net.IsHost() ? 1 : 0;
			int skins_idx = net.IsHost() ? 2 : 1;

			if (net.IsHost() && idx == 0) {
				StartGame();
				return;
			} else if (idx == settings_idx) {
				SetSettingsOpen(true);
				return;
			} else if (idx == skins_idx) {
				Scene::Push(std::make_shared<Scene_SkinSelect>());
				return;
			}
		}
	}

	help_window->SetText(GetWaitingHelpText());
}

void Scene_MultiplayerLobby::StartHosting(int mode_idx_override) {
	auto& net = NetManager::Instance();
	int mode_idx = mode_idx_override >= 0 ? mode_idx_override : mode_window->GetIndex();
	if (mode_idx < 0 || mode_idx >= GetModeCount()) return;
	auto mode = static_cast<MultiplayerMode>(mode_idx);
	auto& props = GetModeProperties(mode);

	if (!net.HostGame(DEFAULT_PORT, mode, player_name)) {
		help_window->SetText("Failed to create server!");
		return;
	}

	state = LobbyState::Waiting;
	connected = true;
	settings_open = false;
	mode_window->SetVisible(false);

	status_window->SetText(fmt::format("Hosting: {} (port {})", props.name, DEFAULT_PORT));
	status_window->SetVisible(true);
	playerlist_window->SetVisible(true);
	RefreshLobbyActionWindow();
	start_window->SetVisible(true);
	start_window->SetActive(true);
	settings_window->SetVisible(false);
	settings_window->SetActive(false);

	help_window->SetText(GetWaitingHelpText());
}

void Scene_MultiplayerLobby::StartHostingCustom() {
	CustomMode::Instance().TakePendingSettings();
	StartHosting(static_cast<int>(Chaos::MultiplayerMode::Custom));
}

void Scene_MultiplayerLobby::StartJoining() {
	auto& net = NetManager::Instance();

	// Build IP string from octets, stripping leading zeros
	std::string ip;
	for (int i = 0; i < 4; ++i) {
		if (i > 0) ip += '.';
		int val = std::stoi(ip_octets[i]);
		ip += std::to_string(val);
	}

	if (!net.JoinGame(ip, DEFAULT_PORT, player_name)) {
		help_window->SetText("Failed to connect!");
		return;
	}

	state = LobbyState::Waiting;
	connect_timer = 0;
	settings_open = false;
	ip_window->SetVisible(false);

	status_window->SetText("Connecting...");
	status_window->SetVisible(true);
	playerlist_window->SetVisible(true);
	RefreshLobbyActionWindow();
	start_window->SetVisible(true);
	start_window->SetActive(true);
	settings_window->SetVisible(false);
	settings_window->SetActive(false);

	help_window->SetText(fmt::format("Connecting to {}...", ip));
}

void Scene_MultiplayerLobby::UpdateRelayModeSelect() {
	// Returning from custom mode setup: start relay hosting with Custom mode
	if (Chaos::CustomMode::Instance().HasPendingSettings()) {
		mode_window->SetVisible(false);
		StartRelayHostingCustom();
		return;
	}

	mode_window->Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		state = LobbyState::HostOrJoin;
		mode_window->SetVisible(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
		return;
	}

	int idx = mode_window->GetIndex();
	int custom_idx = static_cast<int>(Chaos::MultiplayerMode::Custom);
	if (idx == custom_idx) {
		help_window->SetText("Mix two modes, pick an objective, toggle chaos features.");
	} else if (idx >= 0 && idx < GetModeCount()) {
		auto& props = GetModeProperties(static_cast<MultiplayerMode>(idx));
		help_window->SetText(props.description);
	}

	if (Input::IsTriggered(Input::DECISION)) {
		if (idx == custom_idx) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			Scene::Push(std::make_shared<Scene_CustomModeSetup>(true), false);
			return;
		}
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		StartRelayHosting();
	}
}

void Scene_MultiplayerLobby::UpdateRelayWaiting() {
	auto& net = NetManager::Instance();
	net.Update();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		net.Disconnect();
		connected = false;
		settings_open = false;
		state = LobbyState::HostOrJoin;
		status_window->SetVisible(false);
		playerlist_window->SetVisible(false);
		start_window->SetVisible(false);
		settings_window->SetVisible(false);
		settings_window->SetActive(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
		DiscordIntegration::ClearMultiplayerPresence();
		return;
	}

	if (net.IsHost()) {
		// Waiting for relay room code
		if (net.HasRelayRoomCode()) {
			std::string code = net.GetRelayRoomCode();
			auto mode = net.GetMode();
			auto& props = GetModeProperties(mode);
			status_window->SetText(fmt::format("Room: {}  Mode: {}", code, props.name));

			// Set Discord join secret so people can join via Discord
			if (DiscordIntegration::IsEnabled()) {
				DiscordIntegration::SetJoinSecret(code);
				int max_players = props.max_players > 0 ? props.max_players : 32;
				DiscordIntegration::SetPartyInfo(
					"room_" + code,
					static_cast<int>(net.GetPeers().size()) + 1,
					max_players);
			}

			// Transition to main waiting lobby
			state = LobbyState::Waiting;
			settings_open = false;
			RefreshLobbyActionWindow();
			start_window->SetVisible(true);
			start_window->SetActive(true);
			settings_window->SetVisible(false);
			settings_window->SetActive(false);
			help_window->SetText(GetWaitingHelpText());
		} else if (!net.IsRelayConnected()) {
			status_window->SetText("Relay connection failed!");
			help_window->SetText("Failed to connect to relay server");
		} else {
			status_window->SetText("Creating room...");
		}
	} else {
		// Client waiting for join result
		if (net.GetLocalPeerId() != 0) {
			// Got JoinAccept via relay - check if host's game is available
			if (!IsHostGameAvailable()) {
				// Need to download
				Output::Debug("Multiplayer: Host game '{}' not found locally, starting download",
					net.GetHostGameName());
				net.RequestGameFiles();
				state = LobbyState::Downloading;
				download_timer = 0;
				status_window->SetText(fmt::format("Downloading {}...", net.GetHostGameName()));
				help_window->SetText("Game not found locally - downloading from host");
			} else {
				state = LobbyState::Waiting;
				settings_open = false;
				RefreshLobbyActionWindow();
				start_window->SetVisible(true);
				start_window->SetActive(true);
				settings_window->SetVisible(false);
				settings_window->SetActive(false);
				status_window->SetText(fmt::format("Connected as {} (waiting for host to start)",
					net.GetLocalPlayerName()));
				help_window->SetText(GetWaitingHelpText());
			}
		} else if (!net.IsClient()) {
			// Join failed or disconnected
			status_window->SetText("Connection failed");
			help_window->SetText("Failed to join room");
		} else {
			status_window->SetText("Joining room...");
		}
	}

	RefreshPlayerList();
}

void Scene_MultiplayerLobby::UpdateServerBrowser() {
	auto& net = NetManager::Instance();
	net.UpdateBrowse();

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		net.DisconnectBrowse();
		state = LobbyState::HostOrJoin;
		browser_window->SetVisible(false);
		browser_info_window->SetVisible(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
		return;
	}

	// Check if room list arrived
	if (browser_fetching && net.HasRoomList()) {
		browser_fetching = false;
		RefreshServerList();
	}

	// Auto-refresh every 5 seconds
	browser_refresh_timer++;
	if (browser_refresh_timer >= 300) {
		browser_refresh_timer = 0;
		net.RequestRoomList();
		browser_fetching = true;
	}

	browser_window->Update();

	// Show info for selected room
	int idx = browser_window->GetIndex();
	if (idx >= 0 && idx < static_cast<int>(browser_room_codes.size())) {
		std::string game = browser_game_names[idx];
		std::string local_game = Player::game_title.empty() ? "" : Player::game_title;
		std::string match;
		if (local_game.empty()) {
			match = "No game loaded";
		} else if (game == local_game) {
			match = "Same game - quick join!";
		} else {
			match = "Different game - download needed";
		}
		browser_info_window->SetText(fmt::format("Game: {} | {}", game, match));
	}

	if (Input::IsTriggered(Input::DECISION)) {
		if (idx >= 0 && idx < static_cast<int>(browser_room_codes.size())) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));

			room_code = browser_room_codes[idx];
			net.DisconnectBrowse();

			browser_window->SetVisible(false);
			browser_info_window->SetVisible(false);

			// Join the selected room
			auto& nm = NetManager::Instance();
			if (!nm.JoinViaRelay(RELAY_HOST, RELAY_PORT, room_code, player_name)) {
				help_window->SetText("Failed to connect to relay server!");
				state = LobbyState::HostOrJoin;
				hostjoin_window->SetVisible(true);
				return;
			}

			state = LobbyState::RelayWaiting;
			connect_timer = 0;
			status_window->SetText("Joining room...");
			status_window->SetVisible(true);
			playerlist_window->SetVisible(true);
			help_window->SetText(fmt::format("Joining room {}...", room_code));
		}
	}
}

void Scene_MultiplayerLobby::RefreshServerList() {
	auto& net = NetManager::Instance();
	auto rooms = net.GetRoomList();
	net.ClearRoomList();

	browser_room_codes.clear();
	browser_game_names.clear();

	std::vector<std::string> options;
	if (rooms.empty()) {
		options.push_back("No servers found");
	} else {
		for (auto& r : rooms) {
			std::string mode_str = IsSupportedMultiplayerMode(static_cast<MultiplayerMode>(r.mode))
				? GetModeProperties(static_cast<MultiplayerMode>(r.mode)).name
				: "Unsupported";
			if (mode_str == "Unsupported") {
				continue;
			}
			std::string game = r.game_name.empty() ? "Unknown" : r.game_name;
			options.push_back(fmt::format("[{}] {} - {} ({} p.)",
				r.code, game, r.host_name, r.players));
			browser_room_codes.push_back(r.code);
			browser_game_names.push_back(game);
		}
	}

	// Recreate browser window with new options
	browser_window = std::make_unique<Window_Command>(options, Player::screen_width);
	browser_window->SetX(0);
	browser_window->SetY(48);
	browser_window->SetVisible(true);
}

void Scene_MultiplayerLobby::StartRelayHosting(int mode_idx_override) {
	auto& net = NetManager::Instance();
	int mode_idx = mode_idx_override >= 0 ? mode_idx_override : mode_window->GetIndex();
	if (mode_idx < 0 || mode_idx >= GetModeCount()) return;
	auto mode = static_cast<MultiplayerMode>(mode_idx);

	if (!net.HostViaRelay(RELAY_HOST, RELAY_PORT, mode, player_name)) {
		help_window->SetText("Failed to connect to relay server!");
		return;
	}

	state = LobbyState::RelayWaiting;
	connected = true;
	settings_open = false;
	mode_window->SetVisible(false);

	status_window->SetText("Connecting to relay...");
	status_window->SetVisible(true);
	playerlist_window->SetVisible(true);
	RefreshLobbyActionWindow();
	start_window->SetVisible(false);
	start_window->SetActive(false);
	settings_window->SetVisible(false);
	settings_window->SetActive(false);

	help_window->SetText("Creating relay room...");
}

void Scene_MultiplayerLobby::StartRelayHostingCustom() {
	CustomMode::Instance().TakePendingSettings();
	StartRelayHosting(static_cast<int>(Chaos::MultiplayerMode::Custom));
}

void Scene_MultiplayerLobby::StartGame() {
	auto& net = NetManager::Instance();

	// Mark the host session before notifying clients so late joins receive the game state.
	net.MarkGameStarted();
	PacketWriter packet(PacketType::GameStart);
	net.Broadcast(packet, true);

	// Start multiplayer state manager
	MultiplayerState::Instance().StartMultiplayer();

	// If God Mode, randomly assign a god player and broadcast
	if (net.GetMode() == MultiplayerMode::GodMode) {
		MultiplayerState::Instance().AssignRandomGod();
	}

	// If ASYM Mode, randomly assign the hunter and broadcast
	if (net.GetMode() == MultiplayerMode::Asym) {
		MultiplayerState::Instance().AssignRandomHunter();
	}

	// Pop this lobby and proceed to logo/title
	auto logos = Scene_Logo::LoadLogos();
	if (!logos.empty()) {
		Scene::Push(std::make_shared<Scene_Logo>(std::move(logos), 1), true);
		return;
	}
	Scene::Push(std::make_shared<Scene_Title>(), true);
}

void Scene_MultiplayerLobby::ClientStartGame() {
	// Start multiplayer state manager on client side
	MultiplayerState::Instance().StartMultiplayer();

	Output::Debug("Multiplayer: Game starting (client), waiting for host to select a game");

	// Client goes to a waiting screen instead of title screen
	Scene::Push(std::make_shared<Scene_MultiplayerWait>(), true);
}

std::string Scene_MultiplayerLobby::GetExeDirectory() {
#ifdef _WIN32
	wchar_t exe_path[MAX_PATH];
	GetModuleFileNameW(NULL, exe_path, MAX_PATH);
	std::wstring wpath(exe_path);
	size_t pos = wpath.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		wpath = wpath.substr(0, pos);
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
	std::string result;
	result.resize(len - 1);
	WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], len, NULL, NULL);
	return result;
#else
	return ".";
#endif
}

bool Scene_MultiplayerLobby::IsHostGameAvailable() {
	auto& net = NetManager::Instance();
	std::string host_game = net.GetHostGameName();

	if (host_game.empty() || host_game == "Unknown Game") {
		return true;
	}

	// Check if the local game title matches
	if (!Player::game_title.empty() && Player::game_title == host_game) {
		return true;
	}

	// Sanitize game name for directory lookup
	std::string safe_name = host_game;
	for (char& c : safe_name) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
			c = '_';
		}
	}

	std::string game_path = GetExeDirectory() + "/" + safe_name;
	std::error_code ec;
	if (std::filesystem::is_directory(game_path, ec)) {
		SwitchToHostGame(game_path);
		return true;
	}

	return false;
}

void Scene_MultiplayerLobby::SwitchToHostGame(const std::string& game_path) {
	Output::Debug("Multiplayer: Switching to host's game at {}", game_path);

	auto fs = FileFinder::Root().Create(game_path);
	if (fs) {
		FileFinder::SetGameFilesystem(fs);
		Player::CreateGameObjects();
		Output::Debug("Multiplayer: Game objects created for host's game");
	} else {
		Output::Warning("Multiplayer: Failed to create filesystem for {}", game_path);
	}
}

void Scene_MultiplayerLobby::UpdateDownloading() {
	auto& net = NetManager::Instance();
	net.Update();

	download_timer++;

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		net.Disconnect();
		connected = false;
		state = LobbyState::HostOrJoin;
		status_window->SetVisible(false);
		playerlist_window->SetVisible(false);
		start_window->SetVisible(false);
		settings_open = false;
		settings_window->SetVisible(false);
		settings_window->SetActive(false);
		hostjoin_window->SetVisible(true);
		help_window->SetText(fmt::format("Playing as: {}  -  Choose an option", player_name));
		DiscordIntegration::ClearMultiplayerPresence();
		game_check_done = false;
		return;
	}

	auto* transfer = net.GetClientTransfer();
	if (!transfer) {
		// Transfer object not created yet or lost
		std::string dots;
		int n = (download_timer / 30) % 4;
		for (int i = 0; i < n; ++i) dots += '.';
		status_window->SetText(fmt::format("Requesting game files{}", dots));
		return;
	}

	if (transfer->HasError()) {
		status_window->SetText(fmt::format("Download failed: {}", transfer->GetError()));
		help_window->SetText("Press Cancel to go back");
		return;
	}

	if (transfer->IsComplete()) {
		// Determine downloaded game path
		std::string safe_name = net.GetHostGameName();
		for (char& c : safe_name) {
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
				c = '_';
			}
		}
		if (safe_name.empty()) safe_name = "DownloadedGame";

		std::string game_path = GetExeDirectory() + "/" + safe_name;
		SwitchToHostGame(game_path);

		// Move to waiting lobby
		state = LobbyState::Waiting;
		settings_open = false;
		RefreshLobbyActionWindow();
		start_window->SetVisible(true);
		start_window->SetActive(true);
		settings_window->SetVisible(false);
		settings_window->SetActive(false);
		status_window->SetText(fmt::format("Connected as {} (waiting for host to start)",
			net.GetLocalPlayerName()));
		help_window->SetText(GetWaitingHelpText());
		Output::Debug("Multiplayer: Download complete, switched to host's game");
		return;
	}

	// Show progress
	int pct = transfer->GetProgressPercent();
	uint16_t files_done = transfer->GetReceivedFiles();
	uint16_t files_total = transfer->GetTotalFiles();
	if (pct > 0) {
		status_window->SetText(fmt::format("Downloading {}... {}% ({}/{})",
			net.GetHostGameName(), pct, files_done, files_total));
	} else if (transfer->IsInfoReceived()) {
		status_window->SetText(fmt::format("Downloading {}... preparing", net.GetHostGameName()));
	} else {
		std::string dots;
		int n = (download_timer / 30) % 4;
		for (int i = 0; i < n; ++i) dots += '.';
		status_window->SetText(fmt::format("Requesting game files{}", dots));
	}
}

void Scene_MultiplayerLobby::RefreshPlayerList() {
	auto& net = NetManager::Instance();
	auto& peers = net.GetPeers();

	std::string list;
	if (net.IsHost()) {
		list += fmt::format("1. {} (Host)\n", net.GetLocalPlayerName());
	}

	for (auto& p : peers) {
		if (net.IsHost()) {
			list += fmt::format("{}. {}\n", p.peer_id + 1, p.player_name);
		} else {
			list += fmt::format("{}. {}\n", p.peer_id, p.player_name);
		}
	}

	if (!net.IsHost() && net.GetLocalPeerId() != 0) {
		list += fmt::format("{}. {} (You)\n", net.GetLocalPeerId(), net.GetLocalPlayerName());
	}

	if (list.empty()) {
		list = "No players connected";
	}

	playerlist_window->SetText(list);
}

} // namespace Chaos
