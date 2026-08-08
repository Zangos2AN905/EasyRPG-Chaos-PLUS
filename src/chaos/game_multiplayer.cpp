/*
 * Chaos Fork: Remote Player Character Implementation
 */

#include "chaos/game_multiplayer.h"
#include "game_map.h"
#include <algorithm>
#include <cstdlib>

namespace Chaos {

namespace {
// Clamp a tile coordinate to the current map bounds. No-op when the map
// is not loaded yet (tiles == 0) so remote packets received before the
// map loads can't produce OOB coordinates or crash map lookups.
void ClampToCurrentMap(int& x, int& y) {
	const int tiles_x = Game_Map::GetTilesX();
	const int tiles_y = Game_Map::GetTilesY();
	if (tiles_x > 0) {
		x = std::clamp(x, 0, tiles_x - 1);
	}
	if (tiles_y > 0) {
		y = std::clamp(y, 0, tiles_y - 1);
	}
}
} // namespace

Game_RemotePlayer::Game_RemotePlayer(uint16_t peer_id, const std::string& name)
	: Game_CharacterDataStorage(Game_Character::Event)
	, peer_id(peer_id)
	, player_name(name)
{
	// Set defaults for a visible character
	SetLayer(1);       // Same layer as player
	SetMoveSpeed(4);   // Normal speed
	SetDirection(2);   // Face down
	SetFacing(2);
	SetThrough(true);  // Don't block movement
	data()->active = true;
	data()->transparency = 0;
	data()->sprite_hidden = false;
	data()->animation_type = lcf::rpg::EventPage::AnimType_non_continuous;
}

void Game_RemotePlayer::SetNetworkPosition(int map_id, int x, int y, int direction, int facing) {
	// Clamp to the local map bounds when the remote player is on the same map.
	if (map_id == Game_Map::GetMapId()) {
		ClampToCurrentMap(x, y);
	}

	// Different map or first placement → teleport
	if (GetMapId() != map_id || (!has_target && GetX() == 0 && GetY() == 0)) {
		TeleportTo(map_id, x, y, direction, facing);
		return;
	}

	int dx = x - GetX();
	int dy = y - GetY();

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

	// If distance is more than a couple tiles, teleport instead of walking
	if (std::abs(dx) > 2 || std::abs(dy) > 2) {
		TeleportTo(map_id, x, y, direction, facing);
		return;
	}

	// Set target for smooth movement
	target_x = x;
	target_y = y;
	target_direction = direction;
	target_facing = facing;
	has_target = true;
}

void Game_RemotePlayer::TeleportTo(int map_id, int x, int y, int direction, int facing) {
	if (map_id == Game_Map::GetMapId()) {
		ClampToCurrentMap(x, y);
	}
	SetMapId(map_id);
	SetX(x);
	SetY(y);
	SetDirection(direction);
	SetFacing(facing);
	SetRemainingStep(0);
	target_x = x;
	target_y = y;
	target_direction = direction;
	target_facing = facing;
	has_target = false;
}

void Game_RemotePlayer::UpdateNextMovementAction() {
	if (!has_target) {
		SetDirection(target_direction);
		SetFacing(target_facing);
		return;
	}

	int dx = target_x - GetX();
	int dy = target_y - GetY();

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

	if (dx == 0 && dy == 0) {
		// Arrived at target
		has_target = false;
		SetDirection(target_direction);
		SetFacing(target_facing);
		return;
	}

	// Determine direction to move
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

bool Game_RemotePlayer::MakeWay(int, int, int, int) {
	// Remote players never collide — always allow movement
	return true;
}

void Game_RemotePlayer::UpdateRemote() {
	SetProcessed(false);
	Update();
}

void Game_RemotePlayer::SetCharacterSprite(const std::string& sprite_name, int sprite_index) {
	SetSpriteGraphic(sprite_name, sprite_index);
}

bool Game_RemotePlayer::IsOnCurrentMap() const {
	return GetMapId() == Game_Map::GetMapId();
}

} // namespace Chaos
