/*
 * Chaos Fork: Toggle voice chat (press F to toggle).
 * Uses Opus codec for compression and SDL2 audio for capture/playback.
 */

#ifndef EP_CHAOS_MULTIPLAYER_VOICE_H
#define EP_CHAOS_MULTIPLAYER_VOICE_H

#include "system.h"
#include <cstdint>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

namespace Chaos {

class MultiplayerVoice {
public:
	static MultiplayerVoice& Instance();

	/** Initialize audio devices and Opus codec. Safe to call multiple times. */
	bool Initialize();

	/** Shut down audio devices and release resources. */
	void Shutdown();

	/** Call every frame from the chat Update loop. */
	void Update();

	/** Feed received voice data from a remote peer. */
	void OnVoiceDataReceived(uint16_t peer_id, const uint8_t* data, size_t len);

	bool IsInitialized() const { return initialized; }
	bool IsTransmitting() const { return transmitting; }

private:
	MultiplayerVoice() = default;

	void StartTransmit();
	void StopTransmit();
	void CaptureAndSend();

	bool initialized = false;
	bool transmitting = false;

	// SDL audio device IDs (0 = invalid)
	uint32_t capture_dev = 0;
	uint32_t playback_dev = 0;

	// Audio format: 48kHz mono 16-bit (Opus native rate)
	static constexpr int VOICE_FREQ = 48000;
	static constexpr int VOICE_CHANNELS = 1;
	static constexpr int VOICE_FRAME_MS = 20;
	// 960 samples = 20ms at 48kHz
	static constexpr int VOICE_FRAME_SAMPLES = VOICE_FREQ * VOICE_FRAME_MS / 1000;

	// Opus codec state
	OpusEncoder* opus_enc = nullptr;
	OpusDecoder* opus_dec = nullptr;

	// Buffers
	std::vector<uint8_t> capture_buf;
	std::vector<uint8_t> encode_buf;
	std::vector<int16_t> decode_buf;
};

} // namespace Chaos

#endif
