/*
 * Chaos Fork: Multiplayer boombox.
 */

#ifndef EP_CHAOS_MULTIPLAYER_BOOMBOX_H
#define EP_CHAOS_MULTIPLAYER_BOOMBOX_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Chaos {

class MultiplayerBoombox {
public:
	static MultiplayerBoombox& Instance();

	void Start();
	void Stop();
	void Update();
	bool IsOverridingGameMusic() const { return active && !restoring_game_music && HasActiveSource(); }
	bool SubmitTrack(size_t index);
	bool SubmitCustomMusic(const std::string& filename);
	void StopLocal();
	void HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len);
	void SendStatesTo(uint16_t peer_id);

private:
	MultiplayerBoombox() = default;

	struct State {
		bool active = false;
		std::string path;
	};

	void SetState(uint16_t peer_id, bool active, const std::string& path, bool broadcast);
	void SendState(uint16_t peer_id, bool active, const std::string& path, bool to_server);
	void AcceptCustom(uint16_t sender_id, uint32_t token, const std::string& name,
		const std::string& extension, std::vector<uint8_t> data);
	bool WriteCustomFile(const std::string& path, const std::vector<uint8_t>& data) const;
	int GetSlot(uint16_t peer_id);
	bool HasActiveSource() const;
	void RestoreGameMusic();

	bool active = false;
	std::map<uint16_t, State> states;
	std::map<uint16_t, int> slots;
	std::map<uint16_t, std::string> playing_paths;
	struct PendingUpload { uint32_t token; std::string name; std::string extension; std::vector<uint8_t> data; };
	struct IncomingUpload { std::vector<uint8_t> data; size_t received = 0; };
	std::map<uint16_t, PendingUpload> pending_uploads;
	std::map<std::string, IncomingUpload> incoming_uploads;
	std::set<std::string> custom_paths;
	bool restoring_game_music = false;
	bool saved_bgm_valid = false;
	struct SavedMusic {
		std::string name;
		int32_t volume = 100;
		int32_t tempo = 100;
		int32_t fadein = 0;
		int32_t balance = 50;
	} saved_bgm;
};

} // namespace Chaos

#endif
