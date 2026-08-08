/*
 * Chaos Fork: Toggle voice chat implementation.
 * Uses Opus codec for compression and SDL2 audio for capture/playback.
 */

#include "chaos/multiplayer_voice.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "input.h"
#include "keys.h"
#include "output.h"
#include <cstring>

#ifdef USE_SDL
#if USE_SDL == 2
#include <SDL.h>
#define HAS_SDL_AUDIO 1
#endif
#endif

#ifdef HAVE_OPUS_CODEC
#include <opus/opus.h>
#endif

namespace Chaos {

// Max Opus encoded frame size (Opus docs recommend 4000 bytes max)
static constexpr int MAX_OPUS_FRAME_BYTES = 1500;
// Opus bitrate for voice (24 kbps is good for VOIP)
static constexpr int OPUS_BITRATE = 24000;
// Volume boost for incoming voice (1.0 = normal, 3.0 = 3x louder)
static constexpr float VOICE_VOLUME = 3.0f;

MultiplayerVoice& MultiplayerVoice::Instance() {
	static MultiplayerVoice instance;
	return instance;
}

bool MultiplayerVoice::Initialize() {
	if (initialized) return true;

#ifdef HAS_SDL_AUDIO
	// --- Open capture device (microphone) at 48kHz mono ---
	SDL_AudioSpec want_cap = {};
	SDL_AudioSpec have_cap = {};
	want_cap.freq = VOICE_FREQ;
	want_cap.format = AUDIO_S16SYS;
	want_cap.channels = VOICE_CHANNELS;
	want_cap.samples = VOICE_FRAME_SAMPLES;
	want_cap.callback = nullptr;

	capture_dev = SDL_OpenAudioDevice(nullptr, 1, &want_cap, &have_cap,
		SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (capture_dev == 0) {
		Output::Warning("Voice chat: failed to open capture device: {}", SDL_GetError());
		return false;
	}

	// --- Open playback device at 48kHz mono ---
	SDL_AudioSpec want_play = {};
	SDL_AudioSpec have_play = {};
	want_play.freq = VOICE_FREQ;
	want_play.format = AUDIO_S16SYS;
	want_play.channels = VOICE_CHANNELS;
	want_play.samples = VOICE_FRAME_SAMPLES;
	want_play.callback = nullptr;

	playback_dev = SDL_OpenAudioDevice(nullptr, 0, &want_play, &have_play, 0);
	if (playback_dev == 0) {
		Output::Warning("Voice chat: failed to open playback device: {}", SDL_GetError());
		SDL_CloseAudioDevice(capture_dev);
		capture_dev = 0;
		return false;
	}

	// Start playback immediately so queued audio plays
	SDL_PauseAudioDevice(playback_dev, 0);

	// Allocate buffers
	capture_buf.resize(VOICE_FRAME_SAMPLES * VOICE_CHANNELS * sizeof(int16_t));
	encode_buf.resize(MAX_OPUS_FRAME_BYTES);
	decode_buf.resize(VOICE_FRAME_SAMPLES * VOICE_CHANNELS);

#ifdef HAVE_OPUS_CODEC
	// --- Initialize Opus encoder ---
	int err = 0;
	opus_enc = opus_encoder_create(VOICE_FREQ, VOICE_CHANNELS,
		OPUS_APPLICATION_VOIP, &err);
	if (err != OPUS_OK || !opus_enc) {
		Output::Warning("Voice chat: opus_encoder_create failed: {}", opus_strerror(err));
		SDL_CloseAudioDevice(capture_dev);
		SDL_CloseAudioDevice(playback_dev);
		capture_dev = 0;
		playback_dev = 0;
		return false;
	}
	opus_encoder_ctl(opus_enc, OPUS_SET_BITRATE(OPUS_BITRATE));
	opus_encoder_ctl(opus_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

	// --- Initialize Opus decoder ---
	opus_dec = opus_decoder_create(VOICE_FREQ, VOICE_CHANNELS, &err);
	if (err != OPUS_OK || !opus_dec) {
		Output::Warning("Voice chat: opus_decoder_create failed: {}", opus_strerror(err));
		opus_encoder_destroy(opus_enc);
		opus_enc = nullptr;
		SDL_CloseAudioDevice(capture_dev);
		SDL_CloseAudioDevice(playback_dev);
		capture_dev = 0;
		playback_dev = 0;
		return false;
	}
#endif

	initialized = true;
	Output::Debug("Voice chat: initialized (capture={}, playback={}, opus={})",
		capture_dev, playback_dev,
#ifdef HAVE_OPUS_CODEC
		"yes"
#else
		"no (raw PCM)"
#endif
	);
	return true;
#else
	Output::Warning("Voice chat: SDL2 audio not available");
	return false;
#endif
}

void MultiplayerVoice::Shutdown() {
#ifdef HAVE_OPUS_CODEC
	if (opus_enc) {
		opus_encoder_destroy(opus_enc);
		opus_enc = nullptr;
	}
	if (opus_dec) {
		opus_decoder_destroy(opus_dec);
		opus_dec = nullptr;
	}
#endif
#ifdef HAS_SDL_AUDIO
	if (capture_dev) {
		SDL_CloseAudioDevice(capture_dev);
		capture_dev = 0;
	}
	if (playback_dev) {
		SDL_CloseAudioDevice(playback_dev);
		playback_dev = 0;
	}
#endif
	initialized = false;
	transmitting = false;
}

void MultiplayerVoice::Update() {
	if (!initialized) return;

	// Toggle voice on/off with F
	if (Input::IsRawKeyTriggered(Input::Keys::F)) {
		if (!transmitting) {
			StartTransmit();
		} else {
			StopTransmit();
		}
	}

	if (transmitting) {
		CaptureAndSend();
	}
}

void MultiplayerVoice::StartTransmit() {
#ifdef HAS_SDL_AUDIO
	transmitting = true;
	SDL_PauseAudioDevice(capture_dev, 0);
	SDL_ClearQueuedAudio(capture_dev);
#endif
}

void MultiplayerVoice::StopTransmit() {
#ifdef HAS_SDL_AUDIO
	transmitting = false;
	SDL_PauseAudioDevice(capture_dev, 1);
	SDL_ClearQueuedAudio(capture_dev);
#endif
}

void MultiplayerVoice::CaptureAndSend() {
#ifdef HAS_SDL_AUDIO
	auto& net = NetManager::Instance();
	if (!net.IsConnected()) return;

	const size_t frame_bytes = VOICE_FRAME_SAMPLES * VOICE_CHANNELS * sizeof(int16_t);

	while (SDL_GetQueuedAudioSize(capture_dev) >= static_cast<Uint32>(frame_bytes)) {
		SDL_DequeueAudio(capture_dev, capture_buf.data(), static_cast<Uint32>(frame_bytes));

#ifdef HAVE_OPUS_CODEC
		// Encode PCM to Opus
		const int16_t* pcm_in = reinterpret_cast<const int16_t*>(capture_buf.data());
		int encoded_bytes = opus_encode(opus_enc, pcm_in, VOICE_FRAME_SAMPLES,
			encode_buf.data(), MAX_OPUS_FRAME_BYTES);
		if (encoded_bytes < 0) {
			Output::Debug("Voice chat: opus_encode error: {}", opus_strerror(encoded_bytes));
			continue;
		}

		PacketWriter pw(PacketType::VoiceData);
		pw.write(static_cast<uint16_t>(encoded_bytes));
		pw.writeBytes(encode_buf.data(), encoded_bytes);
		net.Broadcast(pw, false);
#else
		// Fallback: send raw PCM
		PacketWriter pw(PacketType::VoiceData);
		pw.write(static_cast<uint16_t>(frame_bytes));
		pw.writeBytes(capture_buf.data(), frame_bytes);
		net.Broadcast(pw, false);
#endif
	}
#endif
}

static void AmplifyPcm(int16_t* samples, int count) {
	for (int i = 0; i < count; ++i) {
		int32_t s = static_cast<int32_t>(samples[i] * VOICE_VOLUME);
		if (s > 32767) s = 32767;
		else if (s < -32768) s = -32768;
		samples[i] = static_cast<int16_t>(s);
	}
}

void MultiplayerVoice::OnVoiceDataReceived(uint16_t /*peer_id*/, const uint8_t* data, size_t len) {
#ifdef HAS_SDL_AUDIO
	if (len == 0 || len > 4096) return;
	// Auto-initialize if we haven't yet
	if (!initialized) {
		Initialize();
	}
	if (playback_dev == 0) return;

#ifdef HAVE_OPUS_CODEC
	if (!opus_dec) return;

	// Decode Opus frame to PCM
	int decoded_samples = opus_decode(opus_dec, data, static_cast<int>(len),
		decode_buf.data(), VOICE_FRAME_SAMPLES, 0);
	if (decoded_samples < 0) {
		Output::Debug("Voice chat: opus_decode error: {}", opus_strerror(decoded_samples));
		return;
	}

	AmplifyPcm(decode_buf.data(), decoded_samples * VOICE_CHANNELS);

	SDL_QueueAudio(playback_dev,
		reinterpret_cast<const uint8_t*>(decode_buf.data()),
		static_cast<Uint32>(decoded_samples * VOICE_CHANNELS * sizeof(int16_t)));
#else
	// Fallback: raw PCM — copy, amplify, then queue
	size_t sample_count = len / sizeof(int16_t);
	decode_buf.resize(sample_count);
	std::memcpy(decode_buf.data(), data, len);
	AmplifyPcm(decode_buf.data(), static_cast<int>(sample_count));
	SDL_QueueAudio(playback_dev, reinterpret_cast<const uint8_t*>(decode_buf.data()), static_cast<Uint32>(len));
#endif
#else
	(void)data;
	(void)len;
#endif
}

} // namespace Chaos
