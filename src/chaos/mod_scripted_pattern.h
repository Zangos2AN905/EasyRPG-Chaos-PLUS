/*
 * Chaos Fork: Scripted Bullet Pattern Bridge
 *
 * Bridges ScriptedAttackDef (from mod API) to UndertaleBulletPattern
 * (from the bullet system). When a scripted attack is selected,
 * a ScriptedBulletPattern is created — it delegates Start/Update
 * to the appropriate script engine.
 */

#ifndef EP_CHAOS_MOD_SCRIPTED_PATTERN_H
#define EP_CHAOS_MOD_SCRIPTED_PATTERN_H

#include "chaos/undertale_bullet.h"
#include "chaos/mod_api.h"
#include "chaos/mod_script_engine.h"

#include <string>

namespace Chaos {

class ScriptEngine;

/**
 * A bullet pattern that delegates to a scripted attack definition.
 * The script engine is called each frame via ScriptBulletContext,
 * and any bullets the script requests are materialized as UndertaleBullet.
 */
class ScriptedBulletPattern : public UndertaleBulletPattern {
public:
	ScriptedBulletPattern(const ScriptedAttackDef& def, ScriptEngine* engine);

	void Start(int box_x, int box_y, int box_w, int box_h, bool tough) override;
	bool Update(std::vector<UndertaleBullet>& bullets, int frame) override;
	int GetDuration() const override;

private:
	/** Convert pending bullet spawns from context into real bullets */
	void MaterializeBullets(std::vector<UndertaleBullet>& bullets);

	const ScriptedAttackDef& attack_def;
	ScriptEngine* engine;
	ScriptBulletContext ctx;
	BitmapRef default_bullet_bitmap;

	void EnsureDefaultBitmap();
};

} // namespace Chaos

#endif // EP_CHAOS_MOD_SCRIPTED_PATTERN_H
