/*
 * Chaos Fork: Multiplayer radio scene.
 */

#include "chaos/scene_radio.h"
#include "chaos/multiplayer_radio.h"
#include "game_system.h"
#include "input.h"
#include "main_data.h"
#include "player.h"
#include <algorithm>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace Chaos {

Scene_Radio::Scene_Radio() {
	type = Scene::Radio;
}

void Scene_Radio::Start() {
	MultiplayerRadio::Instance().RefreshAvailableTracks();
	CreateWindows();
	RefreshList();
	RefreshQueue();
}

void Scene_Radio::CreateWindows() {
	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	help_window->SetText("Radio - select a song to add it to the queue");

	queue_window = std::make_unique<Window_Help>(0, 32, Player::screen_width, 48);
	queue_window->SetBackOpacity(160);

	const auto count = static_cast<int>(MultiplayerRadio::Instance().GetAvailableTracks().size()) + 1;
	list_window = std::make_unique<Window_Command>(std::vector<std::string>(count), Player::screen_width);
	list_window->SetX(0);
	list_window->SetY(80);
	list_window->SetHeight(std::max(48, Player::screen_height - 80));
}

void Scene_Radio::RefreshList() {
	list_window->SetItemText(0, "<Add custom music>");
	const auto& tracks = MultiplayerRadio::Instance().GetAvailableTracks();
	for (size_t i = 0; i < tracks.size(); ++i) {
		list_window->SetItemText(static_cast<int>(i + 1), tracks[i].name);
	}
}

void Scene_Radio::RefreshQueue() {
	const auto& radio = MultiplayerRadio::Instance();
	const auto& tracks = radio.GetQueue();
	if (tracks.empty()) {
		queue_window->SetText("Queue empty");
		return;
	}
	std::string text = "Playing: " + tracks.front().name;
	if (tracks.size() > 1) {
		text += " | Next: ";
		for (size_t i = 1; i < tracks.size(); ++i) {
			if (i > 1) text += ", ";
			text += tracks[i].name;
		}
	}
	queue_window->SetText(text);
}

bool Scene_Radio::SelectCustomMusicFile(std::string& path) const {
#ifdef _WIN32
	wchar_t filename[MAX_PATH] = {};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.lpstrFile = filename;
	dialog.nMaxFile = MAX_PATH;
	dialog.lpstrFilter = L"Audio Files\0*.mp3;*.ogg;*.wav;*.mid;*.midi\0MP3\0*.mp3\0OGG\0*.ogg\0WAV\0*.wav\0MIDI\0*.mid;*.midi\0\0";
	dialog.nFilterIndex = 1;
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog)) return false;

	const int size = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
	if (size <= 1) return false;
	std::vector<char> utf8(static_cast<size_t>(size));
	WideCharToMultiByte(CP_UTF8, 0, filename, -1, utf8.data(), size, nullptr, nullptr);
	path.assign(utf8.data(), static_cast<size_t>(size - 1));
	return true;
#else
	(void)path;
	return false;
#endif
}

void Scene_Radio::vUpdate() {
	list_window->Update();
	RefreshQueue();

	if (Input::IsTriggered(Input::CANCEL) || Input::IsRawKeyTriggered(Input::Keys::ENDS)) {
		Scene::Pop();
		return;
	}

	if (!Input::IsTriggered(Input::DECISION)) return;
	Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));

	const int index = list_window->GetIndex();
	if (index == 0) {
		std::string path;
		if (!SelectCustomMusicFile(path)) return;
		if (!MultiplayerRadio::Instance().SubmitCustomMusic(path)) {
			help_window->SetText("Could not import music. MP3, OGG, WAV and MIDI files must be under 15 MiB.");
		} else {
			help_window->SetText("Music submitted to the radio queue.");
		}
		return;
	}

	if (MultiplayerRadio::Instance().SubmitGameTrack(static_cast<size_t>(index - 1))) {
		help_window->SetText("Music submitted to the radio queue.");
		RefreshQueue();
	}
}

} // namespace Chaos
