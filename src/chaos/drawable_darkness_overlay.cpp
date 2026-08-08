/*
 * Chaos Fork: Realtime Darkness/Lighting Overlay Implementation
 *
 * Uses a light accumulation buffer approach:
 * 1. Fill buffer with ambient light level (darker = less ambient)
 * 2. Additively blit glow bitmaps for each light source
 * 3. Multiply the buffer onto the screen (dark areas darken the scene)
 */

#include "chaos/drawable_darkness_overlay.h"
#include "drawable_mgr.h"
#include "game_character.h"
#include "game_event.h"
#include "game_map.h"
#include "game_player.h"
#include "game_switches.h"
#include "game_variables.h"
#include "main_data.h"
#include "player.h"
#include <algorithm>
#include <cmath>

Drawable_DarknessOverlay::Drawable_DarknessOverlay()
	: Drawable(Priority_Weather + (1ULL << 48))
{
	DrawableMgr::Register(this);
	SetVisible(false);
}

Drawable_DarknessOverlay::~Drawable_DarknessOverlay() = default;

BitmapRef Drawable_DarknessOverlay::GetGlowBitmap(int radius) {
	if (radius <= 0) return {};

	auto it = glow_cache.find(radius);
	if (it != glow_cache.end()) {
		return it->second;
	}

	// Generate a radial gradient glow: white center fading to black, fully opaque
	// This will be blitted additively onto the light buffer
	int size = radius * 2;
	auto glow = Bitmap::Create(size, size, false); // opaque bitmap
	glow->Fill(Color(0, 0, 0, 255)); // Start with black (no light)

	int cx = radius;
	int cy = radius;

	// Draw concentric rings from outer to inner, each brighter ring overwrites
	for (int ring = radius; ring >= 0; --ring) {
		float t = static_cast<float>(ring) / static_cast<float>(radius);
		// Smooth falloff: quadratic ease-out for natural light attenuation
		float brightness = 1.0f - t * t;
		uint8_t val = static_cast<uint8_t>(std::min(255.0f, brightness * 255.0f));

		if (val == 0) continue;

		int r2 = ring * ring;
		for (int y = -ring; y <= ring; ++y) {
			int x_span = static_cast<int>(std::sqrt(static_cast<float>(r2 - y * y)));
			int px = cx - x_span;
			int py = cy + y;
			int width = x_span * 2 + 1;
			if (py < 0 || py >= size) continue;
			px = std::max(0, px);
			width = std::min(width, size - px);
			if (width <= 0) continue;
			glow->FillRect(Rect(px, py, width, 1), Color(val, val, val, 255));
		}
	}

	glow_cache[radius] = glow;
	return glow;
}

void Drawable_DarknessOverlay::GatherLights(std::vector<LightSource>& out_lights) {
	out_lights.clear();

	// Player light
	if (player_light_radius > 0 && Main_Data::game_player) {
		auto* hero = Main_Data::game_player.get();
		LightSource ls;
		ls.screen_x = hero->GetScreenX();
		ls.screen_y = hero->GetScreenY() - 8; // Center on character body, not feet
		ls.radius = player_light_radius;
		ls.intensity = player_light_intensity;
		ls.r = 255; ls.g = 245; ls.b = 220; // Warm white
		out_lights.push_back(ls);
	}

	// Event lights
	for (auto& [event_id, el] : event_lights) {
		Game_Event* ev = Game_Map::GetEvent(event_id);
		if (!ev) continue;
		if (!ev->GetActivePage()) continue;

		LightSource ls;
		ls.screen_x = ev->GetScreenX();
		ls.screen_y = ev->GetScreenY() - 8;
		ls.radius = el.radius;
		ls.intensity = el.intensity;
		ls.r = el.r; ls.g = el.g; ls.b = el.b;
		out_lights.push_back(ls);
	}

	// Fixed lights
	for (auto& fl : fixed_lights) {
		out_lights.push_back(fl);
	}
}

void Drawable_DarknessOverlay::Draw(Bitmap& dst) {
	// Check game switch control
	if (control_switch_id > 0 && Main_Data::game_switches) {
		enabled = Main_Data::game_switches->Get(control_switch_id);
		if (!enabled) return;
	}

	if (!enabled) return;

	// Check game variable control for darkness level
	if (darkness_variable_id > 0 && Main_Data::game_variables) {
		int val = Main_Data::game_variables->Get(darkness_variable_id);
		darkness_level = static_cast<uint8_t>(std::clamp(val, 0, 255));
	}

	// Check game variable control for player light radius
	if (player_radius_variable_id > 0 && Main_Data::game_variables) {
		int val = Main_Data::game_variables->Get(player_radius_variable_id);
		if (val > 0) {
			player_light_radius = std::clamp(val, 16, 256);
		}
	}

	if (darkness_level == 0) return;

	int w = dst.GetWidth();
	int h = dst.GetHeight();

	// Recreate light buffer if screen size changed
	if (!darkness_bitmap || last_dst_w != w || last_dst_h != h) {
		darkness_bitmap = Bitmap::Create(w, h, false);
		last_dst_w = w;
		last_dst_h = h;
	}

	// Fill with ambient light level
	// darkness_level 255 = pitch black (ambient = 0)
	// darkness_level 0 = fully lit (ambient = 255, no effect)
	uint8_t ambient = static_cast<uint8_t>(255 - darkness_level);
	darkness_bitmap->Fill(Color(ambient, ambient, ambient, 255));

	// Gather all active lights
	std::vector<LightSource> lights;
	GatherLights(lights);

	// Additively blit each light's glow onto the light buffer
	for (auto& light : lights) {
		if (light.radius <= 0 || light.intensity == 0) continue;

		auto glow = GetGlowBitmap(light.radius);
		if (!glow) continue;

		int lx = light.screen_x - light.radius;
		int ly = light.screen_y - light.radius;

		// Additive blit: adds the glow brightness to the light buffer
		// Intensity scales the opacity of the blit
		darkness_bitmap->Blit(lx, ly, *glow, glow->GetRect(),
			Opacity(static_cast<int>(light.intensity)),
			Bitmap::BlendMode::Additive);
	}

	// Multiply the light buffer onto the screen
	// Areas with value 255 (bright/lit) leave the screen unchanged
	// Areas with value 0 (dark) make the screen black
	dst.Blit(0, 0, *darkness_bitmap, darkness_bitmap->GetRect(),
		Opacity::Opaque(), Bitmap::BlendMode::Multiply);
}

void Drawable_DarknessOverlay::SetPlayerLight(int radius, uint8_t intensity) {
	player_light_radius = std::clamp(radius, 0, 256);
	player_light_intensity = intensity;
}

void Drawable_DarknessOverlay::SetEventLight(int event_id, int radius, uint8_t intensity,
		uint8_t r, uint8_t g, uint8_t b) {
	event_lights[event_id] = { std::clamp(radius, 0, 256), intensity, r, g, b };
}

void Drawable_DarknessOverlay::ClearEventLight(int event_id) {
	event_lights.erase(event_id);
}

void Drawable_DarknessOverlay::ClearAllEventLights() {
	event_lights.clear();
}

void Drawable_DarknessOverlay::AddFixedLight(int screen_x, int screen_y, int radius,
		uint8_t intensity, uint8_t r, uint8_t g, uint8_t b) {
	LightSource ls;
	ls.screen_x = screen_x;
	ls.screen_y = screen_y;
	ls.radius = std::clamp(radius, 0, 256);
	ls.intensity = intensity;
	ls.r = r; ls.g = g; ls.b = b;
	fixed_lights.push_back(ls);
}

void Drawable_DarknessOverlay::ClearFixedLights() {
	fixed_lights.clear();
}
