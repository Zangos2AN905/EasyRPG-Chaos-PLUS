/*
 * Chaos Fork: Fake players ("lambda players") that wander around, chat with a
 * basic Markov chain trained on assets/AILINES.txt, and optionally follow real
 * players in smart mode. Simulated on the host and replicated to clients like
 * normal players.
 */

#ifndef EP_CHAOS_MULTIPLAYER_AI_H
#define EP_CHAOS_MULTIPLAYER_AI_H

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace Chaos {

class MultiplayerAi {
public:
	static MultiplayerAi& Instance();

	/** Host: add fake players near the local player and sync the roster. */
	void Spawn(int count);

	/** Host: remove all fake players and sync the roster. */
	void Clear();

	/** Full reset when the multiplayer session ends. */
	void Reset();

	/** Smart mode: fake players walk toward real players instead of randomly. */
	void SetSmart(bool value) { smart_mode = value; }
	bool IsSmart() const { return smart_mode; }
	void ToggleSmart() { smart_mode = !smart_mode; }

	/** Host: simulate movement and chat. No-op on clients. */
	void Update();

	/** Name lookup for a fake peer id ("" when unknown). */
	std::string GetName(uint16_t peer_id) const;
	bool IsFakePeer(uint16_t peer_id) const;

	/** Host: send the roster to a newly joined player. */
	void SendRosterTo(uint16_t peer_id) const;

	/** AiSync packet: roster replace (id + name entries). */
	void HandlePacket(const uint8_t* data, size_t len);

private:
	struct FakePlayer {
		uint16_t peer_id = 0;
		std::string name;
		int map_id = 0;
		int x = 0;
		int y = 0;
		int direction = 2;
		int facing = 2;
		std::string sprite_name;
		int sprite_index = 0;
		int move_timer = 0;
		int chat_timer = 0;
		int keepalive_timer = 0;
		int last_dir = 2;
	};

	class Markov {
	public:
		std::string Generate();
	private:
		void EnsureLoaded();
		bool loaded = false;
		std::map<std::string, std::vector<std::string>> next_words;
		std::vector<std::string> start_words;
		std::mt19937 rng{std::random_device{}()};
	};

	MultiplayerAi() = default;

	int Rand(int min, int max);
	void TickMovement(FakePlayer& p);
	bool TryMove(FakePlayer& p, int direction);
	void TeleportToHost(FakePlayer& p);
	void SendPosition(const FakePlayer& p);
	void ApplyLocal(const FakePlayer& p);
	void Say(FakePlayer& p);
	void BroadcastRoster() const;

	std::map<uint16_t, FakePlayer> players;
	uint16_t next_fake_id = 0xF000;
	int name_counter = 0;
	bool smart_mode = false;
	std::mt19937 rng{std::random_device{}()};
	Markov markov;
};

} // namespace Chaos

#endif
