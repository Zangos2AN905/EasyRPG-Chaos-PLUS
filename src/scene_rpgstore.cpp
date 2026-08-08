/*
 * EasyRPG Chaos Fork - RpgStore
 * A Wii Shop Channel-inspired game download store.
 */

#include "scene_rpgstore.h"
#include "audio.h"
#include "bitmap.h"
#include "cache.h"
#include "color.h"
#include "filefinder.h"
#include "font.h"
#include "game_system.h"
#include "input.h"
#include "output.h"
#include "player.h"
#include "text.h"
#include "transition.h"

#include <curl/curl.h>
#include <fstream>
#include <filesystem>

Scene_RpgStore::Scene_RpgStore() {
	type = Scene::RpgStore;

	// Populate the game catalog
	games.push_back({
		"The Gray Garden",
		"https://vgperson.com/games/TheGrayGarden108.zip",
		"By Mogeko. A garden where gray is the norm."
	});
	games.push_back({
		"Wadanohara",
		"https://vgperson.com/games/Wadanohara104.zip",
		"By Mogeko. The great blue sea and its witch."
	});
	games.push_back({
		"More coming soon!",
		"",
		"Stay tuned for more games."
	});
}

void Scene_RpgStore::Start() {
	CreateBackground();
	CreateWindows();
	PlayStoreMusic();
}

void Scene_RpgStore::Suspend(SceneType /* next_scene */) {
	StopStoreMusic();
}

void Scene_RpgStore::vUpdate() {
	game_list_window->Update();

	// Update download progress display
	if (downloading) {
		UpdateDownloadProgress();
		return; // Block input while downloading
	}

	// Handle download completion
	if (download_finished) {
		OnDownloadComplete();
		return;
	}

	// Update info window based on selection
	int idx = game_list_window->GetIndex();
	if (idx >= 0 && idx < static_cast<int>(games.size())) {
		info_window->SetText(games[idx].description);
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
	} else if (Input::IsTriggered(Input::DECISION)) {
		int index = game_list_window->GetIndex();
		if (index >= 0 && index < static_cast<int>(games.size()) && !games[index].url.empty()) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			StartDownload(index);
		} else {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
		}
	}
}

void Scene_RpgStore::CreateBackground() {
	// Create a blue gradient background (Wii Shop Channel style)
	auto bg_bitmap = Bitmap::Create(Player::screen_width, Player::screen_height, false);

	for (int y = 0; y < Player::screen_height; ++y) {
		float t = static_cast<float>(y) / Player::screen_height;
		// Light blue at top → deeper blue at bottom
		uint8_t r = static_cast<uint8_t>(200 - t * 80);
		uint8_t g = static_cast<uint8_t>(220 - t * 60);
		uint8_t b = static_cast<uint8_t>(255 - t * 30);
		bg_bitmap->FillRect(Rect(0, y, Player::screen_width, 1), Color(r, g, b, 255));
	}

	// Draw title text on the background
	std::string title_text = "Welcome to the RpgStore";
	auto text_size = Text::GetSize(*Font::Default(), title_text);
	int title_x = (Player::screen_width - text_size.width) / 2;
	bg_bitmap->TextDraw(title_x, 8, Color(255, 255, 255, 255), title_text);

	background = std::make_unique<Sprite>();
	background->SetBitmap(bg_bitmap);
	background->SetZ(Priority_Background);
}

void Scene_RpgStore::CreateWindows() {
	// Game list command window (centered below title)
	std::vector<std::string> options;
	for (auto& game : games) {
		options.push_back(game.name);
	}

	game_list_window = std::make_unique<Window_Command>(options, Player::screen_width - 40);
	game_list_window->SetX(20);
	game_list_window->SetY(32);

	// Disable "More coming soon" entry
	for (int i = 0; i < static_cast<int>(games.size()); ++i) {
		if (games[i].url.empty()) {
			game_list_window->DisableItem(i);
		}
	}

	// Info window at the bottom (shows description)
	info_window = std::make_unique<Window_Help>(
		0, Player::screen_height - 48, Player::screen_width, 32);
	info_window->SetText(games[0].description);

	// Status window (hidden by default, shows during downloads)
	status_window = std::make_unique<Window_Help>(
		Player::screen_width / 4, Player::screen_height / 2 - 16,
		Player::screen_width / 2, 32);
	status_window->SetText("");
	status_window->SetVisible(false);
}

void Scene_RpgStore::PlayStoreMusic() {
	// Play GameShop.wav from chaos assets
	auto chaos_fs = FileFinder::ChaosAssets();
	if (!chaos_fs) {
		Output::Debug("RpgStore: ChaosAssets not available for music");
		return;
	}

	DirectoryTree::Args args = { FileFinder::MakePath("Music", "GameShop"), FileFinder::MUSIC_TYPES, 1, false };
	auto stream = chaos_fs.OpenFile(args);
	if (!stream) {
		Output::Debug("RpgStore: Could not find Music/GameShop");
		return;
	}

	Audio().BGM_Stop();
	Audio().BGM_Play(std::move(stream), 100, 100, 0, 50);

	Output::Debug("RpgStore: Playing store music");
}

void Scene_RpgStore::StopStoreMusic() {
	Audio().BGM_Stop();
}

std::string Scene_RpgStore::GetGamesDirectory() {
	// Get the directory next to the executable
#ifdef _WIN32
	wchar_t exe_path[MAX_PATH];
	GetModuleFileNameW(NULL, exe_path, MAX_PATH);
	std::wstring wpath(exe_path);
	size_t pos = wpath.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		wpath = wpath.substr(0, pos);
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
	std::string exe_dir(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &exe_dir[0], len, NULL, NULL);
	return exe_dir;
#else
	return Main_Data::GetDefaultProjectPath();
#endif
}

void Scene_RpgStore::StartDownload(int index) {
	if (downloading || index < 0 || index >= static_cast<int>(games.size())) return;

	downloading_index = index;
	downloading = true;
	download_finished = false;
	download_success = false;
	download_percent = 0;

	// Show status
	status_window->SetVisible(true);
	status_window->SetText("Connecting...");
	game_list_window->SetActive(false);

	// Build destination path
	std::string games_dir = GetGamesDirectory();
	// Extract filename from URL
	std::string url = games[index].url;
	std::string filename = url.substr(url.find_last_of('/') + 1);
	std::string dest_path = games_dir + "/" + filename;

	Output::Debug("RpgStore: Downloading {} to {}", url, dest_path);

	// Launch download in background thread
	download_thread = std::make_unique<std::thread>(&Scene_RpgStore::DownloadThread, this, url, dest_path);
}

size_t Scene_RpgStore::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	auto* ofs = static_cast<std::ofstream*>(userp);
	size_t total = size * nmemb;
	ofs->write(static_cast<const char*>(contents), total);
	return total;
}

int Scene_RpgStore::ProgressCallback(void* clientp, double dltotal, double dlnow, double /*ultotal*/, double /*ulnow*/) {
	auto* self = static_cast<Scene_RpgStore*>(clientp);
	if (dltotal > 0) {
		int pct = static_cast<int>((dlnow / dltotal) * 100.0);
		self->download_percent = pct;
	}
	return 0;
}

void Scene_RpgStore::DownloadThread(std::string url, std::string dest_path) {
	CURL* curl = curl_easy_init();
	if (!curl) {
		{
			std::lock_guard<std::mutex> lock(status_mutex);
			download_error = "Failed to initialize download";
		}
		download_success = false;
		download_finished = true;
		return;
	}

	std::ofstream ofs(dest_path, std::ios::binary);
	if (!ofs.is_open()) {
		{
			std::lock_guard<std::mutex> lock(status_mutex);
			download_error = "Cannot write to: " + dest_path;
		}
		curl_easy_cleanup(curl);
		download_success = false;
		download_finished = true;
		return;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, ProgressCallback);
	curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, this);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
	// Use a reasonable user-agent
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "EasyRPG-ChaosPlayer/1.0");

	CURLcode res = curl_easy_perform(curl);
	ofs.close();

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK || (http_code != 200 && http_code != 0)) {
		// Clean up partial download
		std::error_code ec;
		std::filesystem::remove(dest_path, ec);
		{
			std::lock_guard<std::mutex> lock(status_mutex);
			if (res != CURLE_OK) {
				download_error = std::string("Download failed: ") + curl_easy_strerror(res);
			} else {
				download_error = "Server error (HTTP " + std::to_string(http_code) + ")";
			}
		}
		download_success = false;
	} else {
		download_success = true;
	}

	download_finished = true;
}

void Scene_RpgStore::UpdateDownloadProgress() {
	int pct = download_percent.load();
	std::string game_name = (downloading_index >= 0 && downloading_index < static_cast<int>(games.size()))
		? games[downloading_index].name : "game";

	if (pct > 0) {
		status_window->SetText(fmt::format("Downloading {}... {}%", game_name, pct));
	} else {
		status_window->SetText(fmt::format("Downloading {}...", game_name));
	}
}

void Scene_RpgStore::OnDownloadComplete() {
	download_finished = false;
	downloading = false;

	// Join thread
	if (download_thread && download_thread->joinable()) {
		download_thread->join();
	}
	download_thread.reset();

	if (download_success) {
		std::string game_name = (downloading_index >= 0 && downloading_index < static_cast<int>(games.size()))
			? games[downloading_index].name : "game";
		status_window->SetText(fmt::format("{} downloaded!", game_name));
		Output::Debug("RpgStore: Download complete for {}", game_name);
	} else {
		std::lock_guard<std::mutex> lock(status_mutex);
		status_window->SetText(download_error.empty() ? "Download failed!" : download_error);
		Output::Warning("RpgStore: Download failed: {}", download_error);
	}

	game_list_window->SetActive(true);
	downloading_index = -1;

	// Status window stays visible briefly — next input will hide it
}
