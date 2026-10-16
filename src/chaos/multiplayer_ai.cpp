/*
 * Chaos Fork: Fake players ("lambda players") with Markov-chain chat.
 */

#include "chaos/multiplayer_ai.h"
#include "chaos/game_multiplayer.h"
#include "chaos/multiplayer_chat.h"
#include "chaos/multiplayer_state.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "filefinder.h"
#include "game_character.h"
#include "game_map.h"
#include "game_player.h"
#include "main_data.h"
#include "output.h"

#include <algorithm>
#include <climits>
#include <sstream>

namespace Chaos {

namespace {

constexpr size_t kMaxFakePlayers = 16;
constexpr size_t kMaxChatChars = 80; // chat packets reject longer messages
constexpr int kRosterKeepaliveTicks = 600;

const char* kFakeNames[] = {
	"Lambda", "Gordon", "Barney", "Alyx", "G-Man", "Kleiner", "Breen",
	"Nihilanth", "Vorty", "Houndeye", "Bullsquid", "Headcrab", "Zombie",
	"Grunt", "Adrian", "Shephard", "Gina", "Colette", "Rosenberg", "Odessa",
	"Otis", "Izzy", "Magnusson", "Eli", "Calhoun", "Walter", "Sheckley",
	"Laszlo", "Griggs", "One-Eye",
};
constexpr size_t kFakeNameCount = sizeof(kFakeNames) / sizeof(kFakeNames[0]);

const char* kFallbackLines[] = {
	"anyone here",
	"ez clap",
	"skill issue",
	"follow me I know the way",
	"who wants to trade a potion",
	"I am stuck in the wall help",
	"gg gg",
	"no u",
};
constexpr size_t kFallbackLineCount = sizeof(kFallbackLines) / sizeof(kFallbackLines[0]);

std::vector<std::string> Tokenize(const std::string& line) {
	std::vector<std::string> tokens;
	std::istringstream stream(line);
	for (std::string word; stream >> word;) tokens.push_back(std::move(word));
	return tokens;
}

} // namespace

MultiplayerAi& MultiplayerAi::Instance() {
	static MultiplayerAi instance;
	return instance;
}

int MultiplayerAi::Rand(int min, int max) {
	if (min >= max) return min;
	std::uniform_int_distribution<int> dist(min, max);
	return dist(rng);
}

void MultiplayerAi::Spawn(int count) {
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;

	for (int i = 0; i < count && players.size() < kMaxFakePlayers; ++i) {
		FakePlayer p;
		p.peer_id = next_fake_id++;
		p.name = kFakeNames[name_counter++ % kFakeNameCount];
		p.map_id = Game_Map::GetMapId();
		p.x = Main_Data::game_player ? Main_Data::game_player->GetX() : 0;
		p.y = Main_Data::game_player ? Main_Data::game_player->GetY() : 0;
		p.direction = 2;
		p.facing = 2;
		p.last_dir = 2;
		if (Main_Data::game_player) {
			p.sprite_name = Main_Data::game_player->GetSpriteName();
			p.sprite_index = Main_Data::game_player->GetSpriteIndex();
		}
		if (p.sprite_name.empty()) {
			p.sprite_name = "Chara1";
			p.sprite_index = 0;
		}
		p.move_timer = Rand(10, 60);
		p.chat_timer = Rand(300, 1500);
		p.keepalive_timer = 0;

		auto id = p.peer_id;
		auto name = p.name;
		players.emplace(id, p);
		ApplyLocal(players[id]);
		Output::Debug("MultiplayerAi: spawned fake player {} ({})", name, id);
	}

	BroadcastRoster();
}

void MultiplayerAi::Clear() {
	if (players.empty()) return;
	for (auto& [id, p] : players) {
		MultiplayerState::Instance().RemoveRemotePlayer(id);
	}
	players.clear();
	next_fake_id = 0xF000;
	if (NetManager::Instance().IsHost()) {
		BroadcastRoster();
	}
}

void MultiplayerAi::Reset() {
	players.clear();
	next_fake_id = 0xF000;
	name_counter = 0;
	smart_mode = false;
}

std::string MultiplayerAi::GetName(uint16_t peer_id) const {
	auto it = players.find(peer_id);
	return it != players.end() ? it->second.name : std::string();
}

bool MultiplayerAi::IsFakePeer(uint16_t peer_id) const {
	return players.count(peer_id) != 0;
}

void MultiplayerAi::Update() {
	auto& net = NetManager::Instance();
	if (!net.IsHost() || players.empty()) return;
	if (!Main_Data::game_player) return;

	for (auto& [id, p] : players) {
		if (--p.chat_timer <= 0) {
			p.chat_timer = Rand(600, 2100);
			Say(p);
		}
		if (--p.move_timer <= 0) {
			p.move_timer = smart_mode ? Rand(6, 16) : Rand(15, 60);
			TickMovement(p);
		}
		if (++p.keepalive_timer >= 90) {
			p.keepalive_timer = 0;
			SendPosition(p);
		}
	}
}

void MultiplayerAi::TickMovement(FakePlayer& p) {
	// Fake players always live on the host's current map
	if (p.map_id != Game_Map::GetMapId()) {
		TeleportToHost(p);
		return;
	}

	if (smart_mode) {
		// Walk toward the nearest real player on this map
		int best_dx = 0, best_dy = 0, best_dist = INT_MAX;
		bool found = false;
		auto consider = [&](int x, int y) {
			int dx = x - p.x, dy = y - p.y;
			int dist = std::abs(dx) + std::abs(dy);
			if (dist < best_dist) {
				best_dist = dist;
				best_dx = dx;
				best_dy = dy;
				found = true;
			}
		};
		if (Main_Data::game_player) {
			consider(Main_Data::game_player->GetX(), Main_Data::game_player->GetY());
		}
		for (const auto& [id, rp] : MultiplayerState::Instance().GetRemotePlayers()) {
			if (IsFakePeer(id) || !rp) continue;
			if (rp->GetMapId() != p.map_id) continue;
			consider(rp->GetX(), rp->GetY());
		}
		if (!found || best_dist <= 1) return; // nobody around, or already close

		// Try directions that reduce the distance first
		std::vector<int> dirs;
		if (std::abs(best_dx) >= std::abs(best_dy)) {
			if (best_dx > 0) dirs.push_back(Game_Character::Right);
			if (best_dx < 0) dirs.push_back(Game_Character::Left);
			if (best_dy > 0) dirs.push_back(Game_Character::Down);
			if (best_dy < 0) dirs.push_back(Game_Character::Up);
		} else {
			if (best_dy > 0) dirs.push_back(Game_Character::Down);
			if (best_dy < 0) dirs.push_back(Game_Character::Up);
			if (best_dx > 0) dirs.push_back(Game_Character::Right);
			if (best_dx < 0) dirs.push_back(Game_Character::Left);
		}
		// Fill with the remaining directions as fallbacks
		for (int dir : {Game_Character::Down, Game_Character::Left, Game_Character::Right, Game_Character::Up}) {
			if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) dirs.push_back(dir);
		}
		for (int dir : dirs) {
			if (TryMove(p, dir)) return;
		}
		return;
	}

	// Random wander: sometimes idle, mostly keep walking the same way
	if (Rand(0, 2) == 0) return;
	int dir = (Rand(0, 2) != 0) ? p.last_dir
		: std::vector<int>{Game_Character::Down, Game_Character::Left, Game_Character::Right, Game_Character::Up}[Rand(0, 3)];
	if (TryMove(p, dir)) return;
	for (int i = 0; i < 3; ++i) {
		dir = std::vector<int>{Game_Character::Down, Game_Character::Left, Game_Character::Right, Game_Character::Up}[Rand(0, 3)];
		if (TryMove(p, dir)) return;
	}
}

bool MultiplayerAi::TryMove(FakePlayer& p, int direction) {
	int to_x = p.x, to_y = p.y;
	switch (direction) {
		case Game_Character::Up: to_y -= 1; break;
		case Game_Character::Down: to_y += 1; break;
		case Game_Character::Left: to_x -= 1; break;
		case Game_Character::Right: to_x += 1; break;
		default: return false;
	}
	if (!Game_Map::IsValid(to_x, to_y)) return false;

	const Game_Character* self = MultiplayerState::Instance().GetRemotePlayer(p.peer_id);
	if (!self && Main_Data::game_player) self = Main_Data::game_player.get();
	if (!self) return false;
	if (!Game_Map::CheckWay(*self, p.x, p.y, to_x, to_y)) return false;

	p.x = to_x;
	p.y = to_y;
	p.direction = direction;
	p.facing = direction;
	p.last_dir = direction;
	SendPosition(p);
	return true;
}

void MultiplayerAi::TeleportToHost(FakePlayer& p) {
	p.map_id = Game_Map::GetMapId();
	if (Main_Data::game_player) {
		p.x = Main_Data::game_player->GetX();
		p.y = Main_Data::game_player->GetY();
	}
	SendPosition(p);
}

void MultiplayerAi::SendPosition(const FakePlayer& p) {
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;

	PacketWriter packet(PacketType::PlayerPosition);
	packet.write(p.peer_id);
	packet.write(static_cast<int32_t>(p.map_id));
	packet.write(static_cast<int32_t>(p.x));
	packet.write(static_cast<int32_t>(p.y));
	packet.write(static_cast<int32_t>(p.direction));
	packet.write(static_cast<int32_t>(p.facing));
	packet.write(p.sprite_name);
	packet.write(static_cast<int32_t>(p.sprite_index));
	net.Broadcast(packet, false); // unreliable like real player positions

	ApplyLocal(p);
}

void MultiplayerAi::ApplyLocal(const FakePlayer& p) {
	auto* rp = MultiplayerState::Instance().GetOrCreateRemotePlayer(p.peer_id, p.name);
	if (!rp) return;
	rp->SetCharacterSprite(p.sprite_name, p.sprite_index);
	rp->SetNetworkPosition(p.map_id, p.x, p.y, p.direction, p.facing);
}

void MultiplayerAi::Say(FakePlayer& p) {
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;

	std::string text = markov.Generate();
	if (text.size() > kMaxChatChars) text.resize(kMaxChatChars);
	if (text.empty()) return;

	PacketWriter packet(PacketType::ChatMessage);
	packet.write(p.peer_id);
	packet.write(static_cast<uint8_t>(0)); // normal chat
	packet.write(text);
	net.Broadcast(packet, true);

	MultiplayerChat::Instance().OnChatMessageReceived(p.peer_id, p.name, text, false);
}

void MultiplayerAi::BroadcastRoster() const {
	auto& net = NetManager::Instance();
	if (!net.IsHost()) return;
	PacketWriter packet(PacketType::AiSync);
	packet.write(static_cast<uint16_t>(players.size()));
	for (const auto& [id, p] : players) {
		packet.write(id);
		packet.write(p.name);
	}
	net.Broadcast(packet, true);
}

void MultiplayerAi::SendRosterTo(uint16_t peer_id) const {
	auto& net = NetManager::Instance();
	if (!net.IsHost() || players.empty()) return;
	PacketWriter packet(PacketType::AiSync);
	packet.write(static_cast<uint16_t>(players.size()));
	for (const auto& [id, p] : players) {
		packet.write(id);
		packet.write(p.name);
	}
	net.SendTo(peer_id, packet, true);
}

void MultiplayerAi::HandlePacket(const uint8_t* data, size_t len) {
	// Roster is host-authoritative; the host never applies remote AiSync packets
	if (NetManager::Instance().IsHost()) return;

	PacketReader reader(data, len);
	reader.readType();
	const uint16_t count = reader.readU16();
	std::map<uint16_t, std::string> roster;
	for (uint16_t i = 0; i < count && reader.ok(); ++i) {
		const uint16_t id = reader.readU16();
		const std::string name = reader.readString();
		if (!reader.ok() || name.empty()) break;
		roster[id] = name;
	}
	if (!reader.ok()) return;

	// Remove fake players that dropped off the roster
	for (auto it = players.begin(); it != players.end();) {
		if (roster.count(it->first) == 0) {
			MultiplayerState::Instance().RemoveRemotePlayer(it->first);
			it = players.erase(it);
		} else {
			++it;
		}
	}

	for (auto& [id, name] : roster) {
		auto it = players.find(id);
		if (it == players.end()) {
			FakePlayer p;
			p.peer_id = id;
			p.name = name;
			players.emplace(id, p);
		}
		// Heal the display name if a position packet created the remote player
		// before this roster arrived
		if (auto* rp = MultiplayerState::Instance().GetRemotePlayer(id)) {
			if (rp->GetPlayerName() != name) rp->SetPlayerName(name);
		}
	}
}

std::string MultiplayerAi::Markov::Generate() {
	EnsureLoaded();
	if (start_words.empty()) return {};

	const std::string* start = &start_words[rng() % start_words.size()];
	std::string result = *start;
	std::string word = *start;
	for (int i = 0; i < 14; ++i) {
		auto it = next_words.find(word);
		if (it == next_words.end() || it->second.empty()) break;
		const std::string& next = it->second[rng() % it->second.size()];
		if (result.size() + next.size() + 1 > kMaxChatChars) break;
		result += ' ';
		result += next;
		word = next;
	}
	return result;
}

void MultiplayerAi::Markov::EnsureLoaded() {
	if (loaded) return;
	loaded = true;

	auto fs = FileFinder::ChaosAssets();
	std::vector<std::string> lines;
	if (fs) {
		auto in = fs.OpenInputStream("AILINES.txt", std::ios::in);
		if (in) {
			for (std::string line; std::getline(in, line);) {
				while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
				if (!line.empty()) lines.push_back(line);
			}
		}
	}
	if (lines.empty()) {
		for (size_t i = 0; i < kFallbackLineCount; ++i) lines.push_back(kFallbackLines[i]);
		Output::Debug("MultiplayerAi: AILINES.txt not found, using fallback lines");
	} else {
		Output::Debug("MultiplayerAi: loaded {} lines from AILINES.txt", lines.size());
	}

	for (const auto& line : lines) {
		auto tokens = Tokenize(line);
		if (tokens.empty()) continue;
		start_words.push_back(tokens.front());
		for (size_t i = 0; i + 1 < tokens.size(); ++i) {
			next_words[tokens[i]].push_back(tokens[i + 1]);
		}
	}
}

} // namespace Chaos
