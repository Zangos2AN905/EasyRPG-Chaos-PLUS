/*
 * Chaos Fork: Rawberry - Horror Mode Enemy Implementation
 */

#include "chaos/game_rawberry.h"
#include "chaos/multiplayer_state.h"
#include "game_map.h"
#include "game_player.h"
#include "main_data.h"
#include "game_system.h"
#include "audio.h"
#include "filefinder.h"
#include "rand.h"
#include "output.h"
#include <lcf/rpg/sound.h>
#include <cmath>
#include <cstdlib>

namespace Chaos {

Game_Rawberry::Game_Rawberry()
	: Game_CharacterDataStorage(Game_Character::Event)
{
	SetLayer(1);       // Same layer as player
	SetMoveSpeed(3);   // Slightly slower than player (player is 4)
	SetDirection(2);   // Face down
	SetFacing(2);
	SetThrough(true);  // Walk through walls for chasing
	data()->active = true;
	data()->transparency = 0;
	data()->sprite_hidden = false;
	data()->animation_type = lcf::rpg::EventPage::AnimType_non_continuous;

	// Use Rawberry charset, last character (index 7)
	SetSpriteGraphic("Rawberry", 7);
}

void Game_Rawberry::SpawnFarFromPlayer() {
	if (!Main_Data::game_player) return;

	int player_x = Main_Data::game_player->GetX();
	int player_y = Main_Data::game_player->GetY();
	int map_w = Game_Map::GetTilesX();
	int map_h = Game_Map::GetTilesY();

	if (map_w <= 0 || map_h <= 0) return;

	// Try to find a position far from the player
	for (int attempts = 0; attempts < 50; attempts++) {
		int rx = Rand::GetRandomNumber(0, map_w - 1);
		int ry = Rand::GetRandomNumber(0, map_h - 1);

		int dx = std::abs(rx - player_x);
		int dy = std::abs(ry - player_y);

		// Handle map wrapping
		if (Game_Map::LoopHorizontal()) {
			dx = std::min(dx, map_w - dx);
		}
		if (Game_Map::LoopVertical()) {
			dy = std::min(dy, map_h - dy);
		}

		int dist = dx + dy; // Manhattan distance
		if (dist >= SPAWN_MIN_DIST) {
			SetMapId(Game_Map::GetMapId());
			SetX(rx);
			SetY(ry);
			SetRemainingStep(0);
			spawned = true;
			caught_player = false;
			Output::Debug("Rawberry: Spawned at ({}, {}), player at ({}, {}), dist={}",
				rx, ry, player_x, player_y, dist);
			return;
		}
	}

	// Fallback: spawn at opposite corner from player
	int sx = (player_x < map_w / 2) ? map_w - 2 : 1;
	int sy = (player_y < map_h / 2) ? map_h - 2 : 1;
	SetMapId(Game_Map::GetMapId());
	SetX(sx);
	SetY(sy);
	SetRemainingStep(0);
	spawned = true;
	caught_player = false;
	Output::Debug("Rawberry: Fallback spawn at ({}, {})", sx, sy);
}

void Game_Rawberry::TeleportAway() {
	if (!Main_Data::game_player) return;

	int player_x = Main_Data::game_player->GetX();
	int player_y = Main_Data::game_player->GetY();
	int map_w = Game_Map::GetTilesX();
	int map_h = Game_Map::GetTilesY();

	if (map_w <= 0 || map_h <= 0) return;

	// Teleport far from the player
	for (int attempts = 0; attempts < 30; attempts++) {
		int rx = Rand::GetRandomNumber(0, map_w - 1);
		int ry = Rand::GetRandomNumber(0, map_h - 1);

		int dx = std::abs(rx - player_x);
		int dy = std::abs(ry - player_y);
		if (Game_Map::LoopHorizontal()) dx = std::min(dx, map_w - dx);
		if (Game_Map::LoopVertical()) dy = std::min(dy, map_h - dy);

		if (dx + dy >= SPAWN_MIN_DIST) {
			SetX(rx);
			SetY(ry);
			SetRemainingStep(0);
			Output::Debug("Rawberry: Flashlight repelled! Teleported to ({}, {})", rx, ry);
			return;
		}
	}

	// Fallback: opposite corner
	int sx = (player_x < map_w / 2) ? map_w - 2 : 1;
	int sy = (player_y < map_h / 2) ? map_h - 2 : 1;
	SetX(sx);
	SetY(sy);
	SetRemainingStep(0);
	Output::Debug("Rawberry: Flashlight repelled (fallback) to ({}, {})", sx, sy);
}

bool Game_Rawberry::MakeWay(int from_x, int from_y, int to_x, int to_y) {
	// Rawberry collides with walls like a normal character
	return Game_Character::MakeWay(from_x, from_y, to_x, to_y);
}

void Game_Rawberry::UpdateNextMovementAction() {
	// Movement is driven by ChasePlayer() in UpdateChase(), nothing to do here
}

void Game_Rawberry::ChasePlayer() {
	if (!Main_Data::game_player) return;

	int px = Main_Data::game_player->GetX();
	int py = Main_Data::game_player->GetY();
	int mx = GetX();
	int my = GetY();

	int dx = px - mx;
	int dy = py - my;

	// Handle map wrapping
	if (Game_Map::LoopHorizontal()) {
		int tiles_x = Game_Map::GetTilesX();
		if (dx > tiles_x / 2) dx -= tiles_x;
		else if (dx < -tiles_x / 2) dx += tiles_x;
	}
	if (Game_Map::LoopVertical()) {
		int tiles_y = Game_Map::GetTilesY();
		if (dy > tiles_y / 2) dy -= tiles_y;
		else if (dy < -tiles_y / 2) dy += tiles_y;
	}

	if (dx == 0 && dy == 0) return;

	// Determine direction to move toward player
	int dir;
	if (dx > 0 && dy > 0) dir = DownRight;
	else if (dx > 0 && dy < 0) dir = UpRight;
	else if (dx < 0 && dy > 0) dir = DownLeft;
	else if (dx < 0 && dy < 0) dir = UpLeft;
	else if (dx > 0) dir = Right;
	else if (dx < 0) dir = Left;
	else if (dy > 0) dir = Down;
	else dir = Up;

	Move(dir);
}

void Game_Rawberry::CheckFlashlightCollision() {
	if (!Main_Data::game_player) return;

	// Only repel if flashlight is actually providing light
	// The MultiplayerState controls the flashlight radius;
	// we check distance vs the repel range (represents flashlight cone)
	auto& mp = MultiplayerState::Instance();
	if (!mp.IsFlashlightOn()) return;

	int px = Main_Data::game_player->GetX();
	int py = Main_Data::game_player->GetY();
	int mx = GetX();
	int my = GetY();

	int dx = std::abs(px - mx);
	int dy = std::abs(py - my);

	if (Game_Map::LoopHorizontal()) {
		dx = std::min(dx, Game_Map::GetTilesX() - dx);
	}
	if (Game_Map::LoopVertical()) {
		dy = std::min(dy, Game_Map::GetTilesY() - dy);
	}

	int dist = dx + dy; // Manhattan distance
	if (dist <= FLASHLIGHT_REPEL_DIST && dist > 0) {
		// She touched the flashlight! Teleport her far away
		TeleportAway();
	}
}

void Game_Rawberry::CheckPlayerCollision() {
	if (!Main_Data::game_player) return;

	int px = Main_Data::game_player->GetX();
	int py = Main_Data::game_player->GetY();

	if (GetX() == px && GetY() == py) {
		caught_player = true;
		Output::Debug("Rawberry: Caught the player!");
	}
}

void Game_Rawberry::UpdatePositionalAudio() {
	if (!Main_Data::game_player) return;

	if (!bgs_playing) {
		StartBGS();
	}

	int px = Main_Data::game_player->GetX();
	int py = Main_Data::game_player->GetY();
	int mx = GetX();
	int my = GetY();

	int dx = px - mx;
	int dy = py - my;

	if (Game_Map::LoopHorizontal()) {
		int tiles_x = Game_Map::GetTilesX();
		if (dx > tiles_x / 2) dx -= tiles_x;
		else if (dx < -tiles_x / 2) dx += tiles_x;
	}
	if (Game_Map::LoopVertical()) {
		int tiles_y = Game_Map::GetTilesY();
		if (dy > tiles_y / 2) dy -= tiles_y;
		else if (dy < -tiles_y / 2) dy += tiles_y;
	}

	double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));

	// Calculate volume: 100 at AUDIO_FULL_DIST, 0 at AUDIO_MAX_DIST
	int volume;
	if (dist <= AUDIO_FULL_DIST) {
		volume = 100;
	} else if (dist >= AUDIO_MAX_DIST) {
		volume = 0;
	} else {
		double t = (dist - AUDIO_FULL_DIST) / (AUDIO_MAX_DIST - AUDIO_FULL_DIST);
		volume = static_cast<int>(100.0 * (1.0 - t));
	}
	volume = std::max(0, std::min(100, volume));

	// Calculate balance (pan): 50 = center, 0 = full left, 100 = full right
	int balance = 50;
	if (dist > 0) {
		// dx negative = Rawberry is to the left of player
		double pan = static_cast<double>(dx) / static_cast<double>(AUDIO_MAX_DIST);
		pan = std::max(-1.0, std::min(1.0, pan));
		balance = 50 - static_cast<int>(pan * 50.0);
	}

	// Update BGS volume and balance every frame for smooth 3D audio
	Audio().BGS_Volume(volume);
	Audio().BGS_Balance(balance);
}

void Game_Rawberry::StartBGS() {
	auto stream = FileFinder::OpenMusic("Rawberry");
	if (!stream) {
		Output::Debug("Rawberry: Could not find Rawberry.mp3 (checked game Music folder and assets folder)");
		return;
	}
	Audio().BGS_Play(std::move(stream), 0, 100, 50);
	bgs_playing = true;
	Output::Debug("Rawberry: Started BGS loop");
}

void Game_Rawberry::StopBGS() {
	if (bgs_playing) {
		Audio().BGS_Stop();
		bgs_playing = false;
	}
}

void Game_Rawberry::UpdateChase() {
	if (!spawned || !Main_Data::game_player) return;

	// Drive animation
	SetProcessed(false);
	Update();

	// Chase AI with cooldown
	chase_cooldown++;
	if (chase_cooldown >= CHASE_INTERVAL) {
		chase_cooldown = 0;

		// Check flashlight repulsion first (before moving)
		CheckFlashlightCollision();

		// Move toward the player
		if (!IsMoving()) {
			ChasePlayer();
		}
	}

	// Check if caught the player (every frame)
	CheckPlayerCollision();

	// 3D positional audio
	UpdatePositionalAudio();
}

void Game_Rawberry::UpdateDisplay() {
	if (!spawned || !Main_Data::game_player) return;

	// Drive animation only (no AI)
	SetProcessed(false);
	Update();

	// Check if caught the player (every frame)
	CheckPlayerCollision();

	// 3D positional audio
	UpdatePositionalAudio();
}

void Game_Rawberry::SetSyncPosition(int x, int y, int direction) {
	SetX(x);
	SetY(y);
	SetDirection(direction);
	SetFacing(direction);
	SetRemainingStep(0);
	if (!spawned) {
		spawned = true;
		caught_player = false;
	}
}

} // namespace Chaos
