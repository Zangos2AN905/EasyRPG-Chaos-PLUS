/*
 * Chaos Fork: Custom Mode
 * Host-built mix of two game modes with objectives and chaos toggles.
 */

#include "chaos/custom_mode.h"
#include "chaos/multiplayer_state.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "cache.h"
#include "drawable.h"
#include "filefinder.h"
#include "game_map.h"
#include "game_party.h"
#include "game_player.h"
#include "game_screen.h"
#include "game_system.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "rand.h"
#include "scene.h"
#include "scene_map.h"
#include "spriteset_map.h"
#include "window_help.h"
#include <lcf/data.h>
#include <algorithm>
#include <fmt/format.h>

namespace Chaos {

static constexpr int TIMED_OBJECTIVE_FRAMES = 8 * 60 * DEFAULT_FPS;
static constexpr int EFFECT_DURATION = 5 * DEFAULT_FPS;
static constexpr int EVENT_MIN_INTERVAL = 5 * DEFAULT_FPS;
static constexpr int EVENT_MAX_INTERVAL = 10 * DEFAULT_FPS;
static constexpr const char* kForcedSkinCacheName = "__forced_skin";

const char* GetChaosEventName(ChaosEventType ev) {
	switch (ev) {
		case ChaosEventType::ReversedControls: return "Reversed Controls!";
		case ChaosEventType::SlowMotion: return "Slow Motion!";
		case ChaosEventType::ScreenShake: return "Earthquake!";
		case ChaosEventType::ScreenFlash: return "Flashbang!";
		case ChaosEventType::Darkness: return "Lights Out!";
		case ChaosEventType::RandomMusic: return "DJ Chaos!";
		case ChaosEventType::RandomChipset: return "Reality Shift!";
		case ChaosEventType::RandomTeleport: return "Random Teleport!";
		case ChaosEventType::RandomMapTeleport: return "Wrong Warp!";
		case ChaosEventType::SwapPositions: return "Body Swap!";
		case ChaosEventType::FullHeal: return "Blessing!";
		case ChaosEventType::GoldRush: return "Gold Rush!";
		case ChaosEventType::SpinCycle: return "Spin Cycle!";
		case ChaosEventType::SpeedUp: return "Gotta Go Fast!";
		case ChaosEventType::Silence: return "Silence...";
		default: return "???";
	}
}

CustomMode& CustomMode::Instance() {
	static CustomMode instance;
	return instance;
}

bool CustomMode::IsActive() const {
	return started && NetManager::Instance().GetMode() == MultiplayerMode::Custom;
}

bool CustomMode::IsModeInMix(MultiplayerMode m) const {
	int v = static_cast<int>(m);
	return settings.mode_a == v || settings.mode_b == v;
}

ModeProperties CustomMode::GetEffectiveProperties() const {
	auto a = GetModeProperties(static_cast<MultiplayerMode>(settings.mode_a));
	auto b = GetModeProperties(static_cast<MultiplayerMode>(settings.mode_b));
	ModeProperties merged = a;
	merged.sync_switches = a.sync_switches || b.sync_switches;
	merged.sync_variables = a.sync_variables || b.sync_variables;
	merged.sync_actor_states = a.sync_actor_states || b.sync_actor_states;
	merged.proximity_required = a.proximity_required || b.proximity_required;
	merged.has_god_player = a.has_god_player || b.has_god_player;
	if (a.max_players == 0 || b.max_players == 0) {
		merged.max_players = 0;
	} else {
		merged.max_players = std::max(a.max_players, b.max_players);
	}
	return merged;
}

void CustomMode::SetPendingSettings(CustomModeSettings s) {
	settings = std::move(s);
	has_pending = true;
}

CustomModeSettings CustomMode::TakePendingSettings() {
	has_pending = false;
	return settings;
}

void CustomMode::Start() {
	started = true;
	random_start_done = false;
	objective_announced_end = false;
	event_timer = Rand::GetRandomNumber(EVENT_MIN_INTERVAL, EVENT_MAX_INTERVAL);
	reversed_timer = 0;
	slow_timer = 0;
	fast_timer = 0;
	dark_timer = 0;
	spin_timer = 0;
	silence_timer = 0;
	last_speed_adjust = 0;

	objective_frames_left = -1;
	if (static_cast<CustomObjective>(settings.objective) == CustomObjective::Timed) {
		objective_frames_left = TIMED_OBJECTIVE_FRAMES;
	}

	auto& net = NetManager::Instance();
	if (net.IsHost()) {
		BroadcastSettings();
	}
	ApplyForcedSkin();
	ShowBanner(std::string("Objective: ") + GetObjectiveName(static_cast<CustomObjective>(settings.objective)));
}

void CustomMode::Stop() {
	started = false;
	if (force_skin_registered) {
		Cache::UnregisterCharset(kForcedSkinCacheName);
		force_skin_registered = false;
	}
	objective_window.reset();
	banner_window.reset();
	objective_frames_left = -1;
	has_saved_bgm = false;
}

void CustomMode::Update() {
	if (!IsActive()) return;
	auto& net = NetManager::Instance();

	if (objective_frames_left > 0) {
		--objective_frames_left;
		if (objective_frames_left == 0 && !objective_announced_end) {
			objective_announced_end = true;
			ShowBanner("TIME'S UP!");
			if (Main_Data::game_screen) {
				Main_Data::game_screen->FlashOnce(255, 0, 0, 31, DEFAULT_FPS / 2);
			}
		}
	}

	if (reversed_timer > 0) --reversed_timer;
	if (slow_timer > 0) --slow_timer;
	if (fast_timer > 0) --fast_timer;
	if (spin_timer > 0) --spin_timer;

	if (dark_timer > 0) {
		--dark_timer;
		if (dark_timer == 0 && Main_Data::game_screen) {
			Main_Data::game_screen->TintScreen(100, 100, 100, 100, 10);
		}
	}

	if (silence_timer > 0) {
		--silence_timer;
		if (silence_timer == 0 && Main_Data::game_system) {
			if (has_saved_bgm) {
				Main_Data::game_system->BgmPlay(saved_bgm);
			}
			has_saved_bgm = false;
		}
	}

	if (spin_timer > 0 && Main_Data::game_player) {
		spin_counter++;
		if (spin_counter >= 6) {
			spin_counter = 0;
			int dir = Main_Data::game_player->GetDirection();
			// Rotate clockwise: down -> left -> up -> right
			int next = (dir == 2) ? 4 : (dir == 4) ? 8 : (dir == 8) ? 6 : 2;
			Main_Data::game_player->SetDirection(next);
			Main_Data::game_player->SetFacing(next);
		}
	}

	ApplySpeedAdjust();

	if (net.IsHost() && settings.random_events && !MultiplayerState::Instance().IsInMultiplayerBattle()) {
		if (event_timer > 0) {
			--event_timer;
		} else {
			TriggerRandomEvent();
			event_timer = Rand::GetRandomNumber(EVENT_MIN_INTERVAL, EVENT_MAX_INTERVAL);
		}
	}

	if (banner_timer > 0) {
		--banner_timer;
		if (banner_timer == 0 && banner_window) {
			banner_window->SetVisible(false);
		}
	}

	UpdateObjectiveWindow();
}

void CustomMode::OnMapLoaded() {
	if (!IsActive()) return;
	ApplyForcedSkin();
	ApplySpeedAdjust();

	auto& net = NetManager::Instance();
	if (net.IsHost() && settings.random_start && !random_start_done) {
		random_start_done = true;
		const auto& map = Game_Map::GetMap();
		int x = Rand::GetRandomNumber(0, std::max(0, map.width - 1));
		int y = Rand::GetRandomNumber(0, std::max(0, map.height - 1));
		std::vector<int32_t> args = { static_cast<int32_t>(Game_Map::GetMapId()), x, y };
		PacketWriter pw(PacketType::ChaosEvent);
		pw.write(static_cast<uint8_t>(ChaosEventType::RandomTeleport));
		pw.write(static_cast<uint8_t>(args.size()));
		for (auto v : args) pw.write(v);
		pw.write(std::string(""));
		net.Broadcast(pw, true);
		ApplyEvent(ChaosEventType::RandomTeleport, args, "");
		ShowBanner(GetChaosEventName(ChaosEventType::RandomTeleport));
	}
}

void CustomMode::BroadcastSettings() {
	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::CustomModeSettings);
	pw.write(settings.mode_a);
	pw.write(settings.mode_b);
	pw.write(settings.objective);
	uint8_t flags = 0;
	if (settings.random_events) flags |= 1;
	if (settings.force_skin) flags |= 2;
	if (settings.turbo_movement) flags |= 4;
	if (settings.random_start) flags |= 8;
	pw.write(flags);
	if (settings.force_skin) {
		pw.write(settings.force_skin_charset);
		pw.write(static_cast<int32_t>(settings.force_skin_index));
		pw.write(static_cast<int32_t>(settings.force_skin_data.size()));
		pw.writeBytes(settings.force_skin_data.data(), settings.force_skin_data.size());
	}
	net.Broadcast(pw, true);
}

void CustomMode::SendSettingsTo(uint16_t peer_id) {
	auto& net = NetManager::Instance();
	PacketWriter pw(PacketType::CustomModeSettings);
	pw.write(settings.mode_a);
	pw.write(settings.mode_b);
	pw.write(settings.objective);
	uint8_t flags = 0;
	if (settings.random_events) flags |= 1;
	if (settings.force_skin) flags |= 2;
	if (settings.turbo_movement) flags |= 4;
	if (settings.random_start) flags |= 8;
	pw.write(flags);
	if (settings.force_skin) {
		pw.write(settings.force_skin_charset);
		pw.write(static_cast<int32_t>(settings.force_skin_index));
		pw.write(static_cast<int32_t>(settings.force_skin_data.size()));
		pw.writeBytes(settings.force_skin_data.data(), settings.force_skin_data.size());
	}
	net.SendTo(peer_id, pw, true);
}

void CustomMode::HandleSettingsPacket(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();

	CustomModeSettings s;
	s.mode_a = reader.readU8();
	s.mode_b = reader.readU8();
	s.objective = reader.readU8();
	uint8_t flags = reader.readU8();
	s.random_events = (flags & 1) != 0;
	s.force_skin = (flags & 2) != 0;
	s.turbo_movement = (flags & 4) != 0;
	s.random_start = (flags & 8) != 0;
	if (s.force_skin) {
		s.force_skin_charset = reader.readString();
		s.force_skin_index = reader.readI32();
		int32_t data_size = reader.readI32();
		if (data_size > 0 && data_size <= 1024 * 1024) {
			const uint8_t* img = reader.readBytes(static_cast<size_t>(data_size));
			if (img) {
				s.force_skin_data.assign(img, img + data_size);
			}
		}
	}
	if (!reader.ok()) return;

	settings = std::move(s);
	started = true;
	random_start_done = false;
	objective_announced_end = false;
	event_timer = Rand::GetRandomNumber(EVENT_MIN_INTERVAL, EVENT_MAX_INTERVAL);

	objective_frames_left = -1;
	if (static_cast<CustomObjective>(settings.objective) == CustomObjective::Timed) {
		objective_frames_left = TIMED_OBJECTIVE_FRAMES;
	}

	ApplyForcedSkin();
	ShowBanner(std::string("Objective: ") + GetObjectiveName(static_cast<CustomObjective>(settings.objective)));
	Output::Debug("CustomMode: Settings received (mix {}+{})", settings.mode_a, settings.mode_b);
}

void CustomMode::HandleEventPacket(const uint8_t* data, size_t len) {
	PacketReader reader(data, len);
	reader.readType();
	auto ev = static_cast<ChaosEventType>(reader.readU8());
	uint8_t arg_count = reader.readU8();
	std::vector<int32_t> args;
	for (int i = 0; i < arg_count; ++i) {
		args.push_back(reader.readI32());
	}
	std::string str_arg = reader.readString();
	if (!reader.ok()) return;
	if (ev >= ChaosEventType::Count) return;
	ApplyEvent(ev, args, str_arg);
	ShowBanner(GetChaosEventName(ev));
}

void CustomMode::TriggerRandomEvent() {
	auto& net = NetManager::Instance();
	auto ev = static_cast<ChaosEventType>(Rand::GetRandomNumber(0, static_cast<int32_t>(ChaosEventType::Count) - 1));

	std::vector<int32_t> args;
	std::string str_arg;

	switch (ev) {
		case ChaosEventType::RandomMusic: {
			std::vector<std::string> tracks;
			auto fs = FileFinder::Game();
			auto* dir = fs.ListDirectory("Music");
			if (dir) {
				for (auto& [key, entry] : *dir) {
					if (entry.type != DirectoryTree::FileType::Regular) continue;
					std::string name = entry.name;
					auto dot = name.rfind('.');
					if (dot != std::string::npos) name = name.substr(0, dot);
					tracks.push_back(name);
				}
			}
			if (tracks.empty()) {
				ev = ChaosEventType::ScreenFlash;
			} else {
				str_arg = tracks[Rand::GetRandomNumber(0, static_cast<int32_t>(tracks.size()) - 1)];
			}
			break;
		}
		case ChaosEventType::RandomChipset: {
			std::vector<int32_t> chipsets;
			for (const auto& cs : lcf::Data::chipsets) {
				if (cs.ID != Game_Map::GetChipset()) {
					chipsets.push_back(cs.ID);
				}
			}
			if (chipsets.empty()) {
				ev = ChaosEventType::ScreenFlash;
			} else {
				args.push_back(chipsets[Rand::GetRandomNumber(0, static_cast<int32_t>(chipsets.size()) - 1)]);
			}
			break;
		}
		case ChaosEventType::RandomTeleport: {
			const auto& map = Game_Map::GetMap();
			args.push_back(static_cast<int32_t>(Game_Map::GetMapId()));
			args.push_back(Rand::GetRandomNumber(0, std::max(0, map.width - 1)));
			args.push_back(Rand::GetRandomNumber(0, std::max(0, map.height - 1)));
			break;
		}
		case ChaosEventType::RandomMapTeleport: {
			std::vector<int> map_ids;
			for (const auto& m : lcf::Data::treemap.maps) {
				if (m.ID > 0 && m.ID != Game_Map::GetMapId() &&
					m.type == lcf::rpg::TreeMap::MapType_map) {
					map_ids.push_back(m.ID);
				}
			}
			if (map_ids.empty()) {
				ev = ChaosEventType::RandomTeleport;
				const auto& map = Game_Map::GetMap();
				args.push_back(static_cast<int32_t>(Game_Map::GetMapId()));
				args.push_back(Rand::GetRandomNumber(0, std::max(0, map.width - 1)));
				args.push_back(Rand::GetRandomNumber(0, std::max(0, map.height - 1)));
			} else {
				int map_id = map_ids[Rand::GetRandomNumber(0, static_cast<int32_t>(map_ids.size()) - 1)];
				int x = 1, y = 1;
				if (auto map = Game_Map::LoadMapFile(map_id)) {
					x = std::max(0, map->width / 2);
					y = std::max(0, map->height / 2);
				}
				args.push_back(map_id);
				args.push_back(x);
				args.push_back(y);
			}
			break;
		}
		case ChaosEventType::SwapPositions: {
			struct Candidate { uint16_t id; int32_t map, x, y; };
			std::vector<Candidate> candidates;
			if (Main_Data::game_player) {
				candidates.push_back({ net.GetLocalPeerId(),
					static_cast<int32_t>(Game_Map::GetMapId()),
					Main_Data::game_player->GetX(), Main_Data::game_player->GetY() });
			}
			for (auto& [id, rp] : MultiplayerState::Instance().GetRemotePlayers()) {
				if (rp && rp->IsOnCurrentMap()) {
					candidates.push_back({ id, rp->GetMapId(), rp->GetX(), rp->GetY() });
				}
			}
			if (candidates.size() < 2) {
				ev = ChaosEventType::ScreenShake;
				break;
			}
			int a = Rand::GetRandomNumber(0, static_cast<int32_t>(candidates.size()) - 1);
			int b = a;
			while (b == a) {
				b = Rand::GetRandomNumber(0, static_cast<int32_t>(candidates.size()) - 1);
			}
			args.push_back(candidates[a].id);
			args.push_back(candidates[a].map);
			args.push_back(candidates[a].x);
			args.push_back(candidates[a].y);
			args.push_back(candidates[b].id);
			args.push_back(candidates[b].map);
			args.push_back(candidates[b].x);
			args.push_back(candidates[b].y);
			break;
		}
		case ChaosEventType::GoldRush:
			args.push_back(Rand::GetRandomNumber(500, 2000));
			break;
		default:
			break;
	}

	PacketWriter pw(PacketType::ChaosEvent);
	pw.write(static_cast<uint8_t>(ev));
	pw.write(static_cast<uint8_t>(args.size()));
	for (auto v : args) pw.write(v);
	pw.write(str_arg);
	net.Broadcast(pw, true);
	ApplyEvent(ev, args, str_arg);
	ShowBanner(GetChaosEventName(ev));
}

void CustomMode::ApplyEvent(ChaosEventType ev, const std::vector<int32_t>& args, const std::string& str_arg) {
	auto& net = NetManager::Instance();

	switch (ev) {
		case ChaosEventType::ReversedControls:
			reversed_timer = EFFECT_DURATION;
			break;
		case ChaosEventType::SlowMotion:
			slow_timer = EFFECT_DURATION;
			break;
		case ChaosEventType::ScreenShake:
			if (Main_Data::game_screen) {
				Main_Data::game_screen->ShakeOnce(8, 10, 30);
			}
			break;
		case ChaosEventType::ScreenFlash:
			if (Main_Data::game_screen) {
				Main_Data::game_screen->FlashOnce(255, 255, 255, 20, 15);
			}
			break;
		case ChaosEventType::Darkness:
			if (Main_Data::game_screen) {
				Main_Data::game_screen->TintScreen(30, 30, 30, 100, 10);
			}
			dark_timer = EFFECT_DURATION;
			break;
		case ChaosEventType::RandomMusic: {
			if (!str_arg.empty() && Main_Data::game_system) {
				lcf::rpg::Music music;
				music.name = str_arg;
				music.fadein = 0;
				music.volume = 100;
				music.tempo = 100;
				music.balance = 50;
				Main_Data::game_system->BgmPlay(music);
			}
			break;
		}
		case ChaosEventType::RandomChipset: {
			if (!args.empty() && args[0] != Game_Map::GetChipset()) {
				Game_Map::SetChipset(args[0]);
				Scene_Map* scene = static_cast<Scene_Map*>(Scene::Find(Scene::Map).get());
				if (scene && scene->spriteset) {
					scene->spriteset->ChipsetUpdated();
				}
			}
			break;
		}
		case ChaosEventType::RandomTeleport:
		case ChaosEventType::RandomMapTeleport:
			if (args.size() >= 3 && Main_Data::game_player) {
				Main_Data::game_player->ReserveTeleport(args[0], args[1], args[2], -1,
					TeleportTarget::eParallelTeleport);
			}
			break;
		case ChaosEventType::SwapPositions: {
			if (args.size() < 8 || !Main_Data::game_player) break;
			uint16_t local = net.GetLocalPeerId();
			for (int i = 0; i < 2; ++i) {
				uint16_t self_id = args[i * 4];
				uint16_t other_id = args[(1 - i) * 4];
				int32_t other_map = args[(1 - i) * 4 + 1];
				int32_t other_x = args[(1 - i) * 4 + 2];
				int32_t other_y = args[(1 - i) * 4 + 3];
				if (self_id == local && other_id != local) {
					Main_Data::game_player->ReserveTeleport(other_map, other_x, other_y, -1,
						TeleportTarget::eParallelTeleport);
				}
			}
			break;
		}
		case ChaosEventType::FullHeal:
			if (Main_Data::game_party) {
				for (auto* actor : Main_Data::game_party->GetActors()) {
					if (actor) actor->FullHeal();
				}
			}
			break;
		case ChaosEventType::GoldRush:
			if (Main_Data::game_party && !args.empty()) {
				Main_Data::game_party->GainGold(args[0]);
			}
			break;
		case ChaosEventType::SpinCycle:
			spin_timer = DEFAULT_FPS * 3;
			spin_counter = 0;
			break;
		case ChaosEventType::SpeedUp:
			fast_timer = EFFECT_DURATION;
			break;
		case ChaosEventType::Silence:
			if (Main_Data::game_system) {
				saved_bgm = Main_Data::game_system->GetCurrentBGM();
				has_saved_bgm = true;
				Main_Data::game_system->BgmStop();
			}
			silence_timer = EFFECT_DURATION;
			break;
		default:
			break;
	}
}

void CustomMode::ApplyForcedSkin() {
	if (!settings.force_skin || settings.force_skin_data.empty()) return;

	BitmapRef bmp = Bitmap::Create(settings.force_skin_data.data(),
		static_cast<unsigned>(settings.force_skin_data.size()), true);
	if (!bmp) return;

	Cache::RegisterCharset(kForcedSkinCacheName, bmp);
	force_skin_registered = true;

	if (Main_Data::game_player) {
		Main_Data::game_player->SetSpriteGraphic(kForcedSkinCacheName, settings.force_skin_index);
	}
}

const std::string& CustomMode::GetForcedSkinName() const {
	static const std::string name = kForcedSkinCacheName;
	return name;
}

int CustomMode::ReverseDir4(int dir4) const {
	if (reversed_timer <= 0) return dir4;
	switch (dir4) {
		case 2: return 8;
		case 8: return 2;
		case 4: return 6;
		case 6: return 4;
		default: return dir4;
	}
}

void CustomMode::ApplySpeedAdjust() {
	if (!Main_Data::game_player) return;
	int adjust = 0;
	if (settings.turbo_movement) adjust += 1;
	if (fast_timer > 0) adjust += 2;
	if (slow_timer > 0) adjust -= 2;
	if (adjust == last_speed_adjust) return;

	auto* player = Main_Data::game_player.get();
	int new_speed = player->GetMoveSpeed() - last_speed_adjust + adjust;
	player->SetMoveSpeed(std::max(1, std::min(6, new_speed)));
	last_speed_adjust = adjust;
}

void CustomMode::ShowBanner(const std::string& text) {
	if (!banner_window) {
		banner_window = std::make_unique<Window_Help>(
			Player::screen_width / 4, Player::screen_height / 2 - 16,
			Player::screen_width / 2, 32);
		banner_window->SetZ(Priority_Window + 210);
	}
	banner_window->SetText(text);
	banner_window->SetVisible(true);
	banner_timer = DEFAULT_FPS * 5 / 2;
}

void CustomMode::UpdateObjectiveWindow() {
	// Only the Timed objective has a visible countdown. "Beat the game"
	// has no timer, so it shows nothing persistent (the banner at game
	// start already announces it).
	bool needs_window = objective_frames_left >= 0;
	if (!needs_window) {
		if (objective_window) objective_window->SetVisible(false);
		return;
	}

	if (!objective_window) {
		objective_window = std::make_unique<Window_Help>(
			Player::screen_width - 136, 0, 136, 32);
		objective_window->SetZ(Priority_Window + 200);
	}

	if (objective_frames_left > 0) {
		int total_seconds = objective_frames_left / DEFAULT_FPS;
		int minutes = total_seconds / 60;
		int seconds = total_seconds % 60;
		objective_window->SetText(fmt::format("Time: {}:{:02d}", minutes, seconds));
		objective_window->SetVisible(true);
	} else { // == 0, time expired
		objective_window->SetText("Time: 0:00");
		objective_window->SetVisible(true);
	}
}

} // namespace Chaos
