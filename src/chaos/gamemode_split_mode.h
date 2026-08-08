/*
 * Chaos Fork: Split multiplayer mode.
 * Keeps Split-specific side assignment, transitions, death recovery, and
 * rendering out of the general multiplayer coordinator.
 */

#ifndef EP_CHAOS_GAMEMODE_SPLIT_MODE_H
#define EP_CHAOS_GAMEMODE_SPLIT_MODE_H

#include <cstdint>
#include <cstddef>

class Spriteset_Map;

namespace Chaos {

class SplitOverlay;

struct SplitMapDestination {
	int map_id = 0;
	int x = 0;
	int y = 0;
};

class SplitMode {
public:
	static SplitMode& Instance();

	void Start();
	void Stop();
	void Update();
	void OnMapLoaded(Spriteset_Map* spriteset);
	void OnMapUnloaded();

	bool IsActive() const { return active; }
	bool IsExeSide() const { return active && exe_side; }
	int EnemyHealthMultiplier() const { return active ? 3 : 1; }

	SplitMapDestination GetRandomMapDestination(int excluded_map_id) const;

	// Called when the local party would otherwise enter Game Over.
	bool HandlePlayerDeath();

	// Network side assignment and client death requests.
	void AssignPeer(uint16_t peer_id);
	void HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len);

private:
	SplitMode() = default;

	void SetPeerSide(uint16_t peer_id, bool exe_side, bool broadcast);
	void SetLocalExeSide(bool value);

	bool active = false;
	bool exe_side = false;
	bool death_recovery_pending = false;
	Spriteset_Map* current_spriteset = nullptr;
	SplitOverlay* overlay = nullptr;
};

} // namespace Chaos

#endif
