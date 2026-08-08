/*
 * Chaos Fork: Undertale Bullet Hell System
 * Manages bullets during the enemy turn dodging phase.
 * Designed to be expandable via modding — custom patterns can
 * subclass UndertaleBulletPattern.
 */

#ifndef EP_CHAOS_UNDERTALE_BULLET_H
#define EP_CHAOS_UNDERTALE_BULLET_H

#include "drawable.h"
#include "sprite.h"
#include "bitmap.h"
#include "memory_management.h"

#include <vector>
#include <memory>
#include <functional>

namespace Chaos {

/** A single bullet in the dodge phase */
struct UndertaleBullet {
	float x = 0.0f;
	float y = 0.0f;
	float vx = 0.0f;
	float vy = 0.0f;
	int hitbox_w = 8;
	int hitbox_h = 8;
	bool active = true;
	std::unique_ptr<Sprite> sprite;
};

/**
 * Base class for bullet patterns.
 * Subclass and override SpawnBullets() and UpdateBullets() to create
 * custom enemy attack patterns. The manager calls these each frame.
 */
class UndertaleBulletPattern {
public:
	virtual ~UndertaleBulletPattern() = default;

	/** Called once when the pattern starts */
	virtual void Start(int box_x, int box_y, int box_w, int box_h, bool tough) = 0;

	/** Called each frame. Return true when the pattern is finished. */
	virtual bool Update(std::vector<UndertaleBullet>& bullets, int frame) = 0;

	/** Total duration in frames. Used by the manager to know when to stop. */
	virtual int GetDuration() const = 0;
};

/**
 * Fire bullet pattern — the default Undertale attack.
 * Fires flame projectiles from the top of the box downward.
 * In tough mode, more flames spawn and move faster.
 */
class FireBulletPattern : public UndertaleBulletPattern {
public:
	void Start(int box_x, int box_y, int box_w, int box_h, bool tough) override;
	bool Update(std::vector<UndertaleBullet>& bullets, int frame) override;
	int GetDuration() const override;

private:
	int area_x = 0;
	int area_y = 0;
	int area_w = 0;
	int area_h = 0;
	bool is_tough = false;
	int spawn_interval = 12;
	float bullet_speed = 1.5f;
	int duration = 300; // ~5 seconds at 60fps
	BitmapRef fire_bitmap;

	void LoadFireSprite();
	void SpawnBullet(std::vector<UndertaleBullet>& bullets);
};

/**
 * Manages the bullet dodging phase.
 * Holds bullets, runs the active pattern, and checks collision
 * with the player soul.
 */
class UndertaleBulletManager : public Drawable {
public:
	UndertaleBulletManager();

	void Draw(Bitmap& dst) override;

	/** Start a new bullet dodge phase with the given pattern */
	void StartPattern(std::unique_ptr<UndertaleBulletPattern> pattern,
					  int box_x, int box_y, int box_w, int box_h, bool tough);

	/** Update bullets and check collisions. Returns true when phase is complete. */
	bool Update(int soul_x, int soul_y, int soul_w, int soul_h);

	/** Whether a collision occurred this frame */
	bool HitThisFrame() const { return hit_this_frame; }

	/** Stop and clear all bullets */
	void Clear();

	/** Whether the dodge phase is currently active */
	bool IsActive() const { return active; }

private:
	bool CheckCollision(const UndertaleBullet& bullet,
						int soul_x, int soul_y, int soul_w, int soul_h) const;

	std::vector<UndertaleBullet> bullets;
	std::unique_ptr<UndertaleBulletPattern> pattern;
	int frame = 0;
	bool active = false;
	bool hit_this_frame = false;
	int box_x = 0;
	int box_y = 0;
	int box_w = 0;
	int box_h = 0;
};

} // namespace Chaos

#endif
