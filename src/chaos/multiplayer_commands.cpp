/*
 * Chaos Fork: Multiplayer chat commands.
 */

#include "chaos/multiplayer_commands.h"
#include "chaos/multiplayer_state.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "game_actor.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "game_system.h"
#include "lcf/data.h"
#include "lcf/reader_util.h"
#include "main_data.h"
#include "output.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <sstream>
#include <vector>

namespace Chaos {

namespace {

struct ParsedCommand {
	std::string name;
	std::vector<std::string> args;
};

std::string Lower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

bool ParseInt(const std::string& value, int& result) {
	try {
		size_t used = 0;
		long parsed = std::stol(value, &used, 10);
		if (used != value.size() || parsed < INT_MIN || parsed > INT_MAX) return false;
		result = static_cast<int>(parsed);
		return true;
	} catch (...) {
		return false;
	}
}

bool Parse(const std::string& text, ParsedCommand& command) {
	if (text.size() < 2 || text[0] != ';') return false;
	std::istringstream stream(text.substr(1));
	if (!(stream >> command.name)) return false;
	command.name = Lower(command.name);
	for (std::string arg; stream >> arg;) command.args.push_back(std::move(arg));
	return true;
}

std::string Join(const std::vector<std::string>& args, size_t start) {
	std::string result;
	for (size_t i = start; i < args.size(); ++i) {
		if (!result.empty()) result += ' ';
		result += args[i];
	}
	return result;
}

std::vector<uint16_t> AllPlayers() {
	auto& net = NetManager::Instance();
	std::vector<uint16_t> result{net.GetLocalPeerId()};
	for (const auto& peer : net.GetPeers()) {
		if (peer.peer_id != net.GetLocalPeerId()) result.push_back(peer.peer_id);
	}
	return result;
}

bool MatchesName(const std::string& lhs, const std::string& rhs) {
	return Lower(lhs) == Lower(rhs);
}

bool ResolvePlayer(const std::string& selector, uint16_t issuer, uint16_t& result) {
	auto& net = NetManager::Instance();
	if (Lower(selector) == "me") {
		result = issuer;
		return true;
	}

	int numeric_id = 0;
	if (ParseInt(selector, numeric_id) && numeric_id > 0 && numeric_id <= UINT16_MAX) {
		if (numeric_id == net.GetLocalPeerId() || net.FindPeer(static_cast<uint16_t>(numeric_id))) {
			result = static_cast<uint16_t>(numeric_id);
			return true;
		}
	}

	if (MatchesName(selector, net.GetLocalPlayerName())) {
		result = net.GetLocalPeerId();
		return true;
	}
	for (const auto& peer : net.GetPeers()) {
		if (MatchesName(selector, peer.player_name)) {
			result = peer.peer_id;
			return true;
		}
	}
	return false;
}

bool ResolveTargets(const std::string& selector, uint16_t issuer, std::vector<uint16_t>& result) {
	if (Lower(selector) == "all") {
		result = AllPlayers();
		return !result.empty();
	}
	uint16_t peer_id = 0;
	if (!ResolvePlayer(selector, issuer, peer_id)) return false;
	result = {peer_id};
	return true;
}

bool Contains(const std::vector<uint16_t>& ids, uint16_t id) {
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool GetPosition(uint16_t peer_id, int& map_id, int& x, int& y) {
	auto& net = NetManager::Instance();
	if (peer_id == net.GetLocalPeerId()) {
		if (!Main_Data::game_player) return false;
		map_id = Game_Map::GetMapId();
		x = Main_Data::game_player->GetX();
		y = Main_Data::game_player->GetY();
		return true;
	}
	if (auto* remote = MultiplayerState::Instance().GetRemotePlayer(peer_id)) {
		map_id = remote->GetMapId();
		x = remote->GetX();
		y = remote->GetY();
		return true;
	}
	return false;
}

void SetLocalGold(int amount) {
	if (!Main_Data::game_party) return;
	amount = std::clamp(amount, 0, 999999);
	const int current = Main_Data::game_party->GetGold();
	if (amount > current) Main_Data::game_party->GainGold(amount - current);
	else if (amount < current) Main_Data::game_party->LoseGold(current - amount);
}

void PlayMusic(const std::string& name) {
	if (name.empty() || !Main_Data::game_system) return;
	lcf::rpg::Music music;
	music.name = name;
	music.fadein = 0;
	music.volume = 100;
	music.tempo = 100;
	music.balance = 50;
	Main_Data::game_system->BgmPlay(music);
}

bool Execute(const ParsedCommand& command, uint16_t issuer, bool from_network) {
	auto& net = NetManager::Instance();
	auto& state = MultiplayerState::Instance();

	if (command.name == "rank") {
		if (!from_network && !net.IsHost()) return false;
		if (command.args.size() != 2) return false;
		uint16_t target = 0;
		if (!ResolvePlayer(command.args[0], issuer, target)) return false;
		const auto rank = Lower(command.args[1]);
		if (rank != "admin" && rank != "player") return false;
		state.SetAdmin(target, rank == "admin");
		return true;
	}

	if (command.name == "spectate") {
		if (command.args.size() != 1 || net.GetLocalPeerId() != issuer) return false;
		uint16_t target = 0;
		if (!ResolvePlayer(command.args[0], issuer, target) || target == net.GetLocalPeerId()) return false;
		state.SpectatePlayer(target);
		return true;
	}

	if (command.name == "noclip") {
		if (command.args.size() > 2) return false;
		std::vector<uint16_t> targets{issuer};
		if (!command.args.empty() && Lower(command.args[0]) == "all") {
			targets = AllPlayers();
		} else if (!command.args.empty()) {
			uint16_t target = 0;
			if (!ResolvePlayer(command.args[0], issuer, target)) return false;
			targets = {target};
		}
		bool enabled = !state.IsNoclipEnabled(targets.front());
		if (command.args.size() == 2) {
			const auto value = Lower(command.args[1]);
			if (value == "on" || value == "1") enabled = true;
			else if (value == "off" || value == "0") enabled = false;
			else return false;
		}
		for (const auto target : targets) state.SetNoclipEnabled(target, enabled);
		return true;
	}

	if (command.name == "music" || command.name == "batbgm") {
		const std::string music_name = Join(command.args, 0);
		if (music_name.empty()) return false;
		if (command.name == "music") {
			PlayMusic(music_name);
		} else if (Main_Data::game_system) {
			lcf::rpg::Music music;
			music.name = music_name;
			music.fadein = 0;
			music.volume = 100;
			music.tempo = 100;
			music.balance = 50;
			Main_Data::game_system->SetSystemBGM(Game_System::BGM_Battle, music);
			if (state.IsInMultiplayerBattle()) PlayMusic(music_name);
		}
		return true;
	}

	if (command.name == "bring") {
		if (command.args.size() != 2) return false;
		std::vector<uint16_t> sources;
		uint16_t target = 0;
		if (!ResolveTargets(command.args[0], issuer, sources) ||
			!ResolvePlayer(command.args[1], issuer, target)) return false;
		int map_id = 0, x = 0, y = 0;
		if (!GetPosition(target, map_id, x, y)) return false;
		if (Contains(sources, net.GetLocalPeerId()) && Main_Data::game_player) {
			Main_Data::game_player->ReserveTeleport(map_id, x, y, -1, TeleportTarget::eParallelTeleport);
		}
		return true;
	}

	if (command.name == "gold" || command.name == "gear") {
		if (command.args.size() != 2) return false;
		std::vector<uint16_t> targets;
		if (!ResolveTargets(command.args[0], issuer, targets)) return false;
		int value = 0;
		if (!ParseInt(command.args[1], value)) return false;
		if (!Contains(targets, net.GetLocalPeerId())) return true;
		if (command.name == "gold") {
			SetLocalGold(value);
			return true;
		}
		const auto* item = lcf::ReaderUtil::GetElement(lcf::Data::items, value);
		if (!item || item->type != lcf::rpg::Item::Type_weapon || !Main_Data::game_party) return false;
		Main_Data::game_party->AddItem(value, 1);
		return true;
	}

	if (command.name == "map") {
		if (command.args.size() != 4) return false;
		std::vector<uint16_t> targets;
		if (!ResolveTargets(command.args[0], issuer, targets)) return false;
		int map_id = 0, x = 0, y = 0;
		if (!ParseInt(command.args[1], map_id) || !ParseInt(command.args[2], x) || !ParseInt(command.args[3], y)) return false;
		if (Contains(targets, net.GetLocalPeerId()) && Main_Data::game_player) {
			Main_Data::game_player->ReserveTeleport(map_id, x, y, -1, TeleportTarget::eParallelTeleport);
		}
		return true;
	}

	if (command.name == "battle") {
		if (command.args.size() != 2) return false;
		std::vector<uint16_t> targets;
		if (!ResolveTargets(command.args[0], issuer, targets)) return false;
		int troop_id = 0;
		if (!ParseInt(command.args[1], troop_id) || !lcf::ReaderUtil::GetElement(lcf::Data::troops, troop_id)) return false;
		// In synchronized modes the host's battle packet is authoritative.
		if (command.args[0] == "all" && state.IsBattleSyncMode() && !net.IsHost()) return true;
		if (Contains(targets, net.GetLocalPeerId())) state.StartCommandBattle(troop_id, true);
		return true;
	}

	return false;
}

bool IsCommandAuthorized(const ParsedCommand& command, uint16_t issuer) {
	auto& net = NetManager::Instance();
	if (!net.IsHost() && command.name == "rank") return false;
	return MultiplayerState::Instance().IsAdmin(issuer);
}

} // namespace

bool MultiplayerCommands::Submit(const std::string& text) {
	if (text.empty() || text[0] != ';') return false;
	ParsedCommand command;
	if (!Parse(text, command)) return true;

	auto& net = NetManager::Instance();
	if (!net.IsConnected()) return true;

	PacketWriter packet(PacketType::MultiplayerCommand);
	packet.write(net.GetLocalPeerId());
	packet.write(text);

	if (net.IsHost()) {
		if (IsCommandAuthorized(command, net.GetLocalPeerId()) && Execute(command, net.GetLocalPeerId(), false)) {
			net.Broadcast(packet, true);
		}
	} else {
		net.SendToServer(packet, true);
	}
	return true;
}

void MultiplayerCommands::HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();
	const uint16_t issuer = reader.readU16();
	const std::string text = reader.readString();
	if (!reader.ok() || text.size() > 256) return;

	ParsedCommand command;
	if (!Parse(text, command)) return;
	auto& net = NetManager::Instance();
	if (net.IsHost()) {
		if (sender_id == 0 || sender_id != issuer || !IsCommandAuthorized(command, sender_id)) return;
		if (!Execute(command, issuer, true)) return;
		PacketWriter accepted(PacketType::MultiplayerCommand);
		accepted.write(issuer);
		accepted.write(text);
		net.Broadcast(accepted, true);
	} else {
		Execute(command, issuer, true);
	}
}

} // namespace Chaos
