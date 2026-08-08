/*
 * Chaos Fork: Realtime Darkness/Lighting Overlay
 * Renders a darkness layer with dynamic light sources punched through.
 */

#ifndef EP_CHAOS_DRAWABLE_DARKNESS_OVERLAY_H
#define EP_CHAOS_DRAWABLE_DARKNESS_OVERLAY_H

#include "drawable.h"
#include "bitmap.h"
#include <vector>
#include <map>

/**
 * A fullscreen darkness overlay with dynamic light sources.
 *
 * Light sources can be attached to the player, events, or arbitrary screen positions.
 * The overlay is controlled by game switches and variables:
 *   - Switch (configurable): enables/disables the darkness overlay
 *   - Variable (configurable): darkness level (0=transparent, 255=pitch black)
 *   - Variable (configurable): player light radius in pixels
 * Additional light sources can be attached to events by ID.
 */
class Drawable_DarknessOverlay : public Drawable {
public:
	Drawable_DarknessOverlay();
	~Drawable_DarknessOverlay() override;

	void Draw(Bitmap& dst) override;

	struct LightSource {
		int screen_x = 0;      // Screen pixel X (center)
		int screen_y = 0;      // Screen pixel Y (center)
		int radius = 64;       // Light radius in pixels
		uint8_t intensity = 255; // 0-255 brightness
		uint8_t r = 255;       // Light color
		uint8_t g = 240;
		uint8_t b = 200;
	};

	/** Set the darkness level (0 = no darkness, 255 = pitch black) */
	void SetDarknessLevel(uint8_t level) { darkness_level = level; }
	uint8_t GetDarknessLevel() const { return darkness_level; }

	/** Set enabled state */
	void SetEnabled(bool enabled) { this->enabled = enabled; SetVisible(enabled); }
	bool IsEnabled() const { return enabled; }

	/** Attach a light to the player character */
	void SetPlayerLight(int radius, uint8_t intensity = 255);
	void ClearPlayerLight() { player_light_radius = 0; }
	int GetPlayerLightRadius() const { return player_light_radius; }

	/** Attach a light to an event by ID */
	void SetEventLight(int event_id, int radius, uint8_t intensity = 255,
		uint8_t r = 255, uint8_t g = 240, uint8_t b = 200);
	void ClearEventLight(int event_id);
	void ClearAllEventLights();

	/** Add a light at a fixed screen position */
	void AddFixedLight(int screen_x, int screen_y, int radius,
		uint8_t intensity = 255, uint8_t r = 255, uint8_t g = 240, uint8_t b = 200);
	void ClearFixedLights();

	/** Configure switch/variable IDs for game-driven control */
	void SetControlSwitch(int switch_id) { control_switch_id = switch_id; }
	void SetDarknessVariable(int var_id) { darkness_variable_id = var_id; }
	void SetPlayerRadiusVariable(int var_id) { player_radius_variable_id = var_id; }

private:
	/** Rebuild glow bitmap cache for a given radius */
	BitmapRef GetGlowBitmap(int radius);

	/** Gather all active light sources for this frame */
	void GatherLights(std::vector<LightSource>& out_lights);

	bool enabled = false;
	uint8_t darkness_level = 180; // Default moderate darkness

	// Player-attached light
	int player_light_radius = 80;
	uint8_t player_light_intensity = 255;

	// Event-attached lights: event_id -> light params
	struct EventLight {
		int radius = 48;
		uint8_t intensity = 255;
		uint8_t r = 255, g = 240, b = 200;
	};
	std::map<int, EventLight> event_lights;

	// Fixed screen-position lights
	std::vector<LightSource> fixed_lights;

	// Game control IDs (0 = not controlled by game)
	int control_switch_id = 0;
	int darkness_variable_id = 0;
	int player_radius_variable_id = 0;

	// Cached bitmaps
	BitmapRef darkness_bitmap;
	std::map<int, BitmapRef> glow_cache; // radius -> glow bitmap
	int last_dst_w = 0;
	int last_dst_h = 0;
};

#endif
