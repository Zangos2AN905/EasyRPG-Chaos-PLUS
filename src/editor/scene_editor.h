/*
 * Chaos Fork: In-Game Map & Event Editor using Dear ImGui
 */

#ifndef EP_EDITOR_SCENE_EDITOR_H
#define EP_EDITOR_SCENE_EDITOR_H

#include "scene.h"
#include <string>
#include <vector>
#include <unordered_map>

struct ImGuiContext;
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace Editor {

// When true, the editor handles its own rendering (skip normal Player::Draw)
extern bool editor_active;

// When true, editor should auto-open when Scene_Map starts
extern bool editor_requested;

class Scene_Editor : public Scene {
public:
	Scene_Editor();
	~Scene_Editor() override;

	void Start() override;
	void vUpdate() override;
	void Suspend(SceneType next_scene) override;

private:
	bool InitImGui();
	void ShutdownImGui();
	void ApplyGreenTheme();

	// Rendering
	void BeginFrame();
	void EndFrame();

	// UI Panels
	void DrawMenuBar();
	void DrawTilePalette();
	void DrawEventList();
	void DrawEventEditor();
	void DrawMapViewport();
	void DrawStatusBar();

	// Texture management
	void BuildMapTexture();
	void BuildChipsetTexture();
	void FreeTextures();
	SDL_Texture* GetEventTexture(const std::string& charset_name, int charset_index, int facing);

	// Map editing state
	int map_scroll_x = 0;
	int map_scroll_y = 0;
	int selected_tile_id = 0;
	int selected_layer = 0; // 0 = lower, 1 = upper
	bool show_lower_layer = true;
	bool show_upper_layer = true;
	bool show_events = true;
	bool show_grid = true;

	// Event editing state
	int selected_event_id = -1;
	int selected_event_page = 0;
	bool event_editor_open = false;

	// Edit buffers for event properties
	char event_name_buf[256] = {};
	int event_x = 0;
	int event_y = 0;

	// ImGui state
	bool imgui_initialized = false;
	SDL_Window* sdl_window = nullptr;
	SDL_Renderer* sdl_renderer = nullptr;

	// Tool selection
	enum class Tool { Pencil, Rectangle, Fill, Eraser, EventPlace };
	Tool current_tool = Tool::Pencil;

	// Cached textures
	SDL_Texture* map_lower_tex = nullptr;
	SDL_Texture* map_upper_tex = nullptr;
	SDL_Texture* chipset_tex = nullptr;
	int chipset_tex_w = 0;
	int chipset_tex_h = 0;
	int map_tex_w = 0;
	int map_tex_h = 0;
	int cached_map_id = -1;
	bool map_tex_dirty = true;

	// Event sprite texture cache: key = "charsetname:index:facing"
	std::unordered_map<std::string, SDL_Texture*> event_tex_cache;
};

} // namespace Editor

#endif
