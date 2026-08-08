/*
 * Chaos Fork: Scripted Bullet Pattern Bridge implementation
 */

#include "chaos/mod_scripted_pattern.h"
#include "chaos/mod_loader.h"
#include "bitmap.h"
#include "cache.h"
#include "output.h"

#include <cstdlib>

namespace Chaos {

ScriptedBulletPattern::ScriptedBulletPattern(const ScriptedAttackDef& def, ScriptEngine* eng)
	: attack_def(def), engine(eng) {
}

void ScriptedBulletPattern::Start(int box_x, int box_y, int box_w, int box_h, bool tough) {
	ctx = ScriptBulletContext();
	ctx.box_x = box_x;
	ctx.box_y = box_y;
	ctx.box_w = box_w;
	ctx.box_h = box_h;
	ctx.is_tough = tough;
	ctx.frame = 0;

	EnsureDefaultBitmap();

	if (engine) {
		engine->CallAttackStart(attack_def, ctx);
	}
}

bool ScriptedBulletPattern::Update(std::vector<UndertaleBullet>& bullets, int frame) {
	ctx.frame = frame;
	ctx.pending_spawns.clear();

	if (!engine) {
		return frame >= attack_def.default_duration;
	}

	bool done = engine->CallAttackUpdate(attack_def, ctx);

	// Convert any bullets the script requested into real bullets
	MaterializeBullets(bullets);

	// Also end if we exceeded the default duration
	if (frame >= attack_def.default_duration) {
		done = true;
	}

	return done;
}

int ScriptedBulletPattern::GetDuration() const {
	return attack_def.default_duration;
}

void ScriptedBulletPattern::MaterializeBullets(std::vector<UndertaleBullet>& bullets) {
	for (const auto& spawn : ctx.pending_spawns) {
		UndertaleBullet b;
		b.x = spawn.x;
		b.y = spawn.y;
		b.vx = spawn.vx;
		b.vy = spawn.vy;
		b.hitbox_w = spawn.hitbox_w;
		b.hitbox_h = spawn.hitbox_h;
		b.active = true;

		// Create sprite for the bullet
		BitmapRef bmp = default_bullet_bitmap;

		// If the spawn specified a sprite name, try to load it from mod assets
		if (!spawn.sprite_name.empty()) {
			auto* mod_info = ModRegistry::Instance().FindMod(attack_def.mod_id);
			if (mod_info && !mod_info->assets_path.empty()) {
				std::string full_path = mod_info->assets_path + "/" + spawn.sprite_name;
				// Try to load as a custom bitmap
				// For now, fall back to default if not found
				// TODO: Asset loading from mod folders
			}
		}

		if (bmp) {
			b.sprite = std::make_unique<Sprite>();
			b.sprite->SetBitmap(bmp);
			b.sprite->SetX(static_cast<int>(b.x) - bmp->GetWidth() / 2);
			b.sprite->SetY(static_cast<int>(b.y) - bmp->GetHeight() / 2);
		}

		bullets.push_back(std::move(b));
	}
	ctx.pending_spawns.clear();
}

void ScriptedBulletPattern::EnsureDefaultBitmap() {
	if (default_bullet_bitmap) return;

	// Create a simple white circle bitmap as default bullet
	const int size = 8;
	default_bullet_bitmap = Bitmap::Create(size, size, true);
	default_bullet_bitmap->Clear();

	// Draw a filled circle
	int cx = size / 2;
	int cy = size / 2;
	int r = size / 2 - 1;
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			int dx = x - cx;
			int dy = y - cy;
			if (dx * dx + dy * dy <= r * r) {
				default_bullet_bitmap->FillRect(Rect(x, y, 1, 1), Color(255, 255, 255, 255));
			}
		}
	}
}

} // namespace Chaos
