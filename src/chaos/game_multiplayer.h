/*
 * Chaos Fork: Remote Player Character for Multiplayer
 * Represents another player on the map as a visible character.
 */

#ifndef EP_CHAOS_GAME_MULTIPLAYER_H
#define EP_CHAOS_GAME_MULTIPLAYER_H

#include "game_character.h"
#include <lcf/rpg/savemapevent.h>
#include <string>

namespace Chaos {

/**
 * Represents a remote player character on the map.
 * Uses SaveMapEvent as storage since it extends SaveMapEventBase.
 */
class Game_RemotePlayer : public Game_CharacterDataStorage<lcf::rpg::SaveMapEvent> {
public:
	Game_RemotePlayer(uint16_t peer_id, const std::string& name);

	/** Set the remote player's target position (from network).
	 *  Instead of teleporting, walks smoothly for adjacent tiles. */
	void SetNetworkPosition(int map_id, int x, int y, int direction, int facing);

	/** Set the character sprite */
	void SetCharacterSprite(const std::string& sprite_name, int sprite_index);

	uint16_t GetPeerId() const { return peer_id; }
	const std::string& GetPlayerName() const { return player_name; }

	/** Remote players process queued movement each frame */
	void UpdateNextMovementAction() override;

	/** Public wrapper to drive movement and animation each frame */
	void UpdateRemote();

	/** Whether this remote player is on the current map */
	bool IsOnCurrentMap() const;

	/** Override MakeWay to always allow movement (no collision for remote) */
	bool MakeWay(int from_x, int from_y, int to_x, int to_y) override;

private:
	/** Teleport directly (used for non-adjacent moves or map changes) */
	void TeleportTo(int map_id, int x, int y, int direction, int facing);

	uint16_t peer_id;
	std::string player_name;

	// Target position received from network
	int target_x = 0;
	int target_y = 0;
	int target_direction = 2;
	int target_facing = 2;
	bool has_target = false;
};

} // namespace Chaos

#endif
