/*
 * Chaos Fork: In-Game Map & Event Editor using Dear ImGui
 */

#include "editor/scene_editor.h"
#include "baseui.h"
#include "input.h"
#include "output.h"
#include "player.h"
#include "game_map.h"
#include "game_event.h"
#include "main_data.h"
#include "game_system.h"
#include "game_party.h"
#include "game_character.h"
#include "cache.h"
#include "bitmap.h"
#include "options.h"
#include "map_data.h"
#include <lcf/data.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <fmt/format.h>

namespace Editor {

bool editor_active = false;
bool editor_requested = false;

// Original resolution before editor opened
static int original_width = 0;
static int original_height = 0;

static constexpr int EDITOR_WIDTH = 848;
static constexpr int EDITOR_HEIGHT = 480;

Scene_Editor::Scene_Editor() {
	type = Scene::Editor;
}

Scene_Editor::~Scene_Editor() {
	editor_active = false;
	if (DisplayUi) {
		DisplayUi->sdl_event_forwarder = nullptr;
	}
	FreeTextures();
	ShutdownImGui();
}

void Scene_Editor::Start() {
	editor_active = true;

	if (!InitImGui()) {
		Output::Warning("Editor: Failed to initialize ImGui");
		editor_active = false;
		Scene::Pop();
		return;
	}

	// Save original window size and resize to editor size
	SDL_GetWindowSize(sdl_window, &original_width, &original_height);
	SDL_SetWindowSize(sdl_window, EDITOR_WIDTH, EDITOR_HEIGHT);

	// Forward SDL events to ImGui
	DisplayUi->sdl_event_forwarder = [](void* ev) {
		ImGui_ImplSDL2_ProcessEvent(static_cast<SDL_Event*>(ev));
	};

	Output::Debug("Editor: Started ({}x{})", EDITOR_WIDTH, EDITOR_HEIGHT);
}

void Scene_Editor::Suspend(SceneType /*next_scene*/) {
	editor_active = false;
	DisplayUi->sdl_event_forwarder = nullptr;
	// Restore original window size
	if (sdl_window && original_width > 0 && original_height > 0) {
		SDL_SetWindowSize(sdl_window, original_width, original_height);
	}
	FreeTextures();
	ShutdownImGui();
}

bool Scene_Editor::InitImGui() {
	if (imgui_initialized) return true;

	sdl_window = static_cast<SDL_Window*>(DisplayUi->GetSDLWindow());
	sdl_renderer = static_cast<SDL_Renderer*>(DisplayUi->GetSDLRenderer());

	if (!sdl_window || !sdl_renderer) {
		Output::Warning("Editor: SDL2 window/renderer not available");
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ApplyGreenTheme();

	ImGui_ImplSDL2_InitForSDLRenderer(sdl_window, sdl_renderer);
	ImGui_ImplSDLRenderer2_Init(sdl_renderer);

	imgui_initialized = true;
	return true;
}

void Scene_Editor::ShutdownImGui() {
	if (!imgui_initialized) return;

	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	imgui_initialized = false;
	sdl_window = nullptr;
	sdl_renderer = nullptr;
}

void Scene_Editor::ApplyGreenTheme() {
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	// EasyRPG-inspired green theme
	ImVec4 bg_dark    = ImVec4(0.08f, 0.14f, 0.08f, 1.00f);
	ImVec4 bg_mid     = ImVec4(0.12f, 0.20f, 0.12f, 1.00f);
	ImVec4 bg_light   = ImVec4(0.16f, 0.26f, 0.16f, 1.00f);
	ImVec4 accent     = ImVec4(0.20f, 0.55f, 0.20f, 1.00f);
	ImVec4 accent_hi  = ImVec4(0.30f, 0.70f, 0.30f, 1.00f);
	ImVec4 accent_act = ImVec4(0.15f, 0.45f, 0.15f, 1.00f);
	ImVec4 text       = ImVec4(0.85f, 0.95f, 0.85f, 1.00f);
	ImVec4 text_dim   = ImVec4(0.55f, 0.70f, 0.55f, 1.00f);
	ImVec4 border     = ImVec4(0.25f, 0.40f, 0.25f, 0.60f);

	colors[ImGuiCol_Text]                  = text;
	colors[ImGuiCol_TextDisabled]          = text_dim;
	colors[ImGuiCol_WindowBg]              = bg_mid;
	colors[ImGuiCol_ChildBg]               = bg_dark;
	colors[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.18f, 0.10f, 0.95f);
	colors[ImGuiCol_Border]                = border;
	colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg]               = bg_light;
	colors[ImGuiCol_FrameBgHovered]        = accent;
	colors[ImGuiCol_FrameBgActive]         = accent_act;
	colors[ImGuiCol_TitleBg]               = bg_dark;
	colors[ImGuiCol_TitleBgActive]         = accent_act;
	colors[ImGuiCol_TitleBgCollapsed]      = bg_dark;
	colors[ImGuiCol_MenuBarBg]             = bg_dark;
	colors[ImGuiCol_ScrollbarBg]           = bg_dark;
	colors[ImGuiCol_ScrollbarGrab]         = accent;
	colors[ImGuiCol_ScrollbarGrabHovered]  = accent_hi;
	colors[ImGuiCol_ScrollbarGrabActive]   = accent_act;
	colors[ImGuiCol_CheckMark]             = accent_hi;
	colors[ImGuiCol_SliderGrab]            = accent;
	colors[ImGuiCol_SliderGrabActive]      = accent_hi;
	colors[ImGuiCol_Button]                = accent;
	colors[ImGuiCol_ButtonHovered]         = accent_hi;
	colors[ImGuiCol_ButtonActive]          = accent_act;
	colors[ImGuiCol_Header]               = accent;
	colors[ImGuiCol_HeaderHovered]         = accent_hi;
	colors[ImGuiCol_HeaderActive]          = accent_act;
	colors[ImGuiCol_Separator]             = border;
	colors[ImGuiCol_SeparatorHovered]      = accent_hi;
	colors[ImGuiCol_SeparatorActive]       = accent_act;
	colors[ImGuiCol_ResizeGrip]            = accent;
	colors[ImGuiCol_ResizeGripHovered]     = accent_hi;
	colors[ImGuiCol_ResizeGripActive]      = accent_act;
	colors[ImGuiCol_Tab]                   = accent_act;
	colors[ImGuiCol_TabHovered]            = accent_hi;
	colors[ImGuiCol_TabActive]             = accent;
	colors[ImGuiCol_TabUnfocused]          = bg_dark;
	colors[ImGuiCol_TabUnfocusedActive]    = accent_act;
	colors[ImGuiCol_TableHeaderBg]         = bg_dark;
	colors[ImGuiCol_TableBorderStrong]     = border;
	colors[ImGuiCol_TableBorderLight]      = ImVec4(border.x, border.y, border.z, 0.30f);
	colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);

	// Round corners
	style.WindowRounding    = 4.0f;
	style.ChildRounding     = 2.0f;
	style.FrameRounding     = 3.0f;
	style.PopupRounding     = 3.0f;
	style.ScrollbarRounding = 3.0f;
	style.GrabRounding      = 2.0f;
	style.TabRounding       = 3.0f;

	style.WindowPadding     = ImVec2(6, 6);
	style.FramePadding      = ImVec2(4, 2);
	style.ItemSpacing       = ImVec2(6, 4);
}

void Scene_Editor::BeginFrame() {
	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
}

void Scene_Editor::EndFrame() {
	ImGui::Render();
	SDL_SetRenderDrawColor(sdl_renderer, 20, 36, 20, 255);
	SDL_RenderClear(sdl_renderer);
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
	SDL_RenderPresent(sdl_renderer);
}

// Helper: create SDL_Texture from a Bitmap using the renderer's native format
static SDL_Texture* BitmapToTexture(SDL_Renderer* renderer, const Bitmap& bmp) {
	// Use SDL_PIXELFORMAT_RGBA32 which matches Bitmap's pixel_format on the current platform
	SDL_Texture* tex = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
		bmp.width(), bmp.height());
	if (!tex) return nullptr;

	void* px;
	int pitch;
	if (SDL_LockTexture(tex, nullptr, &px, &pitch) == 0) {
		const uint8_t* src = reinterpret_cast<const uint8_t*>(bmp.pixels());
		int src_pitch = bmp.pitch();
		int row_bytes = std::min(pitch, src_pitch);
		for (int y = 0; y < bmp.height(); y++) {
			std::memcpy(
				static_cast<uint8_t*>(px) + y * pitch,
				src + y * src_pitch,
				row_bytes);
		}
		SDL_UnlockTexture(tex);
	}

	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

void Scene_Editor::FreeTextures() {
	if (map_lower_tex) { SDL_DestroyTexture(map_lower_tex); map_lower_tex = nullptr; }
	if (map_upper_tex) { SDL_DestroyTexture(map_upper_tex); map_upper_tex = nullptr; }
	if (chipset_tex) { SDL_DestroyTexture(chipset_tex); chipset_tex = nullptr; }
	for (auto& [key, tex] : event_tex_cache) {
		if (tex) SDL_DestroyTexture(tex);
	}
	event_tex_cache.clear();
	cached_map_id = -1;
	map_tex_dirty = true;
}

void Scene_Editor::BuildChipsetTexture() {
	if (chipset_tex) { SDL_DestroyTexture(chipset_tex); chipset_tex = nullptr; }

	auto chipset_name = Game_Map::GetChipsetName();
	if (chipset_name.empty()) return;

	BitmapRef chipset_bmp = Cache::Chipset(std::string(chipset_name));
	if (!chipset_bmp) return;

	chipset_tex_w = chipset_bmp->width();
	chipset_tex_h = chipset_bmp->height();
	chipset_tex = BitmapToTexture(sdl_renderer, *chipset_bmp);
}

void Scene_Editor::BuildMapTexture() {
	int map_w = Game_Map::GetTilesX();
	int map_h = Game_Map::GetTilesY();
	if (map_w <= 0 || map_h <= 0) return;

	auto chipset_name = Game_Map::GetChipsetName();
	if (chipset_name.empty()) return;

	BitmapRef chipset_bmp = Cache::Chipset(std::string(chipset_name));
	if (!chipset_bmp) return;

	int tex_w = map_w * TILE_SIZE;
	int tex_h = map_h * TILE_SIZE;

	// Helper: compute chipset source rect for a raw chip_id (lower layer)
	auto GetLowerTileRect = [](int chip_id) -> Rect {
		if (chip_id >= BLOCK_E && chip_id < BLOCK_E_END) {
			// Regular lower tiles (BLOCK_E)
			int id = chip_id - BLOCK_E;
			int col, row;
			if (id < 96) {
				col = 12 + id % 6;
				row = id / 6;
			} else {
				col = 18 + (id - 96) % 6;
				row = (id - 96) / 6;
			}
			return Rect(col * TILE_SIZE, row * TILE_SIZE, TILE_SIZE, TILE_SIZE);
		} else if (chip_id >= BLOCK_C && chip_id < BLOCK_C_END) {
			// Animated tiles (BLOCK_C) — show first frame
			int idx = (chip_id - BLOCK_C) / BLOCK_C_STRIDE;
			int col = 3 + idx;
			int row = 4;
			return Rect(col * TILE_SIZE, row * TILE_SIZE, TILE_SIZE, TILE_SIZE);
		} else if (chip_id >= BLOCK_D && chip_id < BLOCK_D_END) {
			// Terrain autotiles (BLOCK_D) — show center piece of the block
			int block = (chip_id - BLOCK_D) / BLOCK_D_STRIDE;
			// Block positions in half-tile coords (same as tilemap_layer.cpp)
			int block_x, block_y;
			if (block < 4) {
				block_x = (block % 2) * 3;
				block_y = 8 + (block / 2) * 4;
			} else {
				block_x = 6 + (block % 2) * 3;
				block_y = ((block - 4) / 2) * 4;
			}
			// Take a representative tile from center of the autotile source
			int px = (block_x + 1) * (TILE_SIZE / 2);
			int py = (block_y + 1) * (TILE_SIZE / 2);
			return Rect(px, py, TILE_SIZE, TILE_SIZE);
		} else if (chip_id < BLOCK_C) {
			// Water/terrain autotiles (BLOCK_A/B)
			int block = chip_id / BLOCK_A_STRIDE;
			// Show representative water tile from chipset (first animation frame, center area)
			// Block 0 (A1): source at cols 0-2, rows 0-3 → take (1,1) in half-tile coords
			// Block 1 (A2): source at cols 3-5, rows 0-3 → take (4,1)
			// Block 2 (B):  source at cols 0-2, rows 4-7 → take (1,5)
			int hx, hy;
			if (block == 0) { hx = 1; hy = 1; }
			else if (block == 1) { hx = 4; hy = 1; }
			else { hx = 1; hy = 5; }
			int px = hx * (TILE_SIZE / 2);
			int py = hy * (TILE_SIZE / 2);
			return Rect(px, py, TILE_SIZE, TILE_SIZE);
		}
		return Rect(0, 0, 0, 0);
	};

	// Helper: compute chipset source rect for a raw chip_id (upper layer)
	auto GetUpperTileRect = [](int chip_id) -> Rect {
		if (chip_id >= BLOCK_F && chip_id < BLOCK_F_END) {
			int id = chip_id - BLOCK_F;
			int col, row;
			if (id < 48) {
				col = 18 + id % 6;
				row = 8 + id / 6;
			} else {
				col = 24 + (id - 48) % 6;
				row = (id - 48) / 6;
			}
			return Rect(col * TILE_SIZE, row * TILE_SIZE, TILE_SIZE, TILE_SIZE);
		}
		return Rect(0, 0, 0, 0);
	};

	// Build lower layer bitmap
	{
		BitmapRef map_bmp = Bitmap::Create(tex_w, tex_h, true);
		map_bmp->Clear();
		for (int y = 0; y < map_h; y++) {
			for (int x = 0; x < map_w; x++) {
				int chip_id = Game_Map::GetTileIdAt(x, y, 0, true);
				Rect src = GetLowerTileRect(chip_id);
				if (src.width > 0) {
					map_bmp->Blit(x * TILE_SIZE, y * TILE_SIZE, *chipset_bmp, src, Opacity::Opaque());
				}
			}
		}
		if (map_lower_tex) SDL_DestroyTexture(map_lower_tex);
		map_lower_tex = BitmapToTexture(sdl_renderer, *map_bmp);
	}

	// Build upper layer bitmap
	{
		BitmapRef map_bmp = Bitmap::Create(tex_w, tex_h, true);
		map_bmp->Clear();
		for (int y = 0; y < map_h; y++) {
			for (int x = 0; x < map_w; x++) {
				int chip_id = Game_Map::GetTileIdAt(x, y, 1, true);
				if (chip_id >= BLOCK_F) {
					Rect src = GetUpperTileRect(chip_id);
					if (src.width > 0) {
						map_bmp->Blit(x * TILE_SIZE, y * TILE_SIZE, *chipset_bmp, src, Opacity::Opaque());
					}
				}
			}
		}
		if (map_upper_tex) SDL_DestroyTexture(map_upper_tex);
		map_upper_tex = BitmapToTexture(sdl_renderer, *map_bmp);
	}

	map_tex_w = tex_w;
	map_tex_h = tex_h;
	cached_map_id = Game_Map::GetMapId();
	map_tex_dirty = false;
}

SDL_Texture* Scene_Editor::GetEventTexture(const std::string& charset_name, int charset_index, int facing) {
	std::string key = fmt::format("{}:{}:{}", charset_name, charset_index, facing);
	auto it = event_tex_cache.find(key);
	if (it != event_tex_cache.end()) return it->second;

	BitmapRef charset_bmp = Cache::Charset(charset_name);
	if (!charset_bmp) return nullptr;

	// Calculate character rect within charset (same logic as Sprite_Character)
	Rect char_rect;
	char_rect.width = 24 * (TILE_SIZE / 16) * 3;
	char_rect.height = 32 * (TILE_SIZE / 16) * 4;
	if (!charset_name.empty() && charset_name.front() == '$' && Player::HasEasyRpgExtensions()) {
		char_rect.width = charset_bmp->width() * (TILE_SIZE / 16) / 4;
		char_rect.height = charset_bmp->height() * (TILE_SIZE / 16) / 2;
	}
	char_rect.x = (charset_index % 4) * char_rect.width;
	char_rect.y = (charset_index / 4) * char_rect.height;

	int frame_w = char_rect.width / 3;
	int frame_h = char_rect.height / 4;

	// Use middle frame (frame 1), specified facing
	int frame_x = char_rect.x + 1 * frame_w;  // middle animation frame
	int frame_y = char_rect.y + facing * frame_h;

	Rect src_rect(frame_x, frame_y, frame_w, frame_h);
	BitmapRef frame_bmp = Bitmap::Create(*charset_bmp, src_rect);
	if (!frame_bmp) return nullptr;

	SDL_Texture* tex = BitmapToTexture(sdl_renderer, *frame_bmp);
	event_tex_cache[key] = tex;
	return tex;
}

void Scene_Editor::vUpdate() {
	if (!imgui_initialized) {
		Scene::Pop();
		return;
	}

	// Check for 7 key to exit editor (same key that opens it)
	if (Input::IsTriggered(Input::N7)) {
		Scene::Pop();
		return;
	}

	// Rebuild textures if map changed or first frame
	if (map_tex_dirty || cached_map_id != Game_Map::GetMapId()) {
		BuildChipsetTexture();
		BuildMapTexture();
	}

	BeginFrame();

	// Full-screen dockspace
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);

	ImGui::Begin("##EditorMain", nullptr, window_flags);

	DrawMenuBar();

	// Layout: Left panel (tile palette) | Center (map) | Right panel (events)
	float left_panel_width = 180.0f;
	float right_panel_width = 240.0f;
	float center_width = ImGui::GetContentRegionAvail().x - left_panel_width - right_panel_width - 12.0f;
	float content_height = ImGui::GetContentRegionAvail().y - 24.0f; // Reserve for status bar

	// Left panel: Tile Palette
	ImGui::BeginChild("##TilePalette", ImVec2(left_panel_width, content_height), true);
	DrawTilePalette();
	ImGui::EndChild();

	ImGui::SameLine();

	// Center: Map Viewport
	ImGui::BeginChild("##MapView", ImVec2(center_width, content_height), true,
		ImGuiWindowFlags_HorizontalScrollbar);
	DrawMapViewport();
	ImGui::EndChild();

	ImGui::SameLine();

	// Right panel: Events
	ImGui::BeginChild("##EventPanel", ImVec2(right_panel_width, content_height), true);
	if (event_editor_open && selected_event_id >= 0) {
		DrawEventEditor();
	} else {
		DrawEventList();
	}
	ImGui::EndChild();

	// Status bar
	DrawStatusBar();

	ImGui::End();

	EndFrame();
}

void Scene_Editor::DrawMenuBar() {
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Save Map", "Ctrl+S")) {
				Output::Debug("Editor: Save not yet implemented");
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit Editor", "7")) {
				Scene::Pop();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Lower Layer", nullptr, &show_lower_layer);
			ImGui::MenuItem("Upper Layer", nullptr, &show_upper_layer);
			ImGui::MenuItem("Events", nullptr, &show_events);
			ImGui::MenuItem("Grid", nullptr, &show_grid);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools")) {
			if (ImGui::MenuItem("Pencil", "1", current_tool == Tool::Pencil))
				current_tool = Tool::Pencil;
			if (ImGui::MenuItem("Rectangle", "2", current_tool == Tool::Rectangle))
				current_tool = Tool::Rectangle;
			if (ImGui::MenuItem("Fill", "3", current_tool == Tool::Fill))
				current_tool = Tool::Fill;
			if (ImGui::MenuItem("Eraser", "4", current_tool == Tool::Eraser))
				current_tool = Tool::Eraser;
			if (ImGui::MenuItem("Place Event", "5", current_tool == Tool::EventPlace))
				current_tool = Tool::EventPlace;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Map")) {
			if (ImGui::BeginMenu("Go to Map")) {
				// List available maps from the tree
				for (auto& map_info : lcf::Data::treemap.maps) {
					if (map_info.type == lcf::rpg::TreeMap::MapType_map) {
						std::string label = fmt::format("{}: {}", map_info.ID, map_info.name);
						if (ImGui::MenuItem(label.c_str())) {
							Output::Debug("Editor: Navigate to map {} not yet implemented", map_info.ID);
						}
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}
}

void Scene_Editor::DrawTilePalette() {
	ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Tile Palette");
	ImGui::Separator();

	// Layer selection
	ImGui::Text("Layer:");
	ImGui::RadioButton("Lower", &selected_layer, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Upper", &selected_layer, 1);
	ImGui::Separator();

	// Tool selection
	ImGui::Text("Tool:");
	const char* tool_names[] = { "Pencil", "Rect", "Fill", "Eraser", "Event" };
	for (int i = 0; i < 5; i++) {
		bool selected = (static_cast<int>(current_tool) == i);
		if (selected) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.70f, 0.30f, 1.0f));
		}
		if (ImGui::Button(tool_names[i], ImVec2(75, 0))) {
			current_tool = static_cast<Tool>(i);
		}
		if (selected) {
			ImGui::PopStyleColor();
		}
		if (i % 2 == 0 && i < 4) ImGui::SameLine();
	}
	ImGui::Separator();

	// Tile ID selector
	ImGui::Text("Tile ID: %d", selected_tile_id);
	ImGui::SliderInt("##TileID", &selected_tile_id, 0, 511);
	ImGui::Separator();

	// Show actual chipset image
	if (chipset_tex && chipset_tex_w > 0 && chipset_tex_h > 0) {
		ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Chipset");

		float avail_w = ImGui::GetContentRegionAvail().x;
		float scale = avail_w / chipset_tex_w;
		if (scale > 2.0f) scale = 2.0f;
		float display_w = chipset_tex_w * scale;
		float display_h = chipset_tex_h * scale;

		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImTextureID tex_id = (ImTextureID)(intptr_t)chipset_tex;
		ImGui::Image(tex_id, ImVec2(display_w, display_h));

		// Click on chipset to select tile position
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
			ImVec2 mouse = ImGui::GetMousePos();
			int tx = static_cast<int>((mouse.x - cursor.x) / scale / TILE_SIZE);
			int ty = static_cast<int>((mouse.y - cursor.y) / scale / TILE_SIZE);
			int cols = chipset_tex_w / TILE_SIZE;
			int new_id = ty * cols + tx;
			if (new_id >= 0 && new_id < 512) {
				selected_tile_id = new_id;
			}
		}
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No chipset loaded");
	}
}

void Scene_Editor::DrawMapViewport() {
	int map_w = Game_Map::GetTilesX();
	int map_h = Game_Map::GetTilesY();

	if (map_w <= 0 || map_h <= 0) {
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No map loaded");
		return;
	}

	ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
		"Map %d (%dx%d)", Game_Map::GetMapId(), map_w, map_h);

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
	float map_px_w = static_cast<float>(map_w * TILE_SIZE);
	float map_px_h = static_cast<float>(map_h * TILE_SIZE);
	ImVec2 canvas_size = ImVec2(map_px_w, map_px_h);

	// Scrollable canvas
	ImGui::InvisibleButton("##MapCanvas", canvas_size);
	bool canvas_hovered = ImGui::IsItemHovered();

	ImVec2 p0 = canvas_pos;
	ImVec2 p1 = ImVec2(canvas_pos.x + map_px_w, canvas_pos.y + map_px_h);

	// Draw lower layer texture
	if (show_lower_layer && map_lower_tex) {
		ImTextureID tex_id = (ImTextureID)(intptr_t)map_lower_tex;
		draw_list->AddImage(tex_id, p0, p1);
	}

	// Draw upper layer texture
	if (show_upper_layer && map_upper_tex) {
		ImTextureID tex_id = (ImTextureID)(intptr_t)map_upper_tex;
		draw_list->AddImage(tex_id, p0, p1);
	}

	// Draw grid overlay
	if (show_grid) {
		for (int x = 0; x <= map_w; x++) {
			float lx = canvas_pos.x + x * TILE_SIZE;
			draw_list->AddLine(ImVec2(lx, canvas_pos.y), ImVec2(lx, canvas_pos.y + map_px_h),
				IM_COL32(80, 120, 80, 40));
		}
		for (int y = 0; y <= map_h; y++) {
			float ly = canvas_pos.y + y * TILE_SIZE;
			draw_list->AddLine(ImVec2(canvas_pos.x, ly), ImVec2(canvas_pos.x + map_px_w, ly),
				IM_COL32(80, 120, 80, 40));
		}
	}

	// Draw events on map
	if (show_events) {
		auto& events = Game_Map::GetEvents();
		for (auto& ev : events) {
			int ex = ev.GetX();
			int ey = ev.GetY();
			if (ex < 0 || ex >= map_w || ey < 0 || ey >= map_h) continue;

			std::string sprite_name{ev.GetSpriteName()};
			int sprite_index = ev.GetSpriteIndex();
			int facing = ev.GetFacing();
			bool is_selected = (ev.GetId() == selected_event_id);

			if (!sprite_name.empty()) {
				// Charset sprite event
				SDL_Texture* ev_tex = GetEventTexture(sprite_name, sprite_index, facing);
				if (ev_tex) {
					int tex_w, tex_h;
					SDL_QueryTexture(ev_tex, nullptr, nullptr, &tex_w, &tex_h);

					// Center the sprite on the tile
					float draw_x = canvas_pos.x + ex * TILE_SIZE + (TILE_SIZE - tex_w) * 0.5f;
					float draw_y = canvas_pos.y + (ey + 1) * TILE_SIZE - tex_h;

					ImVec2 sp0(draw_x, draw_y);
					ImVec2 sp1(draw_x + tex_w, draw_y + tex_h);
					ImTextureID tex_id = (ImTextureID)(intptr_t)ev_tex;
					draw_list->AddImage(tex_id, sp0, sp1);

					if (is_selected) {
						draw_list->AddRect(sp0, sp1, IM_COL32(255, 255, 100, 255), 0, 0, 2.0f);
					}
				}
			} else {
				// Tile sprite event or no graphic — draw a colored marker
				ImVec2 ep0(canvas_pos.x + ex * TILE_SIZE, canvas_pos.y + ey * TILE_SIZE);
				ImVec2 ep1(ep0.x + TILE_SIZE, ep0.y + TILE_SIZE);

				// If tile sprite, draw tile
				int tile_id = ev.GetTileId();
				if (tile_id > 0) {
					auto chipset_name = Game_Map::GetChipsetName();
					if (!chipset_name.empty()) {
						// Try to get tile texture for this event
						std::string key = fmt::format("tile:{}", tile_id);
						SDL_Texture* tile_tex = nullptr;
						auto it = event_tex_cache.find(key);
						if (it != event_tex_cache.end()) {
							tile_tex = it->second;
						} else {
							BitmapRef tile_bmp = Cache::Tile(std::string(chipset_name), tile_id);
							if (tile_bmp) {
								tile_tex = BitmapToTexture(sdl_renderer, *tile_bmp);
								event_tex_cache[key] = tile_tex;
							}
						}
						if (tile_tex) {
							ImTextureID tex_id = (ImTextureID)(intptr_t)tile_tex;
							draw_list->AddImage(tex_id, ep0, ep1);
						}
					}
				} else {
					// No graphic — draw a small diamond marker
					ImU32 ev_color = is_selected ?
						IM_COL32(255, 255, 100, 200) :
						IM_COL32(100, 200, 255, 160);
					draw_list->AddRectFilled(ep0, ep1, ev_color);
				}

				if (is_selected) {
					draw_list->AddRect(ep0, ep1, IM_COL32(255, 255, 100, 255), 0, 0, 2.0f);
				}
			}

			// Draw event ID label
			ImVec2 text_pos(canvas_pos.x + ex * TILE_SIZE + 1, canvas_pos.y + ey * TILE_SIZE + 1);
			std::string id_str = fmt::format("{}", ev.GetId());
			draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 220), id_str.c_str());
		}
	}

	// Handle map click
	if (canvas_hovered && ImGui::IsMouseClicked(0)) {
		ImVec2 mouse = ImGui::GetMousePos();
		int tile_x = static_cast<int>((mouse.x - canvas_pos.x) / TILE_SIZE);
		int tile_y = static_cast<int>((mouse.y - canvas_pos.y) / TILE_SIZE);

		if (tile_x >= 0 && tile_x < map_w && tile_y >= 0 && tile_y < map_h) {
			if (current_tool == Tool::EventPlace) {
				// Check if clicking on an existing event
				auto& events = Game_Map::GetEvents();
				bool found = false;
				for (auto& ev : events) {
					if (ev.GetX() == tile_x && ev.GetY() == tile_y) {
						selected_event_id = ev.GetId();
						std::string name{ev.GetName()};
						std::strncpy(event_name_buf, name.c_str(), sizeof(event_name_buf) - 1);
						event_name_buf[sizeof(event_name_buf) - 1] = '\0';
						event_x = ev.GetX();
						event_y = ev.GetY();
						event_editor_open = true;
						found = true;
						break;
					}
				}
				if (!found) {
					Output::Debug("Editor: New event at ({}, {}) - not yet implemented", tile_x, tile_y);
				}
			} else {
				// Tile editing - log the action
				Output::Debug("Editor: Paint tile {} at ({}, {}) layer {}",
					selected_tile_id, tile_x, tile_y, selected_layer);
			}
		}
	}
}

void Scene_Editor::DrawEventList() {
	ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Events");
	ImGui::Separator();

	auto& events = Game_Map::GetEvents();

	ImGui::Text("Count: %d", static_cast<int>(events.size()));
	ImGui::Separator();

	for (auto& ev : events) {
		std::string label = fmt::format("#{}: {} ({},{})",
			ev.GetId(), ev.GetName(), ev.GetX(), ev.GetY());

		bool is_selected = (ev.GetId() == selected_event_id);
		if (ImGui::Selectable(label.c_str(), is_selected)) {
			selected_event_id = ev.GetId();
			std::string name{ev.GetName()};
			std::strncpy(event_name_buf, name.c_str(), sizeof(event_name_buf) - 1);
			event_name_buf[sizeof(event_name_buf) - 1] = '\0';
			event_x = ev.GetX();
			event_y = ev.GetY();
			event_editor_open = false;
		}

		// Double-click to open editor
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
			event_editor_open = true;
		}
	}
}

void Scene_Editor::DrawEventEditor() {
	auto* ev = Game_Map::GetEvent(selected_event_id);
	if (!ev) {
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Event not found");
		if (ImGui::Button("Back to List")) {
			event_editor_open = false;
		}
		return;
	}

	ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Event Editor");
	ImGui::Separator();

	if (ImGui::Button("<< Back")) {
		event_editor_open = false;
	}
	ImGui::Separator();

	// Event properties
	ImGui::Text("ID: %d", ev->GetId());

	ImGui::InputText("Name", event_name_buf, sizeof(event_name_buf));
	ImGui::InputInt("X", &event_x);
	ImGui::InputInt("Y", &event_y);

	// Clamp position
	int map_w = Game_Map::GetTilesX();
	int map_h = Game_Map::GetTilesY();
	event_x = std::clamp(event_x, 0, std::max(0, map_w - 1));
	event_y = std::clamp(event_y, 0, std::max(0, map_h - 1));

	ImGui::Separator();

	// Event page info (read-only for now)
	ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Pages");

	// Display direction/facing info
	ImGui::Text("Direction: %d", ev->GetDirection());
	ImGui::Text("Sprite: %s [%d]", std::string(ev->GetSpriteName()).c_str(), ev->GetSpriteIndex());

	ImGui::Text("Speed: %d", ev->GetMoveSpeed());
	ImGui::Text("Frequency: %d", ev->GetMoveFrequency());
	ImGui::Text("Through: %s", ev->GetThrough() ? "Yes" : "No");

	ImGui::Separator();

	// Event commands summary
	ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Commands");
	ImGui::TextWrapped("(Event command editing is read-only in this version)");

	ImGui::Separator();
	if (ImGui::Button("Apply Changes")) {
		Output::Debug("Editor: Apply changes to event {} - not yet fully implemented", ev->GetId());
	}
}

void Scene_Editor::DrawStatusBar() {
	ImGui::Separator();

	// Tool name
	const char* tool_names[] = { "Pencil", "Rectangle", "Fill", "Eraser", "Place Event" };
	ImGui::Text("Tool: %s | Layer: %s | Tile: %d | Map: %d (%dx%d)",
		tool_names[static_cast<int>(current_tool)],
		selected_layer == 0 ? "Lower" : "Upper",
		selected_tile_id,
		Game_Map::GetMapId(),
		Game_Map::GetTilesX(),
		Game_Map::GetTilesY());

	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
	ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "7 = Exit");
}

} // namespace Editor
