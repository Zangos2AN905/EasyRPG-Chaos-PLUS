/*
 * Chaos Fork: Multiplayer radio.
 */

#ifndef EP_CHAOS_MULTIPLAYER_RADIO_H
#define EP_CHAOS_MULTIPLAYER_RADIO_H

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace Chaos {

struct RadioTrack {
	uint32_t id = 0;
	bool custom = false;
	std::string name;
	std::string path;
};

class MultiplayerRadio {
public:
	static MultiplayerRadio& Instance();

	void Start();
	void Stop();
	void Update();
	void OnMapLoaded();

	void RefreshAvailableTracks();
	const std::vector<RadioTrack>& GetAvailableTracks() const { return available_tracks; }
	const std::deque<RadioTrack>& GetQueue() const { return queue; }

	bool SubmitGameTrack(size_t index);
	bool SubmitCustomMusic(const std::string& filename);

	void HandlePacket(uint16_t sender_id, const uint8_t* data, size_t len);
	void SendQueueTo(uint16_t peer_id);

private:
	MultiplayerRadio() = default;

	struct PendingUpload {
		uint32_t token = 0;
		std::string name;
		std::string extension;
		std::vector<uint8_t> data;
	};

	struct IncomingDownload {
		std::string name;
		std::string extension;
		std::vector<uint8_t> data;
		size_t received = 0;
	};

	void QueueGameTrack(const std::string& path);
	void AcceptCustomUpload(uint16_t sender_id, uint32_t token, std::string name,
		std::string extension, std::vector<uint8_t> data);
	void AddTrack(RadioTrack track);
	void BroadcastQueue();
	void ApplyQueue(const std::deque<RadioTrack>& new_queue);
	void StartTrack(const RadioTrack& track);
	void AdvanceTrack();
	void RestoreGameMusic();
	bool WriteCustomFile(uint32_t track_id, const std::string& extension,
		const std::vector<uint8_t>& data) const;
	void BroadcastCustomTrack(uint32_t track_id, const std::string& name,
		const std::string& extension, const std::vector<uint8_t>& data);
	void SendCustomTrackTo(uint16_t peer_id, uint32_t track_id,
		const RadioTrack& track);

	bool active = false;
	uint32_t next_track_id = 1;
	uint32_t next_upload_token = 1;
	uint32_t queue_revision = 0;
	uint32_t playing_track_id = 0;
	int playback_grace_frames = 0;
	std::deque<RadioTrack> queue;
	std::vector<RadioTrack> available_tracks;
	std::map<uint32_t, std::vector<uint8_t>> custom_data;
	std::map<uint16_t, PendingUpload> pending_uploads;
	std::map<uint32_t, IncomingDownload> incoming_downloads;

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
