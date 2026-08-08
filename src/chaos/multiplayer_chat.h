/*
 * Chaos Fork: Multiplayer Chat System
 * Normal chat (T key) and Dialogue chat (Y key).
 */

#ifndef EP_CHAOS_MULTIPLAYER_CHAT_H
#define EP_CHAOS_MULTIPLAYER_CHAT_H

#include <deque>
#include <memory>
#include <string>
#include <cstdint>

class Window_Help;

namespace Chaos { class Window_ChatDialogue; }

namespace Chaos {

/** Moderation strictness level. */
enum class ModerationLevel {
	None = 0,
	Basic,
	Moderate,
	Strict
};

/** A single chat entry shown on screen. */
struct ChatEntry {
	std::string sender;
	std::string text;
	int timer = 0; // frames remaining
};

/**
 * Multiplayer chat manager — singleton.
 *
 * Normal chat (T):  Shows text overlay at top of screen.
 * Dialogue chat (Y): Shows an RPG-style dialogue box, only in TeamParty/Chaotix.
 */
class MultiplayerChat {
public:
	static MultiplayerChat& Instance();

	// ---- Lobby settings -----------------------------------------------
	bool IsChatEnabled() const { return chat_enabled; }
	void SetChatEnabled(bool v) { chat_enabled = v; }

	ModerationLevel GetModerationLevel() const { return moderation; }
	void SetModerationLevel(ModerationLevel v) { moderation = v; }

	// ---- Input / text entry -------------------------------------------
	/** Call every frame from Scene_Map. Returns true while chat input is open. */
	bool Update();

	/** Whether the chat input box is currently active (blocks game input). */
	bool IsInputActive() const { return input_active; }

	// ---- Receiving messages -------------------------------------------
	/** Called by multiplayer_state when a ChatMessage packet arrives. */
	void OnChatMessageReceived(uint16_t sender_peer_id, const std::string& sender_name,
							   const std::string& message, bool is_dialogue);

	// ---- Sending ------------------------------------------------------
	/** Broadcast a chat message to all players via network. */
	void SendNormalChat(const std::string& text);
	void SendDialogueChat(const std::string& text);

	// ---- Rendering helpers --------------------------------------------
	/** Create / destroy the overlay windows; called when entering / leaving a map scene. */
	void OnMapLoaded();
	void OnMapUnloaded();

	/** Reset all state (on disconnect, etc.). */
	void Reset();

private:
	MultiplayerChat() = default;

	// Text entry
	void OpenInput(bool dialogue_mode);
	void CloseInput();
	void UpdateInput();
	void UpdateOverlay();
	void UpdateDialogue();
	void RefreshOverlay();
	void ShowDialogue(const std::string& sender, const std::string& text);

	// Moderation
	bool PassesModeration(const std::string& text) const;
	std::string FilterText(const std::string& text) const;

	// Settings
	bool chat_enabled = true;
	ModerationLevel moderation = ModerationLevel::Basic;

	// Input state
	bool input_active = false;
	bool input_dialogue_mode = false;
	std::string input_buffer;
	int input_cursor = 0;

	// Normal chat overlay (top of screen)
	static constexpr int MAX_OVERLAY_LINES = 4;
	static constexpr int OVERLAY_DISPLAY_FRAMES = 300; // ~5 seconds at 60fps
	std::deque<ChatEntry> overlay_entries;
	std::unique_ptr<Window_Help> overlay_window;
	std::unique_ptr<Window_Help> input_window;

	// Dialogue chat — queued custom windows
	static constexpr int DIALOGUE_DISPLAY_FRAMES = 180; // 3 seconds at 60fps
	struct DialogueQueueEntry {
		std::string sender;
		std::string text;
	};
	std::deque<DialogueQueueEntry> dialogue_queue;
	std::unique_ptr<Window_ChatDialogue> dialogue_window;

	// Character set for cycling in input
	static const std::string input_chars;
};

} // namespace Chaos

#endif
