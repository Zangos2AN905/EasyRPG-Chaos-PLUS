/*
 * Chaos Fork: Multiplayer Chat System Implementation
 */

#include "chaos/multiplayer_chat.h"
#include "chaos/multiplayer_state.h"
#include "chaos/net_manager.h"
#include "chaos/net_packet.h"
#include "chaos/multiplayer_mode.h"
#include "chaos/discord_integration.h"
#include "chaos/window_chat_dialogue.h"
#include "game_character.h"
#include "game_player.h"
#include "input.h"
#include "keys.h"
#include "player.h"
#include "game_party.h"
#include "game_actor.h"
#include "game_system.h"
#include "main_data.h"
#include "window_help.h"
#include "output.h"
#include "sprite.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <thread>

namespace Chaos {

namespace {

size_t AvatarWriteCallback(void* data, size_t size, size_t count, void* user_data) {
	auto& output = *static_cast<std::vector<uint8_t>*>(user_data);
	const size_t bytes = size * count;
	if (output.size() + bytes > 1024 * 1024) return 0;
	const auto* begin = static_cast<const uint8_t*>(data);
	output.insert(output.end(), begin, begin + bytes);
	return bytes;
}

} // namespace

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
	const int player_list_width = std::min(360, std::max(16, Player::screen_width - 16));
	player_list_window = std::make_unique<Window_Help>(
		(Player::screen_width - player_list_width) / 2, 32,
		player_list_width, Player::screen_height - 64);
	player_list_window->SetVisible(false);
	player_list_window->SetZ(Priority_Window + 210);

}

void MultiplayerChat::OnMapUnloaded() {
	CloseInput();
	overlay_window.reset();
	input_window.reset();
	player_list_window.reset();
	avatar_sprites.clear();
	chat_bubbles.clear();
	dialogue_window.reset();
	dialogue_queue.clear();
}

void MultiplayerChat::Reset() {
	CloseInput();
	overlay_entries.clear();
	overlay_window.reset();
	input_window.reset();
	player_list_window.reset();
	avatar_sprites.clear();
	chat_bubbles.clear();
	std::lock_guard lock(avatar_mutex);
	avatar_bitmaps.clear();
	avatar_downloads.clear();
	avatar_ready.clear();
	dialogue_window.reset();
	dialogue_queue.clear();
}

// ---------------------------------------------------------------------------
// Per-frame update (called from Scene_Map)
// ---------------------------------------------------------------------------

bool MultiplayerChat::Update() {
	auto& mp = MultiplayerState::Instance();
	if (!mp.IsActive()) {
		if (player_list_window) player_list_window->SetVisible(false);
		chat_bubbles.clear();
		return false;
	}

	UpdateOverlay();
	UpdateDialogue();
	UpdatePlayerList();
	UpdateChatBubbles();


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
		auto& mp = MultiplayerState::Instance();
		if (mp.IsModeActive(MultiplayerMode::TeamParty) || mp.IsModeActive(MultiplayerMode::Chaotix)) {
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
	key_hold_frames.fill(0);

	if (input_window) {
		input_window->SetVisible(true);
		input_window->SetText(dialogue_mode ? "Say (Dialogue): _" : "Say: _");
	}
}

void MultiplayerChat::CloseInput() {
	input_active = false;
	input_buffer.clear();
	input_cursor = 0;
	key_hold_frames.fill(0);
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
	const bool backspace_pressed = Input::IsRawKeyPressed(Input::Keys::BACKSPACE);
	int& backspace_held = key_hold_frames[Input::Keys::BACKSPACE];
	bool erase = false;
	if (!backspace_pressed) {
		backspace_held = 0;
	} else if (Input::IsRawKeyTriggered(Input::Keys::BACKSPACE)) {
		backspace_held = 1;
		erase = true;
	} else if (backspace_held > 0) {
		++backspace_held;
		erase = backspace_held >= 30 && (backspace_held - 30) % 4 == 0;
	}
	if (erase && !input_buffer.empty()) {
		input_buffer.pop_back();
	}

	// Character input — scan all printable keys
	static const struct { Input::Keys::InputKey key; char lower; char upper; bool letter; } char_keys[] = {
		{ Input::Keys::A, 'a', 'A', true }, { Input::Keys::B, 'b', 'B', true },
		{ Input::Keys::C, 'c', 'C', true }, { Input::Keys::D, 'd', 'D', true },
		{ Input::Keys::E, 'e', 'E', true }, { Input::Keys::F, 'f', 'F', true },
		{ Input::Keys::G, 'g', 'G', true }, { Input::Keys::H, 'h', 'H', true },
		{ Input::Keys::I, 'i', 'I', true }, { Input::Keys::J, 'j', 'J', true },
		{ Input::Keys::K, 'k', 'K', true }, { Input::Keys::L, 'l', 'L', true },
		{ Input::Keys::M, 'm', 'M', true }, { Input::Keys::N, 'n', 'N', true },
		{ Input::Keys::O, 'o', 'O', true }, { Input::Keys::P, 'p', 'P', true },
		{ Input::Keys::Q, 'q', 'Q', true }, { Input::Keys::R, 'r', 'R', true },
		{ Input::Keys::S, 's', 'S', true },
		// T is the chat trigger — still allow typing T once input is open
		{ Input::Keys::T, 't', 'T', true },
		{ Input::Keys::U, 'u', 'U', true }, { Input::Keys::V, 'v', 'V', true },
		{ Input::Keys::W, 'w', 'W', true }, { Input::Keys::X, 'x', 'X', true },
		{ Input::Keys::Y, 'y', 'Y', true }, { Input::Keys::Z, 'z', 'Z', true },
		{ Input::Keys::N0, '0', ')', false }, { Input::Keys::N1, '1', '!', false },
		{ Input::Keys::N2, '2', '@', false }, { Input::Keys::N3, '3', '#', false },
		{ Input::Keys::N4, '4', '$', false }, { Input::Keys::N5, '5', '%', false },
		{ Input::Keys::N6, '6', '^', false }, { Input::Keys::N7, '7', '&', false },
		{ Input::Keys::N8, '8', '*', false }, { Input::Keys::N9, '9', '(', false },
		{ Input::Keys::SPACE, ' ', ' ', false },
		{ Input::Keys::PERIOD, '.', '>', false },
		{ Input::Keys::COMMA, ',', '<', false },
		{ Input::Keys::SLASH, '/', '?', false },
		{ Input::Keys::SEMICOLON, ';', ':', false },
		{ Input::Keys::APOSTROPH, '\'', '"', false },
	};

	bool shift = Input::IsRawKeyPressed(Input::Keys::LSHIFT) ||
				 Input::IsRawKeyPressed(Input::Keys::RSHIFT);
	bool caps_lock = Input::IsCapsLockActive();

	static constexpr int MAX_CHAT_LEN = 80;
	if (static_cast<int>(input_buffer.size()) < MAX_CHAT_LEN) {
		for (auto& ck : char_keys) {
			const bool pressed = Input::IsRawKeyPressed(ck.key);
			int& held = key_hold_frames[static_cast<unsigned>(ck.key)];
			bool insert = false;
			if (!pressed) {
				held = 0;
			} else if (Input::IsRawKeyTriggered(ck.key)) {
				held = 1;
				insert = true;
			} else if (held > 0) {
				++held;
				insert = held >= 30 && (held - 30) % 4 == 0;
			}
			if (insert) {
				const bool uppercase = ck.letter ? (shift != caps_lock) : shift;
				input_buffer += uppercase ? ck.upper : ck.lower;
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
	pw.write(net.GetLocalPeerId());
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
	pw.write(net.GetLocalPeerId());
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

void MultiplayerChat::OnChatMessageReceived(uint16_t sender_peer_id,
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
	ShowChatBubble(sender_peer_id, display_text);

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
		if (i > 0) combined += "\n";
		combined += overlay_entries[i].sender + ": " + overlay_entries[i].text;
	}
	overlay_window->SetText(combined);
	overlay_window->SetVisible(true);
}

void MultiplayerChat::UpdatePlayerList() {
	if (!player_list_window) return;
	ProcessAvatarDownloads();
	const bool visible = !input_active && Input::IsRawKeyPressed(Input::Keys::TAB);
	if (!visible) {
		player_list_window->SetVisible(false);
		for (auto& [peer_id, sprite] : avatar_sprites) sprite->SetVisible(false);
		return;
	}

	RefreshPlayerList();
	player_list_window->Update();
	player_list_window->SetVisible(true);
}

void MultiplayerChat::RefreshPlayerList() {
	if (!player_list_window) return;

	auto& net = NetManager::Instance();
	std::string host_name = net.IsHost() ? net.GetLocalPlayerName() : std::string();
	if (host_name.empty()) {
		if (auto* host = net.FindPeer(1)) host_name = host->player_name;
	}
	if (host_name.empty()) host_name = "Player";

	struct PlayerEntry {
		uint16_t peer_id;
		std::string name;
		std::string discord_user_id;
		std::string avatar_hash;
	};
	std::vector<PlayerEntry> entries;
	auto local_entry = PlayerEntry{
		net.GetLocalPeerId(), net.GetLocalPlayerName(),
		DiscordIntegration::GetDiscordUserId(), DiscordIntegration::GetDiscordAvatarHash()};
	if (net.IsHost()) {
		entries.push_back(local_entry);
		for (const auto& peer : net.GetPeers()) {
			entries.push_back({peer.peer_id, peer.player_name, peer.discord_user_id, peer.discord_avatar_hash});
		}
	} else {
		for (const auto& peer : net.GetPeers()) {
			entries.push_back({peer.peer_id, peer.player_name, peer.discord_user_id, peer.discord_avatar_hash});
		}
		entries.push_back(local_entry);
	}

	std::string text = "Players\nHosted by " + host_name + "\n\n";
	text += "Online: " + std::to_string(entries.size()) + "\n";
	std::vector<uint16_t> player_ids;
	for (const auto& entry : entries) {
		text += "   " + (entry.name.empty() ? std::string("Player") : entry.name);
		if (entry.peer_id == net.GetLocalPeerId()) text += " (You)";
		text += "\n";
		player_ids.push_back(entry.peer_id);
		if (!entry.discord_user_id.empty() && !entry.avatar_hash.empty()) {
			StartAvatarDownload(entry.discord_user_id, entry.avatar_hash);
		}
	}

	player_list_window->SetText(text);
	UpdateAvatarSprites(player_ids);
}

std::string MultiplayerChat::WrapBubbleText(const std::string& message) {
	constexpr size_t MAX_LINE_LENGTH = 28;
	std::string result;
	size_t line_start = 0;
	while (line_start < message.size()) {
		const size_t newline = message.find('\n', line_start);
		const size_t line_end = newline == std::string::npos ? message.size() : newline;
		size_t pos = line_start;
		while (line_end - pos > MAX_LINE_LENGTH) {
			size_t break_at = message.rfind(' ', pos + MAX_LINE_LENGTH);
			if (break_at < pos) break_at = pos + MAX_LINE_LENGTH;
			result.append(message, pos, break_at - pos);
			result += '\n';
			pos = break_at;
			while (pos < line_end && message[pos] == ' ') ++pos;
		}
		result.append(message, pos, line_end - pos);
		if (newline != std::string::npos) result += '\n';
		line_start = newline == std::string::npos ? message.size() : newline + 1;
	}
	return result;
}

void MultiplayerChat::ShowChatBubble(uint16_t peer_id, const std::string& message) {
	if (!player_list_window || message.empty()) return;
	const std::string wrapped = WrapBubbleText(message);
	auto set_bubble_text = [](Window_Help& window, const std::string& text) {
		window.SetText(text);
		// Bitmap fonts do not support ApplyStyle size changes. Scale the
		// rendered content so bubbles stay smaller with every game font.
		auto source = window.GetContents();
		auto scaled = Bitmap::Create(source->GetWidth(), source->GetHeight(), true);
		scaled->ZoomOpacityBlit(0, 0, 0, 0, *source, source->GetRect(),
			0.75, 0.75, Opacity::Opaque());
		window.SetContents(scaled);
	};
	auto it = chat_bubbles.find(peer_id);
	if (it != chat_bubbles.end()) {
		set_bubble_text(*it->second.window, wrapped);
		it->second.timer = BUBBLE_DISPLAY_FRAMES;
		return;
	}

	const int line_count = static_cast<int>(std::count(wrapped.begin(), wrapped.end(), '\n')) + 1;
	ChatBubble bubble;
	bubble.window = std::make_unique<Window_Help>(0, 0, 208, 16 * line_count + 16);
	bubble.window->SetBackOpacity(255);
	bubble.window->SetFrameOpacity(255);
	bubble.window->SetZ(Priority_Window + 205);
	set_bubble_text(*bubble.window, wrapped);
	bubble.timer = BUBBLE_DISPLAY_FRAMES;
	chat_bubbles.emplace(peer_id, std::move(bubble));
}

void MultiplayerChat::UpdateChatBubbles() {
	for (auto it = chat_bubbles.begin(); it != chat_bubbles.end();) {
		Game_Character* target = nullptr;
		if (it->first == NetManager::Instance().GetLocalPeerId()) {
			target = Main_Data::game_player.get();
		} else {
			auto* remote = MultiplayerState::Instance().GetRemotePlayer(it->first);
			if (remote && remote->IsOnCurrentMap()) target = remote;
		}

		if (!target || !target->IsVisible()) {
			it->second.window->SetVisible(false);
		} else {
			const int x = std::clamp(target->GetScreenX() - it->second.window->GetWidth() / 2,
				0, std::max(0, Player::screen_width - it->second.window->GetWidth()));
			const int y = std::max(0, target->GetScreenY() - 32 - it->second.window->GetHeight());
			it->second.window->SetX(x);
			it->second.window->SetY(y);
			it->second.window->SetVisible(true);
			it->second.window->Update();
		}

		if (--it->second.timer <= 0) it = chat_bubbles.erase(it);
		else ++it;
	}
}

void MultiplayerChat::StartAvatarDownload(const std::string& user_id, const std::string& avatar_hash) {
	const std::string key = user_id + "/" + avatar_hash;
	{
		std::lock_guard lock(avatar_mutex);
		if (avatar_bitmaps.count(key) != 0 || !avatar_downloads.insert(key).second) return;
	}

	const std::string url = "https://cdn.discordapp.com/avatars/" + user_id + "/" + avatar_hash + ".png?size=32";
	std::thread([this, key, url]() {
		std::vector<uint8_t> data;
		CURL* curl = curl_easy_init();
		if (curl) {
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AvatarWriteCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "EasyRPG-Chaos-PLUS");
			long response_code = 0;
			const CURLcode result = curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
			curl_easy_cleanup(curl);
			if (result != CURLE_OK || response_code != 200) data.clear();
		}

		std::lock_guard lock(avatar_mutex);
		if (!data.empty()) avatar_ready.push_back({key, std::move(data)});
		else avatar_downloads.erase(key);
	}).detach();
}

void MultiplayerChat::ProcessAvatarDownloads() {
	std::vector<AvatarReady> ready;
	{
		std::lock_guard lock(avatar_mutex);
		ready.swap(avatar_ready);
	}
	for (auto& result : ready) {
		auto bitmap = Bitmap::Create(result.data.data(), static_cast<unsigned>(result.data.size()), true);
		std::lock_guard lock(avatar_mutex);
		if (bitmap) avatar_bitmaps[result.key] = bitmap;
		avatar_downloads.erase(result.key);
	}
}

void MultiplayerChat::UpdateAvatarSprites(const std::vector<uint16_t>& player_ids) {
	for (auto& [peer_id, sprite] : avatar_sprites) sprite->SetVisible(false);
	const int first_player_y = player_list_window->GetY() + 8 + 4 * 16;
	const int row_height = 16;

	for (size_t i = 0; i < player_ids.size(); ++i) {
		const uint16_t peer_id = player_ids[i];
		std::string user_id;
		std::string avatar_hash;
		if (peer_id == NetManager::Instance().GetLocalPeerId()) {
			user_id = DiscordIntegration::GetDiscordUserId();
			avatar_hash = DiscordIntegration::GetDiscordAvatarHash();
		} else if (auto* peer = NetManager::Instance().FindPeer(peer_id)) {
			user_id = peer->discord_user_id;
			avatar_hash = peer->discord_avatar_hash;
		}
		if (user_id.empty() || avatar_hash.empty()) continue;

		const std::string key = user_id + "/" + avatar_hash;
		auto bitmap_it = avatar_bitmaps.find(key);
		if (bitmap_it == avatar_bitmaps.end()) continue;

		auto& avatar = avatar_sprites[peer_id];
		if (!avatar) {
			avatar = std::make_unique<Sprite>();
			avatar->SetZ(Priority_Window + 211);
		}
		avatar->SetBitmap(bitmap_it->second);
		avatar->SetSrcRect(bitmap_it->second->GetRect());
		avatar->SetX(player_list_window->GetX() + 8);
		avatar->SetY(first_player_y + static_cast<int>(i) * row_height);
		avatar->SetZoomX(16.0 / std::max(1, bitmap_it->second->GetWidth()));
		avatar->SetZoomY(16.0 / std::max(1, bitmap_it->second->GetHeight()));
		avatar->SetVisible(true);
	}
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
