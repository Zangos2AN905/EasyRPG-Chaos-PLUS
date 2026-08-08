/*
 * Chaos Fork: Undertale Enemy Graphic implementation
 */

#include "chaos/undertale_enemy_graphic.h"
#include "game_enemy.h"
#include "game_enemyparty.h"
#include "main_data.h"
#include "cache.h"
#include "bitmap.h"
#include "output.h"

namespace Chaos {

// ---- UndertaleEnemyGraphic ----

UndertaleEnemyGraphic::UndertaleEnemyGraphic(Game_Enemy* enemy)
	: Sprite(Drawable::Flags::Default), enemy(enemy)
{
	SetZ(Priority_Window - 1);
	LoadAndConvert();
}

void UndertaleEnemyGraphic::Refresh() {
	if (!enemy) return;
	std::string current_name(enemy->GetSpriteName());
	if (current_name != loaded_sprite_name) {
		LoadAndConvert();
	}
}

void UndertaleEnemyGraphic::SetDisplayPosition(int x, int y) {
	SetX(x);
	SetY(y);
}

int UndertaleEnemyGraphic::GetGraphicWidth() const {
	auto bm = GetBitmap();
	return bm ? bm->GetWidth() : 0;
}

int UndertaleEnemyGraphic::GetGraphicHeight() const {
	auto bm = GetBitmap();
	return bm ? bm->GetHeight() : 0;
}

void UndertaleEnemyGraphic::LoadAndConvert() {
	if (!enemy) return;

	loaded_sprite_name = std::string(enemy->GetSpriteName());
	if (loaded_sprite_name.empty()) {
		SetVisible(false);
		return;
	}

	// Load the RPG Maker monster graphic
	BitmapRef source;
	source = Cache::Monster(loaded_sprite_name);
	if (!source) {
		Output::Debug("UndertaleEnemy: Could not load monster graphic '{}'", loaded_sprite_name);
		SetVisible(false);
		return;
	}

	// Apply hue if needed
	int hue = enemy->GetHue();
	if (hue != 0) {
		auto hued = Bitmap::Create(source->GetWidth(), source->GetHeight(), true);
		hued->HueChangeBlit(0, 0, *source, source->GetRect(), hue);
		source = hued;
	}

	// Use the original graphic as-is (preserving RPG Maker colors)
	SetBitmap(source);
	// Center the origin like Sprite_Enemy does
	SetOx(source->GetWidth() / 2);
	SetOy(source->GetHeight() / 2);
	SetVisible(true);
}

// ---- UndertaleEnemyGroup ----

void UndertaleEnemyGroup::Create() {
	graphics.clear();

	if (!Main_Data::game_enemyparty) return;

	auto enemies = Main_Data::game_enemyparty->GetEnemies();
	for (auto* enemy : enemies) {
		auto graphic = std::make_unique<UndertaleEnemyGraphic>(enemy);
		graphics.push_back(std::move(graphic));
	}
}

void UndertaleEnemyGroup::Update() {
	// Dead enemy visibility is handled by the vaporize animation
	// in UndertaleDamageRenderer::UpdateEffects(), which calls
	// SetVisible(false) when the dissolve finishes.
}

void UndertaleEnemyGroup::SetVisible(bool visible) {
	for (auto& g : graphics) {
		if (g) {
			// Only show alive enemies
			if (visible && g->GetEnemy() && g->GetEnemy()->IsDead()) {
				g->SetVisible(false);
			} else {
				g->SetVisible(visible);
			}
		}
	}
}

int UndertaleEnemyGroup::GetAliveCount() const {
	int count = 0;
	for (const auto& g : graphics) {
		if (g && g->GetEnemy() && !g->GetEnemy()->IsDead()) {
			++count;
		}
	}
	return count;
}

std::vector<std::string> UndertaleEnemyGroup::GetAliveEnemyNames() const {
	std::vector<std::string> names;
	for (const auto& g : graphics) {
		if (g && g->GetEnemy() && !g->GetEnemy()->IsDead()) {
			names.emplace_back(g->GetEnemy()->GetName());
		}
	}
	return names;
}

std::vector<int> UndertaleEnemyGroup::GetAliveEnemyIndices() const {
	std::vector<int> indices;
	for (int i = 0; i < static_cast<int>(graphics.size()); ++i) {
		if (graphics[i] && graphics[i]->GetEnemy() && !graphics[i]->GetEnemy()->IsDead()) {
			indices.push_back(i);
		}
	}
	return indices;
}

UndertaleEnemyGraphic* UndertaleEnemyGroup::GetGraphic(int index) {
	if (index < 0 || index >= static_cast<int>(graphics.size())) return nullptr;
	return graphics[index].get();
}

void UndertaleEnemyGroup::LayoutEnemies(int area_x, int area_y, int area_w, int area_h) {
	// Place enemies using their display positions (accounts for menu offsets etc.)
	for (auto& g : graphics) {
		if (!g || !g->GetEnemy()) continue;
		if (g->GetEnemy()->IsDead()) {
			g->SetVisible(false);
			continue;
		}

		// Use the same positioning the normal battle system uses
		int dx = g->GetEnemy()->GetDisplayX();
		int dy = g->GetEnemy()->GetDisplayY();

		g->SetDisplayPosition(dx, dy);
	}
}

} // namespace Chaos
