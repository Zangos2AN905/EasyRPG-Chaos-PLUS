/*
 * Chaos Fork: Undertale Bullet Hell System implementation
 */

#include "chaos/undertale_bullet.h"
#include "filefinder.h"
#include "cache.h"
#include "output.h"
#include "drawable_mgr.h"
#include "utils.h"

#include <cstdlib>
#include <algorithm>

namespace Chaos {

// ---- FireBulletPattern ----

void FireBulletPattern::Start(int bx, int by, int bw, int bh, bool tough) {
	area_x = bx;
	area_y = by;
	area_w = bw;
	area_h = bh;
	is_tough = tough;

	if (is_tough) {
		spawn_interval = 6;
		bullet_speed = 2.2f;
		duration = 420; // ~7 seconds
	} else {
		spawn_interval = 12;
		bullet_speed = 1.5f;
		duration = 300; // ~5 seconds
	}

	LoadFireSprite();
}

bool FireBulletPattern::Update(std::vector<UndertaleBullet>& bullets, int frame) {
	// Spawn new bullets at regular intervals
	if (frame < duration - 60 && frame % spawn_interval == 0) {
		SpawnBullet(bullets);
		// Tough mode spawns a second bullet per interval
		if (is_tough && frame % (spawn_interval * 2) == 0) {
			SpawnBullet(bullets);
		}
	}

	// Move existing bullets downward
	for (auto& b : bullets) {
		if (!b.active) continue;
		b.y += b.vy;
		b.x += b.vx;

		// Deactivate bullets that leave the box
		if (b.y > area_y + area_h + 8 || b.y < area_y - 16 ||
			b.x > area_x + area_w + 8 || b.x < area_x - 16) {
			b.active = false;
			if (b.sprite) b.sprite->SetVisible(false);
		}
	}

	return frame >= duration;
}

int FireBulletPattern::GetDuration() const {
	return duration;
}

void FireBulletPattern::LoadFireSprite() {
	if (fire_bitmap) return;

	auto chaos_fs = FileFinder::ChaosAssets();
	if (!chaos_fs) return;

	auto stream = chaos_fs.OpenFile("UndertaleMode.content/Sprite", "spr_firebullet_0", FileFinder::IMG_TYPES);
	if (!stream) {
		Output::Debug("FireBullet: Could not find fire sprite");
		return;
	}

	fire_bitmap = Bitmap::Create(std::move(stream), true);
}

void FireBulletPattern::SpawnBullet(std::vector<UndertaleBullet>& bullets) {
	UndertaleBullet b;
	// Random X within the box area
	b.x = static_cast<float>(area_x + (std::rand() % std::max(1, area_w - 8)));
	b.y = static_cast<float>(area_y - 4);
	b.vx = 0.0f;
	b.vy = bullet_speed;

	if (fire_bitmap) {
		b.hitbox_w = fire_bitmap->GetWidth();
		b.hitbox_h = fire_bitmap->GetHeight();
	} else {
		b.hitbox_w = 8;
		b.hitbox_h = 8;
	}

	b.active = true;

	// Create sprite for this bullet
	b.sprite = std::make_unique<Sprite>();
	b.sprite->SetZ(Priority_Window + 5);
	if (fire_bitmap) {
		b.sprite->SetBitmap(fire_bitmap);
	}
	b.sprite->SetX(static_cast<int>(b.x));
	b.sprite->SetY(static_cast<int>(b.y));
	b.sprite->SetVisible(true);

	bullets.push_back(std::move(b));
}

// ---- UndertaleBulletManager ----

UndertaleBulletManager::UndertaleBulletManager()
	: Drawable(Priority_Window + 4, Drawable::Flags::Default)
{
	DrawableMgr::Register(this);
	SetVisible(false);
}

void UndertaleBulletManager::Draw(Bitmap& /* dst */) {
	// Bullets are drawn by individual Sprite objects, not here.
	// This Drawable exists for lifecycle management.
}

void UndertaleBulletManager::StartPattern(
	std::unique_ptr<UndertaleBulletPattern> new_pattern,
	int bx, int by, int bw, int bh, bool tough)
{
	Clear();
	pattern = std::move(new_pattern);
	box_x = bx;
	box_y = by;
	box_w = bw;
	box_h = bh;
	frame = 0;
	active = true;
	hit_this_frame = false;
	SetVisible(true);

	if (pattern) {
		pattern->Start(bx, by, bw, bh, tough);
	}
}

bool UndertaleBulletManager::Update(int soul_x, int soul_y, int soul_w, int soul_h) {
	if (!active || !pattern) return true;

	hit_this_frame = false;
	++frame;

	bool done = pattern->Update(bullets, frame);

	// Update bullet sprite positions and check collisions
	for (auto& b : bullets) {
		if (!b.active) continue;

		if (b.sprite) {
			b.sprite->SetX(static_cast<int>(b.x));
			b.sprite->SetY(static_cast<int>(b.y));
		}

		if (CheckCollision(b, soul_x, soul_y, soul_w, soul_h)) {
			hit_this_frame = true;
		}
	}

	// Clean up inactive bullets
	bullets.erase(
		std::remove_if(bullets.begin(), bullets.end(),
			[](const UndertaleBullet& b) { return !b.active; }),
		bullets.end()
	);

	if (done && bullets.empty()) {
		Clear();
		return true;
	}

	return false;
}

void UndertaleBulletManager::Clear() {
	for (auto& b : bullets) {
		if (b.sprite) b.sprite->SetVisible(false);
	}
	bullets.clear();
	pattern.reset();
	frame = 0;
	active = false;
	hit_this_frame = false;
	SetVisible(false);
}

bool UndertaleBulletManager::CheckCollision(
	const UndertaleBullet& bullet,
	int soul_x, int soul_y, int soul_w, int soul_h) const
{
	// Simple AABB collision with a small inset for fairness
	constexpr int inset = 2;
	int bx = static_cast<int>(bullet.x) + inset;
	int by = static_cast<int>(bullet.y) + inset;
	int bw = bullet.hitbox_w - inset * 2;
	int bh = bullet.hitbox_h - inset * 2;

	int sx = soul_x + inset;
	int sy = soul_y + inset;
	int sw = soul_w - inset * 2;
	int sh = soul_h - inset * 2;

	return bx < sx + sw && bx + bw > sx &&
		   by < sy + sh && by + bh > sy;
}

} // namespace Chaos
