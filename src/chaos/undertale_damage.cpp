/*
 * Chaos Fork: Undertale Damage Display implementation
 */

#include "chaos/undertale_damage.h"
#include "chaos/undertale_font.h"
#include "chaos/undertale_enemy_graphic.h"
#include "text.h"
#include "drawable_mgr.h"
#include "color.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Chaos {

UndertaleDamageRenderer::UndertaleDamageRenderer()
	: Drawable(Priority_Window + 10, Drawable::Flags::Default)
{
	DrawableMgr::Register(this);
	SetVisible(true);
}

void UndertaleDamageRenderer::Draw(Bitmap& dst) {
	auto font = UndertaleFont::Damage();
	if (!font) return;

	for (const auto& p : popups) {
		if (!p.active) continue;

		// Fade out in the last 15 frames
		int alpha = 255;
		if (p.frame > DamagePopup::kDuration - 15) {
			alpha = 255 * (DamagePopup::kDuration - p.frame) / 15;
		}
		if (alpha <= 0) continue;

		// Rise from start position
		float progress = static_cast<float>(p.frame) / DamagePopup::kDuration;
		int rise = static_cast<int>(DamagePopup::kRisePixels * progress);
		int draw_y = p.start_y - rise;

		std::string text = p.miss ? "MISS" : std::to_string(p.damage);

		Color color = p.miss
			? Color(180, 180, 180, alpha)
			: Color(255, 0, 0, alpha); // Red damage numbers

		Text::Draw(dst, p.x, draw_y, *font, color, text);
	}
}

void UndertaleDamageRenderer::ShowDamage(int x, int y, int damage) {
	DamagePopup p;
	p.x = x;
	p.y = y;
	p.start_y = y;
	p.damage = damage;
	p.frame = 0;
	p.miss = false;
	p.active = true;
	popups.push_back(p);
}

void UndertaleDamageRenderer::ShowMiss(int x, int y) {
	DamagePopup p;
	p.x = x;
	p.y = y;
	p.start_y = y;
	p.damage = 0;
	p.frame = 0;
	p.miss = true;
	p.active = true;
	popups.push_back(p);
}

void UndertaleDamageRenderer::StartShake(int enemy_index, UndertaleEnemyGraphic* graphic) {
	EnemyShake s;
	s.enemy_index = enemy_index;
	s.original_x = graphic ? graphic->GetX() : 0;
	s.frame = 0;
	s.active = true;
	shakes.push_back(s);
}

void UndertaleDamageRenderer::StartVaporize(int enemy_index, UndertaleEnemyGraphic* graphic) {
	EnemyVaporize v;
	v.enemy_index = enemy_index;
	v.frame = 0;
	v.active = true;
	v.original_y = graphic ? graphic->GetY() : 0;
	v.pixels_dissolved = 0;

	// Snapshot the current bitmap and collect all non-transparent pixels
	if (graphic && graphic->GetBitmap()) {
		auto src = graphic->GetBitmap();
		int w = src->GetWidth();
		int h = src->GetHeight();
		v.snapshot = Bitmap::Create(w, h, true);
		v.snapshot->Blit(0, 0, *src, src->GetRect(), Opacity::Opaque());

		// Scan for all non-transparent pixels
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				Color c = v.snapshot->GetColorAt(x, y);
				if (c.alpha > 0) {
					v.opaque_pixels.push_back({x, y});
				}
			}
		}

		// Shuffle so dissolution looks random
		for (int i = static_cast<int>(v.opaque_pixels.size()) - 1; i > 0; --i) {
			int j = std::rand() % (i + 1);
			std::swap(v.opaque_pixels[i], v.opaque_pixels[j]);
		}

		graphic->SetBitmap(v.snapshot);
	}

	vaporizes.push_back(std::move(v));
}

void UndertaleDamageRenderer::UpdatePopups() {
	for (auto& p : popups) {
		if (!p.active) continue;
		++p.frame;
		if (p.frame >= DamagePopup::kDuration) {
			p.active = false;
		}
	}

	// Clean up finished popups
	popups.erase(
		std::remove_if(popups.begin(), popups.end(),
			[](const DamagePopup& p) { return !p.active; }),
		popups.end()
	);
}

void UndertaleDamageRenderer::UpdateEffects(
	std::function<UndertaleEnemyGraphic*(int)> get_graphic)
{
	// Shake effects
	for (auto& s : shakes) {
		if (!s.active) continue;
		++s.frame;

		auto* g = get_graphic(s.enemy_index);
		if (g) {
			if (s.frame < EnemyShake::kDuration) {
				// Oscillate X position
				int offset = static_cast<int>(
					EnemyShake::kAmplitude * std::sin(s.frame * 1.5));
				g->SetX(s.original_x + offset);
			} else {
				// Restore original position
				g->SetX(s.original_x);
				s.active = false;
			}
		} else {
			s.active = false;
		}
	}

	// Vaporize effects — dissolve all non-transparent pixels + drift upward
	for (auto& v : vaporizes) {
		if (!v.active) continue;
		++v.frame;

		auto* g = get_graphic(v.enemy_index);
		if (g) {
			int total = static_cast<int>(v.opaque_pixels.size());
			if (v.frame < EnemyVaporize::kDuration && v.snapshot && total > 0) {
				// Calculate how many pixels should be dissolved by this frame
				// Use an ease-in curve so dissolution accelerates
				float t = static_cast<float>(v.frame) / EnemyVaporize::kDuration;
				float eased = t * t; // quadratic ease-in
				int target_dissolved = static_cast<int>(eased * total);
				int to_dissolve = target_dissolved - v.pixels_dissolved;

				// Erase the next batch of shuffled pixels
				for (int i = 0; i < to_dissolve && v.pixels_dissolved < total; ++i) {
					auto [px, py] = v.opaque_pixels[v.pixels_dissolved];
					v.snapshot->ClearRect(Rect(px, py, 1, 1));
					++v.pixels_dissolved;
				}

				// Drift upward
				int rise = static_cast<int>(EnemyVaporize::kRisePixels * t);
				g->SetY(v.original_y - rise);

				// Fade overall opacity in the final third
				if (t > 0.66f) {
					float fade = 1.0f - (t - 0.66f) / 0.34f;
					g->SetOpacity(static_cast<int>(fade * 255));
				}
			} else {
				g->SetVisible(false);
				g->SetOpacity(255);
				v.active = false;
			}
		} else {
			v.active = false;
		}
	}

	// Clean up finished effects
	shakes.erase(
		std::remove_if(shakes.begin(), shakes.end(),
			[](const EnemyShake& s) { return !s.active; }),
		shakes.end()
	);
	vaporizes.erase(
		std::remove_if(vaporizes.begin(), vaporizes.end(),
			[](const EnemyVaporize& v) { return !v.active; }),
		vaporizes.end()
	);
}

bool UndertaleDamageRenderer::HasActiveEffects() const {
	return !popups.empty() || !shakes.empty() || !vaporizes.empty();
}

void UndertaleDamageRenderer::Clear() {
	popups.clear();
	shakes.clear();
	vaporizes.clear();
}

} // namespace Chaos
