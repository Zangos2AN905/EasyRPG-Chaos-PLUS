/*
 * EasyRPG Chaos Fork - RpgStore
 * A Wii Shop Channel-inspired game download store.
 */

#ifndef EP_SCENE_RPGSTORE_H
#define EP_SCENE_RPGSTORE_H

#include "scene.h"
#include "window_command.h"
#include "window_help.h"
#include "sprite.h"
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

class Scene_RpgStore : public Scene {
public:
	Scene_RpgStore();

	void Start() override;
	void vUpdate() override;
	void Suspend(SceneType next_scene) override;

private:
	struct GameEntry {
		std::string name;
		std::string url;
		std::string description;
	};

	void CreateBackground();
	void CreateWindows();
	void PlayStoreMusic();
	void StopStoreMusic();

	void StartDownload(int index);
	void UpdateDownloadProgress();
	void OnDownloadComplete();

	// curl download thread
	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
	static int ProgressCallback(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow);
	void DownloadThread(std::string url, std::string dest_path);
	void ExtractZip(const std::string& zip_path, const std::string& extract_dir);
	std::string GetGamesDirectory();

	// UI elements
	std::unique_ptr<Sprite> background;
	std::unique_ptr<Window_Help> title_window;
	std::unique_ptr<Window_Command> game_list_window;
	std::unique_ptr<Window_Help> info_window;
	std::unique_ptr<Window_Help> status_window;

	// Game catalog
	std::vector<GameEntry> games;

	// Download state
	std::atomic<bool> downloading{false};
	std::atomic<bool> download_finished{false};
	std::atomic<bool> download_success{false};
	std::atomic<int> download_percent{0};
	std::mutex status_mutex;
	std::string status_text;
	std::string download_error;
	std::unique_ptr<std::thread> download_thread;
	int downloading_index = -1;
};

#endif
