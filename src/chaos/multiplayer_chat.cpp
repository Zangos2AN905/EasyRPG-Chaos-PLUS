/*
 * Chaos Fork: Multiplayer Chat System Implementation
 */

#include "chaos/multiplayer_chat.h"
#include "chaos/multiplayer_state.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "chaos/multiplayer_mode.h"
#include "chaos/window_chat_dialogue.h"
#include "input.h"
#include "keys.h"
#include "player.h"
#include "game_party.h"
#include "game_actor.h"
#include "game_system.h"
#include "main_data.h"
#include "window_help.h"
#include "output.h"
#include <algorithm>
#include <cctype>

namespace Chaos {

const std::string MultiplayerChat::input_chars =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?'-:;()";

MultiplayerChat& MultiplayerChat::Instance() {
	static MultiplayerChat instance;
	return instance;
}

// ---------------------------------------------------------------------------
// Map lifecycle
// ---------------------------------------------------------------------------

void MultiplayerChat::OnMapLoaded() {
	// The overlay is a narrow window at the very top.
	overlay_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 64);
	overlay_window->SetVisible(false);
	overlay_window->SetZ(Priority_Window + 180);
	overlay_window->SetBackOpacity(128);

	// Input bar (shown at bottom when typing)
	input_window = std::make_unique<Window_Help>(0, Player::screen_height - 32,
												 Player::screen_width, 32);
	input_window->SetVisible(false);
	input_window->SetZ(Priority_Window + 200);

}

void MultiplayerChat::OnMapUnloaded() {
	CloseInput();
	overlay_window.reset();
	input_window.reset();
	dialogue_window.reset();
	dialogue_queue.clear();
}

void MultiplayerChat::Reset() {
	CloseInput();
	overlay_entries.clear();
	overlay_window.reset();
	input_window.reset();
	dialogue_window.reset();
	dialogue_queue.clear();
}

// ---------------------------------------------------------------------------
// Per-frame update (called from Scene_Map)
// ---------------------------------------------------------------------------

bool MultiplayerChat::Update() {
	auto& mp = MultiplayerState::Instance();
	if (!mp.IsActive()) return false;

	UpdateOverlay();
	UpdateDialogue();


	if (input_active) {
		UpdateInput();
		return true; // block game input while typing
	}

	if (!chat_enabled) return false;

	// T = Normal chat
	if (Input::IsRawKeyTriggered(Input::Keys::T)) {
		OpenInput(false);
		return true;
	}

	// Y = Dialogue chat (TeamParty / Chaotix only)
	if (Input::IsRawKeyTriggered(Input::Keys::Y)) {
		auto& net = NetManager::Instance();
		auto mode = net.GetMode();
		if (mode == MultiplayerMode::TeamParty || mode == MultiplayerMode::Chaotix) {
			OpenInput(true);
			return true;
		} else {
			Main_Data::game_system->SePlay(
				Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
		}
	}

	return false;
}

// ---------------------------------------------------------------------------
// Text input
// ---------------------------------------------------------------------------

void MultiplayerChat::OpenInput(bool dialogue_mode) {
	input_active = true;
	input_dialogue_mode = dialogue_mode;
	input_buffer.clear();
	input_cursor = 0;

	if (input_window) {
		input_window->SetVisible(true);
		input_window->SetText(dialogue_mode ? "Say (Dialogue): _" : "Say: _");
	}
}

void MultiplayerChat::CloseInput() {
	input_active = false;
	input_buffer.clear();
	input_cursor = 0;
	if (input_window) {
		input_window->SetVisible(false);
	}
}

void MultiplayerChat::UpdateInput() {
	// ESC / Cancel -> close
	if (Input::IsRawKeyTriggered(Input::Keys::ESCAPE)) {
		Main_Data::game_system->SePlay(
			Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		CloseInput();
		return;
	}

	// ENTER -> send
	if (Input::IsRawKeyTriggered(Input::Keys::RETURN)) {
		if (!input_buffer.empty()) {
			if (input_dialogue_mode) {
				SendDialogueChat(input_buffer);
			} else {
				SendNormalChat(input_buffer);
			}
		}
		CloseInput();
		return;
	}

	// Backspace
	if (Input::IsRawKeyTriggered(Input::Keys::BACKSPACE)) {
		if (!input_buffer.empty()) {
			input_buffer.pop_back();
		}
	}

	// Character input — scan all printable keys
	static const struct { Input::Keys::InputKey key; char lower; char upper; } char_keys[] = {
		{ Input::Keys::A, 'a', 'A' }, { Input::Keys::B, 'b', 'B' },
		{ Input::Keys::C, 'c', 'C' }, { Input::Keys::D, 'd', 'D' },
		{ Input::Keys::E, 'e', 'E' }, { Input::Keys::F, 'f', 'F' },
		{ Input::Keys::G, 'g', 'G' }, { Input::Keys::H, 'h', 'H' },
		{ Input::Keys::I, 'i', 'I' }, { Input::Keys::J, 'j', 'J' },
		{ Input::Keys::K, 'k', 'K' }, { Input::Keys::L, 'l', 'L' },
		{ Input::Keys::M, 'm', 'M' }, { Input::Keys::N, 'n', 'N' },
		{ Input::Keys::O, 'o', 'O' }, { Input::Keys::P, 'p', 'P' },
		{ Input::Keys::Q, 'q', 'Q' }, { Input::Keys::R, 'r', 'R' },
		{ Input::Keys::S, 's', 'S' },
		// T is the chat trigger — still allow typing T once input is open
		{ Input::Keys::T, 't', 'T' },
		{ Input::Keys::U, 'u', 'U' }, { Input::Keys::V, 'v', 'V' },
		{ Input::Keys::W, 'w', 'W' }, { Input::Keys::X, 'x', 'X' },
		{ Input::Keys::Y, 'y', 'Y' }, { Input::Keys::Z, 'z', 'Z' },
		{ Input::Keys::N0, '0', ')' }, { Input::Keys::N1, '1', '!' },
		{ Input::Keys::N2, '2', '@' }, { Input::Keys::N3, '3', '#' },
		{ Input::Keys::N4, '4', '$' }, { Input::Keys::N5, '5', '%' },
		{ Input::Keys::N6, '6', '^' }, { Input::Keys::N7, '7', '&' },
		{ Input::Keys::N8, '8', '*' }, { Input::Keys::N9, '9', '(' },
		{ Input::Keys::SPACE, ' ', ' ' },
		{ Input::Keys::PERIOD, '.', '>' },
		{ Input::Keys::COMMA, ',', '<' },
		{ Input::Keys::SLASH, '/', '?' },
		{ Input::Keys::SEMICOLON, ';', ':' },
		{ Input::Keys::APOSTROPH, '\'', '"' },
	};

	bool shift = Input::IsRawKeyPressed(Input::Keys::LSHIFT) ||
				 Input::IsRawKeyPressed(Input::Keys::RSHIFT);

	static constexpr int MAX_CHAT_LEN = 80;
	if (static_cast<int>(input_buffer.size()) < MAX_CHAT_LEN) {
		for (auto& ck : char_keys) {
			if (Input::IsRawKeyTriggered(ck.key)) {
				input_buffer += shift ? ck.upper : ck.lower;
			}
		}
	}

	// Refresh display
	if (input_window) {
		std::string prefix = input_dialogue_mode ? "Say (Dialogue): " : "Say: ";
		input_window->SetText(prefix + input_buffer + "_");
	}
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

void MultiplayerChat::SendNormalChat(const std::string& text) {
	auto& net = NetManager::Instance();
	if (!net.IsConnected()) return;

	PacketWriter pw(PacketType::ChatMessage);
	pw.write(static_cast<uint8_t>(0)); // 0 = normal chat
	pw.write(text);
	net.Broadcast(pw, true);

	// Show locally too
	std::string local_name = net.GetLocalPlayerName();
	OnChatMessageReceived(net.GetLocalPeerId(), local_name, text, false);
}

void MultiplayerChat::SendDialogueChat(const std::string& text) {
	auto& net = NetManager::Instance();
	if (!net.IsConnected()) return;

	PacketWriter pw(PacketType::ChatMessage);
	pw.write(static_cast<uint8_t>(1)); // 1 = dialogue chat
	pw.write(text);
	net.Broadcast(pw, true);

	// Show locally too
	std::string local_name = net.GetLocalPlayerName();
	OnChatMessageReceived(net.GetLocalPeerId(), local_name, text, true);
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

void MultiplayerChat::OnChatMessageReceived(uint16_t /*sender_peer_id*/,
											const std::string& sender_name,
											const std::string& message,
											bool is_dialogue) {
	if (!chat_enabled) return;

	// Apply moderation filter
	if (!PassesModeration(message)) {
		Output::Debug("Chat: message from {} blocked by moderation", sender_name);
		return;
	}

	std::string display_text = FilterText(message);

	if (is_dialogue) {
		// Show as RPG dialogue box
		ShowDialogue(sender_name, display_text);
	} else {
		// Add to normal chat overlay
		ChatEntry entry;
		entry.sender = sender_name;
		entry.text = display_text;
		entry.timer = OVERLAY_DISPLAY_FRAMES;
		overlay_entries.push_back(std::move(entry));

		// Limit visible lines
		while (static_cast<int>(overlay_entries.size()) > MAX_OVERLAY_LINES) {
			overlay_entries.pop_front();
		}
		RefreshOverlay();
	}
}

// ---------------------------------------------------------------------------
// Normal chat overlay
// ---------------------------------------------------------------------------

void MultiplayerChat::UpdateOverlay() {
	if (overlay_entries.empty()) {
		if (overlay_window && overlay_window->IsVisible()) {
			overlay_window->SetVisible(false);
		}
		return;
	}

	bool changed = false;
	for (auto it = overlay_entries.begin(); it != overlay_entries.end();) {
		it->timer--;
		if (it->timer <= 0) {
			it = overlay_entries.erase(it);
			changed = true;
		} else {
			++it;
		}
	}
	if (changed) RefreshOverlay();
}

void MultiplayerChat::RefreshOverlay() {
	if (!overlay_window) return;

	if (overlay_entries.empty()) {
		overlay_window->SetVisible(false);
		return;
	}

	std::string combined;
	for (size_t i = 0; i < overlay_entries.size(); ++i) {
		if (i > 0) combined += "  ";
		combined += overlay_entries[i].sender + ": " + overlay_entries[i].text;
	}
	overlay_window->SetText(combined);
	overlay_window->SetVisible(true);
}

// ---------------------------------------------------------------------------
// Dialogue chat
// ---------------------------------------------------------------------------

void MultiplayerChat::ShowDialogue(const std::string& sender, const std::string& text) {
	// If a dialogue is already showing, queue it
	if (dialogue_window && !dialogue_window->IsFinished()) {
		dialogue_queue.push_back({sender, text});
		return;
	}

	// Get face from the party leader
	std::string face_name;
	int face_index = 0;
	if (Main_Data::game_party && Main_Data::game_party->GetBattlerCount() > 0) {
		auto* actor = Main_Data::game_party->GetActor(0);
		if (actor) {
			face_name = std::string(actor->GetFaceName());
			face_index = actor->GetFaceIndex();
		}
	}

	dialogue_window = std::make_unique<Window_ChatDialogue>(
		face_name, face_index, sender, text, DIALOGUE_DISPLAY_FRAMES);
}

void MultiplayerChat::UpdateDialogue() {
	if (!dialogue_window) {
		// Nothing showing — try to pop from queue
		if (!dialogue_queue.empty()) {
			auto entry = std::move(dialogue_queue.front());
			dialogue_queue.pop_front();
			ShowDialogue(entry.sender, entry.text);
		}
		return;
	}

	dialogue_window->Update();

	if (dialogue_window->IsFinished()) {
		dialogue_window.reset();

		// Immediately show next in queue if available
		if (!dialogue_queue.empty()) {
			auto entry = std::move(dialogue_queue.front());
			dialogue_queue.pop_front();
			ShowDialogue(entry.sender, entry.text);
		}
	}
}

// ---------------------------------------------------------------------------
// Moderation
// ---------------------------------------------------------------------------

// Slurs — censored at Basic and above.
static const char* slur_words[] = {
	"nigger", "nigga", "faggot", "retard", "tranny", "kike", "spic", "chink", "wetback",
	nullptr
};

// General profanity — censored at Moderate and above.
static const char* profanity_words[] = {
	"fuck", "shit", "bitch", "asshole", "dick", "cunt",
	nullptr
};

static std::string ToLower(const std::string& s) {
	std::string out = s;
	for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

static bool ContainsWord(const std::string& text_lower, const char* word) {
	return text_lower.find(word) != std::string::npos;
}

bool MultiplayerChat::PassesModeration(const std::string& text) const {
	if (moderation == ModerationLevel::None) return true;

	std::string lower = ToLower(text);

	if (moderation == ModerationLevel::Strict) {
		// Strict: block if ANY blocked word is found (slurs + profanity)
		for (int i = 0; slur_words[i]; ++i) {
			if (ContainsWord(lower, slur_words[i])) return false;
		}
		for (int i = 0; profanity_words[i]; ++i) {
			if (ContainsWord(lower, profanity_words[i])) return false;
		}
	}
	// Basic / Moderate: allow message but filter text later
	return true;
}

static void CensorWords(const char* const list[], std::string& result, std::string& lower) {
	for (int i = 0; list[i]; ++i) {
		const char* word = list[i];
		size_t wlen = std::strlen(word);
		size_t pos = 0;
		while ((pos = lower.find(word, pos)) != std::string::npos) {
			for (size_t j = 0; j < wlen && (pos + j) < result.size(); ++j) {
				result[pos + j] = '*';
				lower[pos + j] = '*';
			}
			pos += wlen;
		}
	}
}

std::string MultiplayerChat::FilterText(const std::string& text) const {
	if (moderation == ModerationLevel::None) return text;

	std::string lower = ToLower(text);
	std::string result = text;

	// Basic: only censor slurs
	CensorWords(slur_words, result, lower);

	// Moderate+: also censor general profanity
	if (moderation >= ModerationLevel::Moderate) {
		CensorWords(profanity_words, result, lower);
	}

	return result;
}

} // namespace Chaos
