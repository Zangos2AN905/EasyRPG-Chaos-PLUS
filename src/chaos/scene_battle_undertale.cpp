/*
 * Chaos Fork: Undertale Battle Scene implementation
 */

#include "chaos/scene_battle_undertale.h"
#include "chaos/undertale_font.h"
#include "chaos/undertale_bullet.h"
#include "chaos/undertale_damage.h"
#include "input.h"
#include "output.h"
#include "player.h"
#include "options.h"
#include "color.h"
#include "audio.h"
#include "audio_secache.h"
#include "filefinder.h"
#include "game_system.h"
#include "game_battle.h"
#include "game_player.h"
#include "game_party.h"
#include "game_actors.h"
#include "game_enemy.h"
#include "game_enemyparty.h"
#include "main_data.h"
#include "spriteset_battle.h"
#include "transition.h"
#include "utils.h"
#include "scene_gameover.h"
#include <lcf/data.h>
#include <lcf/reader_util.h>

namespace Chaos {

namespace {
	bool ShouldPlayStrongerBattleMusic(const lcf::rpg::Troop& troop) {
		auto has_stronger_keyword = [](const std::string& value) {
			const std::string lower = Utils::LowerCase(value);
			return lower.find("tough+") != std::string::npos
				|| lower.find("tough") != std::string::npos
				|| lower.find("strong") != std::string::npos;
		};

		if (has_stronger_keyword(ToString(troop.name))) {
			return true;
		}

		for (const auto& member : troop.members) {
			const auto* enemy = lcf::ReaderUtil::GetElement(lcf::Data::enemies, member.enemy_id);
			if (enemy && has_stronger_keyword(ToString(enemy->name))) {
				return true;
			}
		}

		return false;
	}

	void PlayUndertaleBattleMusic(int troop_id) {
		auto chaos_fs = FileFinder::ChaosAssets();
		if (!chaos_fs) {
			Output::Debug("UndertaleBattle: ChaosAssets not available for music");
			return;
		}

		const auto* troop = lcf::ReaderUtil::GetElement(lcf::Data::troops, troop_id);
		const char* music_name = "EnemyMIDI";
		if (troop && ShouldPlayStrongerBattleMusic(*troop)) {
			music_name = "StrongerMIDI";
		}

		auto stream = chaos_fs.OpenFile("UndertaleMode.content/Music", music_name, FileFinder::MUSIC_TYPES);
		if (!stream) {
			Output::Debug("UndertaleBattle: Could not find {}", music_name);
			return;
		}

		Audio().BGM_Stop();
		Audio().BGM_Play(std::move(stream), 100, 100, 0, 50);
	}

	constexpr int kSoulSize = 16;
	constexpr double kSoulScale = 0.5;
	constexpr int kEncounterFlashOnFrames = 4;
	constexpr int kEncounterFlashOffFrames = 3;
	constexpr int kEncounterFlashCycles = 3;
	constexpr int kSoulFallFrames = 30;
}

Scene_Battle_Undertale::Scene_Battle_Undertale(const BattleArgs& args)
	: Scene_Battle(args)
{
}

Scene_Battle_Undertale::~Scene_Battle_Undertale() = default;

void Scene_Battle_Undertale::Start() {
	Scene_Battle::Start();

	Game_Battle::GetSpriteset().SetBackgroundVisible(false);
	Audio().BGM_Stop();

	// Create enemies and stats after base Start()
	enemy_group.Create();
	LayoutEnemies();
	RefreshStatsBar();

	SetUndertaleState(UndertaleState::EncounterIntro);
}

void Scene_Battle_Undertale::TransitionIn(SceneType /* prev_scene */) {
	Transition::instance().InitShow(Transition::TransitionCutIn, this, 1);
}

void Scene_Battle_Undertale::TransitionOut(SceneType /* next_scene */) {
	Transition::instance().InitErase(Transition::TransitionCutOut, this, 1);
}

void Scene_Battle_Undertale::CreateUi() {
	// Create the 4 buttons
	buttons[0] = std::make_unique<UndertaleButton>(UndertaleButtonType::Fight);
	buttons[1] = std::make_unique<UndertaleButton>(UndertaleButtonType::Act);
	buttons[2] = std::make_unique<UndertaleButton>(UndertaleButtonType::Item);
	buttons[3] = std::make_unique<UndertaleButton>(UndertaleButtonType::Mercy);

	// Create the dialogue/bullet box
	battle_box = std::make_unique<UndertaleBattleBox>();

	// Create the fight bar (hidden initially)
	fight_bar = std::make_unique<UndertaleFightBar>();

	// Create dialogue system
	dialogue = std::make_unique<UndertaleDialogue>();

	// Create choice menu
	choice_menu = std::make_unique<UndertaleChoiceMenu>();

	// Create stats bar
	stats_bar = std::make_unique<UndertaleStatsBar>();

	// Create bullet manager and damage renderer
	bullet_manager = std::make_unique<UndertaleBulletManager>();
	damage_renderer = std::make_unique<UndertaleDamageRenderer>();

	LoadSoulSprite();
	LayoutButtons();

	// Initial selection
	selected_button = 0;
	UpdateButtonSelection();
}

void Scene_Battle_Undertale::LoadSoulSprite() {
	soul_sprite = std::make_unique<Sprite>();
	soul_sprite->SetZ(Priority_Window + 4);

	auto chaos_fs = FileFinder::ChaosAssets();
	if (!chaos_fs) {
		Output::Debug("UndertaleBattle: ChaosAssets not available for soul sprite");
		soul_sprite->SetVisible(false);
		return;
	}

	auto stream = chaos_fs.OpenFile("UndertaleMode.content/Sprite/Other", "soul", FileFinder::IMG_TYPES);
	if (!stream) {
		Output::Debug("UndertaleBattle: Could not find soul sprite");
		soul_sprite->SetVisible(false);
		return;
	}

	auto bitmap = Bitmap::Create(std::move(stream), true);
	if (!bitmap) {
		soul_sprite->SetVisible(false);
		return;
	}

	soul_sprite->SetBitmap(bitmap);
	soul_sprite->SetZoomX(kSoulScale);
	soul_sprite->SetZoomY(kSoulScale);
	soul_sprite->SetVisible(false);
}

void Scene_Battle_Undertale::LayoutButtons() {
	// Undertale layout: 4 buttons evenly spaced along the bottom of the screen,
	// below the battle box.
	// Screen: 320x240
	// Each button half: 110x42
	// Position buttons in a row near the bottom

	constexpr int btn_w = UndertaleButton::HALF_WIDTH; // 110
	constexpr int btn_h = UndertaleButton::SHEET_HEIGHT; // 42
	constexpr int screen_w = SCREEN_TARGET_WIDTH; // 320

	// Total width of 4 buttons with spacing
	constexpr int num_buttons = 4;
	constexpr int total_btn_width = btn_w * num_buttons; // 440 > 320, need to scale down

	// Scale buttons to fit: target ~70px each with small gaps
	// We'll use the native size but position them overlapping slightly,
	// or better yet, use a narrower arrangement scaled to screen width
	constexpr int btn_display_w = 58;
	constexpr int spacing = 18;
	constexpr int total_width = btn_display_w * num_buttons + spacing * (num_buttons - 1);
	constexpr int start_x = (screen_w - total_width) / 2;
	constexpr int btn_y = SCREEN_TARGET_HEIGHT - 24; // tighter to the bottom like Undertale

	// Scale buttons
	double scale_x = static_cast<double>(btn_display_w) / btn_w;
	double scale_y = scale_x; // uniform scale

	for (int i = 0; i < num_buttons; ++i) {
		buttons[i]->SetX(start_x + i * (btn_display_w + spacing));
		buttons[i]->SetY(btn_y);
		buttons[i]->SetZoomX(scale_x);
		buttons[i]->SetZoomY(scale_y);
	}

	// Battle box: positioned above the buttons
	// Undertale (640x480) box is ~574x140, border ~3px. Scaled to 320x240: ~287x70, border ~2px.
	constexpr int box_width = 287;
	constexpr int box_height = 70;
	constexpr int box_x = (screen_w - box_width) / 2;
	constexpr int box_y = btn_y - box_height - 18; // moved up to leave room for stats bar
	battle_box->SetBorderThickness(2);
	battle_box->SetBounds(box_x, box_y, box_width, box_height);

	// Save default box dimensions for restoring after dodge phase
	default_box_x = box_x;
	default_box_y = box_y;
	default_box_w = box_width;
	default_box_h = box_height;

	// Dialogue and choice menu share the battle box inner bounds
	if (dialogue) {
		dialogue->SetBounds(battle_box->GetInnerX(), battle_box->GetInnerY(),
			battle_box->GetInnerWidth(), battle_box->GetInnerHeight());
	}
	if (choice_menu) {
		choice_menu->SetBounds(battle_box->GetInnerX(), battle_box->GetInnerY(),
			battle_box->GetInnerWidth(), battle_box->GetInnerHeight());
	}

	// Stats bar sits between the box and the buttons, centered on the battle box
	if (stats_bar) {
		int stats_y = box_y + box_height + 2;
		stats_bar->SetPosition(box_x, stats_y, box_width);
	}

	// Layout enemies above the battle box
	LayoutEnemies();
}

void Scene_Battle_Undertale::UpdateButtonSelection() {
	for (int i = 0; i < 4; ++i) {
		buttons[i]->SetSelected(i == selected_button);
	}
	UpdateSoulSelectionPosition();
}

void Scene_Battle_Undertale::SetUiVisible(bool visible) {
	for (auto& button : buttons) {
		if (button) {
			button->SetVisible(visible);
		}
	}
	if (battle_box) {
		battle_box->SetVisible(visible);
	}
	if (stats_bar) {
		stats_bar->SetVisible(visible);
	}
	enemy_group.SetVisible(visible);
}

void Scene_Battle_Undertale::UpdateSoulSelectionPosition() {
	if (!soul_sprite || selected_button < 0 || selected_button >= static_cast<int>(buttons.size()) || !buttons[selected_button]) {
		return;
	}

	const int button_width = static_cast<int>(std::round(UndertaleButton::HALF_WIDTH * buttons[selected_button]->GetZoomX()));
	const int button_height = static_cast<int>(std::round(UndertaleButton::SHEET_HEIGHT * buttons[selected_button]->GetZoomY()));
	const int soul_width = static_cast<int>(std::round(kSoulSize * kSoulScale));
	const int soul_height = static_cast<int>(std::round(kSoulSize * kSoulScale));
	constexpr int kSoulSlotCenterX = 18;

	// Keep the soul centered in the button's left icon slot even when the button scale changes.
	const int soul_slot_center_x = static_cast<int>(std::round(
		static_cast<double>(kSoulSlotCenterX) * button_width / UndertaleButton::HALF_WIDTH));
	soul_target_x = buttons[selected_button]->GetX() + soul_slot_center_x - soul_width / 2;
	soul_target_y = buttons[selected_button]->GetY() + (button_height - soul_height) / 2;

	if (ut_state == UndertaleState::MainMenu) {
		soul_sprite->SetX(soul_target_x);
		soul_sprite->SetY(soul_target_y);
	}
}

void Scene_Battle_Undertale::PlayEncounterSound(const char* name) {
	auto chaos_fs = FileFinder::ChaosAssets();
	if (!chaos_fs) {
		return;
	}

	auto stream = chaos_fs.OpenFile("UndertaleMode.content/Sound", name, FileFinder::SOUND_TYPES);
	if (!stream) {
		Output::Debug("UndertaleBattle: Could not find sound {}", name);
		return;
	}

	auto se_cache = AudioSeCache::Create(std::move(stream), name);
	if (!se_cache) {
		return;
	}

	Audio().SE_Play(std::move(se_cache), 100, 100, 50);
}

void Scene_Battle_Undertale::BeginSoulFall() {
	encounter_phase = EncounterIntroPhase::Falling;
	encounter_frame = 0;
	UpdateSoulSelectionPosition();
	PlayEncounterSound("snd_battlefall");
	if (soul_sprite) {
		soul_sprite->SetVisible(true);
	}
}

void Scene_Battle_Undertale::UpdateEncounterIntro() {
	if (!soul_sprite) {
		SetUndertaleState(UndertaleState::MainMenu);
		return;
	}

	if (encounter_phase == EncounterIntroPhase::Flashing) {
		const int cycle_frames = kEncounterFlashOnFrames + kEncounterFlashOffFrames;
		const int cycle_index = encounter_frame / cycle_frames;
		const int cycle_frame = encounter_frame % cycle_frames;

		if (cycle_frame == 0 && cycle_index < kEncounterFlashCycles) {
			PlayEncounterSound("snd_noise");
		}

		soul_sprite->SetVisible(cycle_frame < kEncounterFlashOnFrames);
		++encounter_frame;

		if (cycle_index >= kEncounterFlashCycles) {
			BeginSoulFall();
		}
		return;
	}

	const float progress = std::min(1.0f, static_cast<float>(encounter_frame) / kSoulFallFrames);
	const int x = static_cast<int>(std::round(soul_start_x + (soul_target_x - soul_start_x) * progress));
	const int y = static_cast<int>(std::round(soul_start_y + (soul_target_y - soul_start_y) * progress));
	soul_sprite->SetVisible(true);
	soul_sprite->SetX(x);
	soul_sprite->SetY(y);
	++encounter_frame;

	if (progress >= 1.0f) {
		PlayUndertaleBattleMusic(troop_id);
		Transition::instance().InitShow(Transition::TransitionFadeIn, this, 16);
		// Show encounter dialogue text (skip typewriter, show immediately) and go to main menu
		ShowEncounterDialogue();
		if (dialogue) {
			dialogue->SkipToEnd();
		}
		SetUndertaleState(UndertaleState::MainMenu);
		SetUiVisible(true);
	}
}

void Scene_Battle_Undertale::vUpdate() {
	// Update enemy group every frame
	enemy_group.Update();

	// Update damage effects every frame regardless of state
	UpdateDamageEffects();

	// Tick box resize animation
	UpdateBoxAnimation();

	// Update iframes counter
	if (iframe_frames > 0) {
		--iframe_frames;
		if (soul_sprite) {
			// Flash the soul during iframes
			bool visible = (iframe_frames / kIframeFlashRate) % 2 == 0;
			soul_sprite->SetVisible(visible);
		}
		if (iframe_frames == 0 && soul_sprite) {
			soul_sprite->SetVisible(true);
		}
	}

	switch (ut_state) {
		case UndertaleState::EncounterIntro:
			UpdateEncounterIntro();
			break;
		case UndertaleState::MainMenu:
			UpdateMainMenu();
			break;
		case UndertaleState::FightBar:
			UpdateFightBar();
			break;
		case UndertaleState::Dialogue:
			UpdateDialogue();
			break;
		case UndertaleState::EnemySelect:
			UpdateEnemySelect();
			break;
		case UndertaleState::ActMenu:
			UpdateActMenu();
			break;
		case UndertaleState::ItemMenu:
			UpdateItemMenu();
			break;
		case UndertaleState::MercyMenu:
			UpdateMercyMenu();
			break;
		case UndertaleState::BulletDodge:
			UpdateBulletDodge();
			break;
		case UndertaleState::Victory:
			UpdateVictory();
			break;
		case UndertaleState::Defeat:
			UpdateDefeat();
			break;
	}
}

void Scene_Battle_Undertale::UpdateMainMenu() {
	if (Input::IsTriggered(Input::RIGHT)) {
		selected_button = (selected_button + 1) % 4;
		UpdateButtonSelection();
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
	}
	if (Input::IsTriggered(Input::LEFT)) {
		selected_button = (selected_button + 3) % 4;
		UpdateButtonSelection();
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
	}

	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));

		switch (static_cast<UndertaleButtonType>(selected_button)) {
			case UndertaleButtonType::Fight:
				// If multiple enemies, go to enemy select first
				if (enemy_group.GetAliveCount() > 1) {
					return_state_after_dialogue = UndertaleState::FightBar;
					SetUndertaleState(UndertaleState::EnemySelect);
				} else {
					selected_enemy = 0;
					auto indices = enemy_group.GetAliveEnemyIndices();
					if (!indices.empty()) selected_enemy = indices[0];
					SetUndertaleState(UndertaleState::FightBar);
				}
				break;
			case UndertaleButtonType::Act:
				SetUndertaleState(UndertaleState::EnemySelect);
				return_state_after_dialogue = UndertaleState::ActMenu;
				break;
			case UndertaleButtonType::Item:
				SetUndertaleState(UndertaleState::ItemMenu);
				break;
			case UndertaleButtonType::Mercy:
				SetUndertaleState(UndertaleState::MercyMenu);
				break;
			default:
				break;
		}
	}
}

void Scene_Battle_Undertale::UpdateFightBar() {
	fight_bar->Update();

	if (Input::IsTriggered(Input::DECISION) && fight_bar->CanStop()) {
		float mult = fight_bar->Stop();
		Output::Debug("UndertaleBattle: Fight bar stopped, multiplier={:.2f}", mult);
		ApplyFightDamage(mult);
	}

	if (fight_bar->IsFinished()) {
		if (waiting_for_effects) {
			// Wait for shake/vaporize to finish before transitioning
			UpdateDamageEffects();
			if (!damage_renderer || !damage_renderer->HasActiveEffects()) {
				waiting_for_effects = false;
				// Check if all enemies are dead
				if (enemy_group.GetAliveCount() == 0) {
					SetUndertaleState(UndertaleState::Victory);
				} else {
					StartEnemyTurn();
				}
			}
		} else {
			// If fight bar finished without hitting (timed out), go to enemy turn
			StartEnemyTurn();
		}
	}
}

void Scene_Battle_Undertale::UpdateDialogue() {
	if (!dialogue) {
		OnDialogueComplete();
		return;
	}

	dialogue->Update();

	if (Input::IsTriggered(Input::DECISION)) {
		if (dialogue->IsPageComplete()) {
			if (!dialogue->NextPage()) {
				OnDialogueComplete();
			}
		} else {
			dialogue->SkipToEnd();
		}
	}
}

void Scene_Battle_Undertale::UpdateEnemySelect() {
	if (!choice_menu) {
		SetUndertaleState(UndertaleState::MainMenu);
		return;
	}

	choice_menu->Update();

	// Move soul to choice cursor position
	if (soul_sprite) {
		soul_sprite->SetX(choice_menu->GetSoulX());
		soul_sprite->SetY(choice_menu->GetSoulY());
		soul_sprite->SetVisible(true);
	}

	if (choice_menu->IsConfirmed()) {
		auto indices = enemy_group.GetAliveEnemyIndices();
		int sel = choice_menu->GetSelectedIndex();
		if (sel >= 0 && sel < static_cast<int>(indices.size())) {
			selected_enemy = indices[sel];
		}
		choice_menu->Close();

		if (return_state_after_dialogue == UndertaleState::ActMenu) {
			SetUndertaleState(UndertaleState::ActMenu);
		} else {
			SetUndertaleState(UndertaleState::FightBar);
		}
	}

	if (choice_menu->IsCancelled()) {
		choice_menu->Close();
		SetUndertaleState(UndertaleState::MainMenu);
	}
}

void Scene_Battle_Undertale::UpdateActMenu() {
	if (!choice_menu) {
		SetUndertaleState(UndertaleState::MainMenu);
		return;
	}

	choice_menu->Update();

	if (soul_sprite) {
		soul_sprite->SetX(choice_menu->GetSoulX());
		soul_sprite->SetY(choice_menu->GetSoulY());
		soul_sprite->SetVisible(true);
	}

	if (choice_menu->IsConfirmed()) {
		std::string action = choice_menu->GetSelectedLabel();
		choice_menu->Close();

		// "Check" action shows enemy stats dialogue
		if (action == "Check") {
			auto* graphic = enemy_group.GetGraphic(selected_enemy);
			if (graphic && graphic->GetEnemy()) {
				auto* enemy = graphic->GetEnemy();
				std::string check_text = std::string(enemy->GetName()) +
					" - ATK " + std::to_string(enemy->GetAtk()) +
					" DEF " + std::to_string(enemy->GetDef());

				UndertaleDialoguePage page;
				page.text = check_text;
				page.char_delay = 2;
				dialogue->SetPages({page});
				return_state_after_dialogue = UndertaleState::MainMenu;
				SetUndertaleState(UndertaleState::Dialogue);
			} else {
				SetUndertaleState(UndertaleState::MainMenu);
			}
		} else {
			// Other ACT actions show a generic dialogue
			UndertaleDialoguePage page;
			page.text = "You " + action + "ed.";
			page.char_delay = 2;
			dialogue->SetPages({page});
			return_state_after_dialogue = UndertaleState::MainMenu;
			SetUndertaleState(UndertaleState::Dialogue);
		}
	}

	if (choice_menu->IsCancelled()) {
		choice_menu->Close();
		SetUndertaleState(UndertaleState::MainMenu);
	}
}

void Scene_Battle_Undertale::UpdateItemMenu() {
	if (!choice_menu) {
		SetUndertaleState(UndertaleState::MainMenu);
		return;
	}

	choice_menu->Update();

	if (soul_sprite) {
		soul_sprite->SetX(choice_menu->GetSoulX());
		soul_sprite->SetY(choice_menu->GetSoulY());
		soul_sprite->SetVisible(true);
	}

	if (choice_menu->IsConfirmed()) {
		int sel = choice_menu->GetSelectedIndex();
		choice_menu->Close();

		// Look up the actual item
		int item_id = (sel >= 0 && sel < static_cast<int>(item_menu_ids.size())) ? item_menu_ids[sel] : 0;
		const auto* db_item = item_id > 0 ? lcf::ReaderUtil::GetElement(lcf::Data::items, item_id) : nullptr;

		std::string item_name = db_item ? ToString(db_item->name) : "item";
		std::string flavour;
		int hp_healed = 0;

		if (db_item && Main_Data::game_party) {
			// Get the lead actor to apply the item
			auto actors = Main_Data::game_party->GetActors();
			Game_Actor* target = actors.empty() ? nullptr : actors[0];

			if (target) {
				int hp_before = target->GetHp();
				target->UseItem(item_id, target);
				hp_healed = target->GetHp() - hp_before;
			}

			// Consume the item from inventory
			Main_Data::game_party->ConsumeItemUse(item_id);

			// Play heal sound if HP was restored
			if (hp_healed > 0) {
				PlayEncounterSound("snd_power");
				flavour = "You recovered " + std::to_string(hp_healed) + " HP!";
			}

			RefreshStatsBar();
		}

		// Build dialogue
		std::vector<UndertaleDialoguePage> pages;
		UndertaleDialoguePage use_page;
		use_page.text = "You used the " + item_name + ".";
		use_page.char_delay = 2;
		pages.push_back(std::move(use_page));

		if (!flavour.empty()) {
			UndertaleDialoguePage heal_page;
			heal_page.text = flavour;
			heal_page.char_delay = 2;
			pages.push_back(std::move(heal_page));
		}

		dialogue->SetPages(std::move(pages));
		// After using an item, it's the enemy's turn
		return_state_after_dialogue = UndertaleState::BulletDodge;
		SetUndertaleState(UndertaleState::Dialogue);
	}

	if (choice_menu->IsCancelled()) {
		choice_menu->Close();
		SetUndertaleState(UndertaleState::MainMenu);
	}
}

void Scene_Battle_Undertale::UpdateMercyMenu() {
	if (!choice_menu) {
		SetUndertaleState(UndertaleState::MainMenu);
		return;
	}

	choice_menu->Update();

	if (soul_sprite) {
		soul_sprite->SetX(choice_menu->GetSoulX());
		soul_sprite->SetY(choice_menu->GetSoulY());
		soul_sprite->SetVisible(true);
	}

	if (choice_menu->IsConfirmed()) {
		std::string action = choice_menu->GetSelectedLabel();
		choice_menu->Close();

		if (action == "Flee") {
			EndBattle(BattleResult::Escape);
			return;
		}

		// Spare action
		UndertaleDialoguePage page;
		page.text = "You spared the enemy.";
		page.char_delay = 2;
		dialogue->SetPages({page});
		return_state_after_dialogue = UndertaleState::MainMenu;
		SetUndertaleState(UndertaleState::Dialogue);
	}

	if (choice_menu->IsCancelled()) {
		choice_menu->Close();
		SetUndertaleState(UndertaleState::MainMenu);
	}
}

void Scene_Battle_Undertale::ApplyFightDamage(float multiplier) {
	auto* graphic = enemy_group.GetGraphic(selected_enemy);
	if (!graphic || !graphic->GetEnemy()) return;

	auto* enemy = graphic->GetEnemy();
	if (enemy->IsDead()) return;

	// Calculate damage: actor ATK * multiplier - enemy DEF
	int atk = 10;
	if (Main_Data::game_party) {
		auto actors = Main_Data::game_party->GetActors();
		if (!actors.empty()) {
			atk = actors[0]->GetAtk();
		}
	}

	int raw_damage = static_cast<int>(atk * multiplier);
	int def = enemy->GetDef();
	int damage = std::max(1, raw_damage - def);

	// Apply damage
	enemy->ChangeHp(-damage, true);

	// Play slash sound
	PlayEncounterSound("snd_laz");

	// Show damage number above enemy
	int dmg_x = graphic->GetX() + graphic->GetGraphicWidth() / 2 - 8;
	int dmg_y = graphic->GetY() - 12;
	if (damage_renderer) {
		damage_renderer->ShowDamage(dmg_x, dmg_y, damage);
	}

	// Play damage sound
	PlayEncounterSound("snd_damage_c");

	// Shake the enemy
	if (damage_renderer) {
		damage_renderer->StartShake(selected_enemy, graphic);
	}

	// Check for death
	if (enemy->IsDead()) {
		Output::Debug("UndertaleBattle: Enemy {} defeated!", selected_enemy);
		PlayEncounterSound("snd_vaporized");
		if (damage_renderer) {
			damage_renderer->StartVaporize(selected_enemy, graphic);
		}
	}

	waiting_for_effects = true;
}

void Scene_Battle_Undertale::StartEnemyTurn() {
	if (enemy_group.GetAliveCount() == 0) {
		SetUndertaleState(UndertaleState::Victory);
		return;
	}

	SetUndertaleState(UndertaleState::BulletDodge);
}

void Scene_Battle_Undertale::UpdateBulletDodge() {
	if (!bullet_manager || !soul_sprite) {
		SetUndertaleState(UndertaleState::MainMenu);
		return;
	}

	// Wait for box resize animation to finish before starting bullets
	if (box_animating) {
		return;
	}

	// Start the bullet pattern once the box is fully open (first frame after anim)
	if (!bullet_manager->IsActive()) {
		bool is_tough = false;
		const auto* troop = lcf::ReaderUtil::GetElement(lcf::Data::troops, troop_id);
		if (troop) {
			is_tough = ShouldPlayStrongerBattleMusic(*troop);
		}
		auto fire_pattern = std::make_unique<FireBulletPattern>();
		bullet_manager->StartPattern(std::move(fire_pattern),
			battle_box->GetInnerX(), battle_box->GetInnerY(),
			battle_box->GetInnerWidth(), battle_box->GetInnerHeight(),
			is_tough);
	}

	// Move soul with directional input using sub-pixel accumulation
	int soul_w = static_cast<int>(std::round(kSoulSize * kSoulScale));
	int soul_h = static_cast<int>(std::round(kSoulSize * kSoulScale));

	// Holding X (CANCEL) slows the soul down
	float speed = kSoulMoveSpeed;
	if (Input::IsPressed(Input::CANCEL)) {
		speed *= kSoulSlowFactor;
	}

	if (Input::IsPressed(Input::UP)) {
		soul_sub_y -= speed;
	}
	if (Input::IsPressed(Input::DOWN)) {
		soul_sub_y += speed;
	}
	if (Input::IsPressed(Input::LEFT)) {
		soul_sub_x -= speed;
	}
	if (Input::IsPressed(Input::RIGHT)) {
		soul_sub_x += speed;
	}

	// Constrain to battle box inner bounds
	if (battle_box) {
		float min_x = static_cast<float>(battle_box->GetInnerX());
		float min_y = static_cast<float>(battle_box->GetInnerY());
		float max_x = static_cast<float>(battle_box->GetInnerX() + battle_box->GetInnerWidth() - soul_w);
		float max_y = static_cast<float>(battle_box->GetInnerY() + battle_box->GetInnerHeight() - soul_h);
		soul_sub_x = Utils::Clamp(soul_sub_x, min_x, max_x);
		soul_sub_y = Utils::Clamp(soul_sub_y, min_y, max_y);
	}

	int soul_x = static_cast<int>(std::round(soul_sub_x));
	int soul_y = static_cast<int>(std::round(soul_sub_y));
	soul_sprite->SetX(soul_x);
	soul_sprite->SetY(soul_y);

	// Update bullet manager and check for completion
	bool done = bullet_manager->Update(soul_x, soul_y, soul_w, soul_h);

	// Check if player was hit (and not in iframes)
	if (bullet_manager->HitThisFrame() && iframe_frames == 0) {
		// Calculate damage from enemy ATK vs player DEF
		int enemy_atk = 5;
		auto alive = enemy_group.GetAliveEnemyIndices();
		if (!alive.empty()) {
			auto* g = enemy_group.GetGraphic(alive[0]);
			if (g && g->GetEnemy()) {
				enemy_atk = g->GetEnemy()->GetAtk();
			}
		}

		// Average DEF across all party members
		int player_def = 5;
		if (Main_Data::game_party) {
			auto actors = Main_Data::game_party->GetActors();
			if (!actors.empty()) {
				int total_def = 0;
				for (auto* a : actors) {
					total_def += a->GetDef();
				}
				player_def = total_def / static_cast<int>(actors.size());
			}
		}

		int damage = std::max(1, enemy_atk - player_def);

		// Spread damage across all alive party members
		if (Main_Data::game_party) {
			auto actors = Main_Data::game_party->GetActors();
			int remaining = damage;
			for (auto* a : actors) {
				if (a->IsDead() || remaining <= 0) continue;
				int share = std::min(remaining, a->GetHp());
				a->ChangeHp(-share, true);
				remaining -= share;
			}
		}

		// Play hurt sound
		PlayEncounterSound("snd_hurt1_c");

		// Start iframes
		iframe_frames = kIframeDuration;

		// Refresh stats bar to show new HP
		RefreshStatsBar();

		// Check if all party members are dead
		bool all_dead = true;
		if (Main_Data::game_party) {
			for (auto* a : Main_Data::game_party->GetActors()) {
				if (!a->IsDead()) { all_dead = false; break; }
			}
		}
		if (all_dead) {
			bullet_manager->Clear();
			SetUndertaleState(UndertaleState::Defeat);
			return;
		}
	}

	// Bullet dodge phase complete — back to player turn
	if (done) {
		SetUndertaleState(UndertaleState::MainMenu);
	}
}

void Scene_Battle_Undertale::UpdateDamageEffects() {
	if (!damage_renderer) return;

	damage_renderer->UpdatePopups();
	damage_renderer->UpdateEffects([this](int index) -> UndertaleEnemyGraphic* {
		return enemy_group.GetGraphic(index);
	});
}

void Scene_Battle_Undertale::ResizeBoxForDodge() {
	if (!battle_box) return;

	constexpr int dodge_size = 155;
	int dodge_x = (SCREEN_TARGET_WIDTH - dodge_size) / 2;
	int dodge_y = default_box_y + default_box_h - dodge_size;

	box_from_x = battle_box->GetBoxX();
	box_from_y = battle_box->GetBoxY();
	box_from_w = battle_box->GetBoxWidth();
	box_from_h = battle_box->GetBoxHeight();
	box_to_x = dodge_x;
	box_to_y = dodge_y;
	box_to_w = dodge_size;
	box_to_h = dodge_size;
	box_anim_frame = 0;
	box_animating = true;
	box_anim_opening = true;
}

void Scene_Battle_Undertale::RestoreBoxDefault() {
	if (!battle_box) return;

	box_from_x = battle_box->GetBoxX();
	box_from_y = battle_box->GetBoxY();
	box_from_w = battle_box->GetBoxWidth();
	box_from_h = battle_box->GetBoxHeight();
	box_to_x = default_box_x;
	box_to_y = default_box_y;
	box_to_w = default_box_w;
	box_to_h = default_box_h;
	box_anim_frame = 0;
	box_animating = true;
	box_anim_opening = false;
}

bool Scene_Battle_Undertale::UpdateBoxAnimation() {
	if (!box_animating || !battle_box) return true;

	++box_anim_frame;
	float t = static_cast<float>(box_anim_frame) / kBoxResizeFrames;
	if (t >= 1.0f) t = 1.0f;

	int cx = box_from_x + static_cast<int>((box_to_x - box_from_x) * t);
	int cy = box_from_y + static_cast<int>((box_to_y - box_from_y) * t);
	int cw = box_from_w + static_cast<int>((box_to_w - box_from_w) * t);
	int ch = box_from_h + static_cast<int>((box_to_h - box_from_h) * t);
	battle_box->SetBounds(cx, cy, cw, ch);

	if (t >= 1.0f) {
		box_animating = false;

		// After restoring, re-sync dialogue/choice bounds
		if (!box_anim_opening) {
			if (dialogue) {
				dialogue->SetBounds(battle_box->GetInnerX(), battle_box->GetInnerY(),
					battle_box->GetInnerWidth(), battle_box->GetInnerHeight());
			}
			if (choice_menu) {
				choice_menu->SetBounds(battle_box->GetInnerX(), battle_box->GetInnerY(),
					battle_box->GetInnerWidth(), battle_box->GetInnerHeight());
			}
		}
		return true;
	}
	return false;
}

void Scene_Battle_Undertale::DrawBackground(Bitmap& dst) {
	// Undertale uses a solid black background
	dst.Fill(Color(0, 0, 0, 255));
}

void Scene_Battle_Undertale::UpdateVictory() {
	if (!victory_rewards_given) {
		victory_rewards_given = true;

		// Stop battle music
		Audio().BGM_Stop();

		// Calculate total EXP and gold from all enemies
		int total_exp = 0;
		int total_gold = 0;
		auto enemies = Main_Data::game_enemyparty->GetEnemies();
		for (auto* enemy : enemies) {
			total_exp += enemy->GetExp();
			total_gold += enemy->GetMoney();
		}

		// Grant EXP to all party members, tracking level ups
		bool leveled_up = false;
		if (Main_Data::game_party) {
			for (auto* actor : Main_Data::game_party->GetActors()) {
				int old_level = actor->GetLevel();
				actor->ChangeExp(actor->GetExp() + total_exp, nullptr);
				if (actor->GetLevel() > old_level) {
					leveled_up = true;
				}
			}
			// Grant gold
			Main_Data::game_party->GainGold(total_gold);
		}

		// Play level up sound if applicable
		if (leveled_up) {
			PlayEncounterSound("snd_levelup");
		}

		// Show victory dialogue
		if (dialogue) {
			UndertaleDialoguePage page;
			page.text = "YOU WON!\nYou earned " + std::to_string(total_exp) +
				" EXP and " + std::to_string(total_gold) + " gold.";
			if (leveled_up) {
				page.text += "\nYour LOVE increased.";
			}
			page.char_delay = 2;
			dialogue->SetPages({page});
			return_state_after_dialogue = UndertaleState::Victory;
			RestoreBoxDefault();
			SetUiVisible(true);
			ut_state = UndertaleState::Dialogue;
		}
		return;
	}

	// Rewards already given and dialogue finished — end the battle
	EndBattle(BattleResult::Victory);
}

void Scene_Battle_Undertale::SetUndertaleState(UndertaleState new_state) {
	ut_state = new_state;

	// Hide choice menu and dialogue by default; states that need them will show them
	if (choice_menu && new_state != UndertaleState::EnemySelect &&
		new_state != UndertaleState::ActMenu && new_state != UndertaleState::ItemMenu &&
		new_state != UndertaleState::MercyMenu) {
		choice_menu->Close();
	}

	// Hide dialogue when leaving to a state that doesn't use it
	if (dialogue && new_state != UndertaleState::Dialogue && new_state != UndertaleState::MainMenu) {
		dialogue->Reset();
	}

	if (new_state == UndertaleState::EncounterIntro) {
		SetUiVisible(false);
		encounter_phase = EncounterIntroPhase::Flashing;
		encounter_frame = 0;

		if (Main_Data::game_player) {
			const int player_x = Main_Data::game_player->GetScreenX();
			const int player_y = Main_Data::game_player->GetScreenY() - 8;
			soul_start_x = Utils::Clamp(player_x - kSoulSize / 2, 0, SCREEN_TARGET_WIDTH - kSoulSize);
			soul_start_y = Utils::Clamp(player_y - kSoulSize / 2, 0, SCREEN_TARGET_HEIGHT - kSoulSize);
		} else {
			soul_start_x = (SCREEN_TARGET_WIDTH - kSoulSize) / 2;
			soul_start_y = (SCREEN_TARGET_HEIGHT - kSoulSize) / 2;
		}

		UpdateSoulSelectionPosition();
		if (soul_sprite) {
			soul_sprite->SetX(soul_start_x);
			soul_sprite->SetY(soul_start_y);
			soul_sprite->SetVisible(false);
		}
	}

	if (new_state == UndertaleState::MainMenu) {
		RestoreBoxDefault();
		SetUiVisible(true);
		UpdateSoulSelectionPosition();
		RefreshStatsBar();
		if (soul_sprite) {
			soul_sprite->SetVisible(true);
		}
		// Show encounter dialogue the first time we enter MainMenu
		// (handled separately via ShowEncounterDialogue)
	} else if (new_state != UndertaleState::EncounterIntro && soul_sprite) {
		// Soul visibility is managed per-state below
	}

	// Fight bar
	if (fight_bar) {
		if (new_state == UndertaleState::FightBar) {
			int bar_x = battle_box->GetInnerX() + (battle_box->GetInnerWidth() - UndertaleFightBar::BAR_DISPLAY_WIDTH) / 2;
			int bar_y = battle_box->GetInnerY() + (battle_box->GetInnerHeight() - UndertaleFightBar::BAR_DISPLAY_HEIGHT) / 2;
			fight_bar->SetPosition(bar_x, bar_y);
			fight_bar->Start();
			if (soul_sprite) soul_sprite->SetVisible(false);
		} else {
			fight_bar->SetVisible(false);
		}
	}

	// Dialogue state — soul stays visible so it isn't interrupted

	// Enemy select: populate choice with alive enemy names
	if (new_state == UndertaleState::EnemySelect && choice_menu) {
		auto names = enemy_group.GetAliveEnemyNames();
		std::vector<UndertaleChoiceItem> items;
		for (auto& n : names) {
			items.push_back({n, true});
		}
		choice_menu->Open(std::move(items));
		if (soul_sprite) soul_sprite->SetVisible(true);
	}

	// ACT menu: populate with actions for selected enemy
	if (new_state == UndertaleState::ActMenu && choice_menu) {
		BuildActChoices(selected_enemy);
		if (soul_sprite) soul_sprite->SetVisible(true);
	}

	// Item menu: populate with party items
	if (new_state == UndertaleState::ItemMenu && choice_menu) {
		std::vector<UndertaleChoiceItem> items;
		item_menu_ids.clear();
		if (Main_Data::game_party) {
			std::vector<int> raw_ids;
			Main_Data::game_party->GetItems(raw_ids);
			for (int id : raw_ids) {
				const auto* db_item = lcf::ReaderUtil::GetElement(lcf::Data::items, id);
				if (db_item) {
					int count = Main_Data::game_party->GetItemCount(id);
					std::string label = ToString(db_item->name);
					if (count > 1) {
						label += " x" + std::to_string(count);
					}
					items.push_back({label, true});
					item_menu_ids.push_back(id);
				}
			}
		}
		if (items.empty()) {
			items.push_back({"(No items)", false});
		}
		choice_menu->Open(std::move(items));
		if (soul_sprite) soul_sprite->SetVisible(true);
	}

	// Mercy menu
	if (new_state == UndertaleState::MercyMenu && choice_menu) {
		std::vector<UndertaleChoiceItem> items;
		items.push_back({"Spare", true});
		if (IsEscapeAllowed()) {
			items.push_back({"Flee", true});
		}
		choice_menu->Open(std::move(items));
		if (soul_sprite) soul_sprite->SetVisible(true);
	}

	// Bullet dodge phase: resize box, move soul to center, start bullet pattern
	if (new_state == UndertaleState::BulletDodge) {
		// Start animating the battle box to a square arena for dodging
		ResizeBoxForDodge();

		if (soul_sprite && battle_box) {
			// Position soul at center of the *target* dodge box
			int soul_w = static_cast<int>(std::round(kSoulSize * kSoulScale));
			int soul_h = static_cast<int>(std::round(kSoulSize * kSoulScale));
			int cx = box_to_x + 2 + (box_to_w - 4 - soul_w) / 2;
			int cy = box_to_y + 2 + (box_to_h - 4 - soul_h) / 2;
			soul_sprite->SetX(cx);
			soul_sprite->SetY(cy);
			soul_sub_x = static_cast<float>(cx);
			soul_sub_y = static_cast<float>(cy);
			soul_sprite->SetVisible(true);
		}

		// Bullet pattern will be started in UpdateBulletDodge once animation finishes

		// Hide menu buttons during dodge phase
		for (auto& button : buttons) {
			if (button) button->SetVisible(false);
		}

		// Reset iframes
		iframe_frames = 0;
	}

	// Defeat: initialize pre-gameover animation
	if (new_state == UndertaleState::Defeat) {
		defeat_phase = DefeatPhase::BlackOut;
		defeat_frame = 0;
		broken_heart_sprite.reset();
		defeat_shards.clear();

		// Hide all UI elements
		SetUiVisible(false);
		if (dialogue) dialogue->Reset();
		if (choice_menu) choice_menu->Close();
		if (fight_bar) fight_bar->SetVisible(false);
		if (soul_sprite) soul_sprite->SetVisible(false);
	}
}

void Scene_Battle_Undertale::LayoutEnemies() {
	if (!battle_box) return;

	// Enemies go above the battle box, in the upper portion of the screen
	int area_x = 0;
	int area_y = 8;
	int area_w = SCREEN_TARGET_WIDTH;
	int area_h = battle_box->GetBoxY() - area_y - 4;

	enemy_group.LayoutEnemies(area_x, area_y, area_w, area_h);
}

void Scene_Battle_Undertale::RefreshStatsBar() {
	if (!stats_bar) return;

	// Sum HP from all party members (combined HP pool)
	int lv = 1;
	int cur_hp = 0;
	int cur_max_hp = 0;
	std::string name = "Chara";

	if (Main_Data::game_party) {
		auto actors = Main_Data::game_party->GetActors();
		if (!actors.empty()) {
			// Use first actor's name and highest level
			name = std::string(actors[0]->GetName());
			for (auto* actor : actors) {
				cur_hp += actor->GetHp();
				cur_max_hp += actor->GetMaxHp();
				if (actor->GetLevel() > lv) {
					lv = actor->GetLevel();
				}
			}
		}
	}

	stats_bar->SetStats(lv, cur_hp, cur_max_hp, name);
}

void Scene_Battle_Undertale::UpdateStatsBar() {
	RefreshStatsBar();
}

void Scene_Battle_Undertale::ShowEncounterDialogue() {
	if (!dialogue) return;

	// Build encounter text from enemies
	auto names = enemy_group.GetAliveEnemyNames();
	std::string encounter_text;
	if (names.size() == 1) {
		encounter_text = names[0] + " blocks the way!";
	} else if (names.size() > 1) {
		encounter_text = names[0];
		for (size_t i = 1; i < names.size(); ++i) {
			if (i == names.size() - 1) {
				encounter_text += " and " + names[i];
			} else {
				encounter_text += ", " + names[i];
			}
		}
		encounter_text += " block the way!";
	} else {
		encounter_text = "But nobody came.";
	}

	UndertaleDialoguePage page;
	page.text = encounter_text;
	page.char_delay = 2;
	dialogue->SetPages({page});
	return_state_after_dialogue = UndertaleState::MainMenu;
}

void Scene_Battle_Undertale::BuildActChoices(int enemy_index) {
	if (!choice_menu) return;

	std::vector<UndertaleChoiceItem> items;

	// "Check" is always the first ACT option in Undertale
	items.push_back({"Check", true});

	// Add generic ACT options based on enemy name
	auto* graphic = enemy_group.GetGraphic(enemy_index);
	if (graphic && graphic->GetEnemy()) {
		// Standard generic actions
		items.push_back({"Talk", true});
		items.push_back({"Flirt", true});
		items.push_back({"Threaten", true});
	}

	choice_menu->Open(std::move(items));
}

void Scene_Battle_Undertale::OnDialogueComplete() {
	if (dialogue) {
		dialogue->Reset();
	}
	SetUndertaleState(return_state_after_dialogue);
}

void Scene_Battle_Undertale::SetState(Scene_Battle::State new_state) {
	// Map base battle states to Undertale states where applicable
	switch (new_state) {
		case State_Victory:
			SetUndertaleState(UndertaleState::Victory);
			break;
		case State_Defeat:
			SetUndertaleState(UndertaleState::Defeat);
			break;
		default:
			break;
	}
	state = new_state;
}

void Scene_Battle_Undertale::UpdateDefeat() {
	defeat_frame++;

	switch (defeat_phase) {
	case DefeatPhase::BlackOut: {
		// Frame 1: stop music, hide all UI and enemies
		if (defeat_frame == 1) {
			Audio().BGM_Stop();
			Audio().SE_Stop();
			SetUiVisible(false);
			if (dialogue) dialogue->Reset();
			if (choice_menu) choice_menu->Close();

			// Hide soul during blackout, we'll show it after a beat
			if (soul_sprite) {
				soul_sprite->SetVisible(false);
			}
		}
		// After 30 frames of black, show the heart at screen center
		if (defeat_frame >= 30) {
			if (soul_sprite) {
				int soul_w = static_cast<int>(std::round(kSoulSize * kSoulScale));
				int soul_h = static_cast<int>(std::round(kSoulSize * kSoulScale));
				soul_sprite->SetX((SCREEN_TARGET_WIDTH - soul_w) / 2);
				soul_sprite->SetY((SCREEN_TARGET_HEIGHT - soul_h) / 2);
				soul_sprite->SetVisible(true);
			}
			defeat_phase = DefeatPhase::HeartBreak;
			defeat_frame = 0;
		}
		break;
	}
	case DefeatPhase::HeartBreak: {
		// Show heart for 20 frames, then crack it
		if (defeat_frame == 20) {
			// Load broken heart sprite
			auto chaos_fs = FileFinder::ChaosAssets();
			if (chaos_fs) {
				auto stream = chaos_fs.OpenFile("UndertaleMode.content/Sprite/Other", "soul_broken", FileFinder::IMG_TYPES);
				if (stream) {
					auto bitmap = Bitmap::Create(std::move(stream), true);
					broken_heart_sprite = std::make_unique<Sprite>();
					broken_heart_sprite->SetBitmap(bitmap);
					broken_heart_sprite->SetZ(Priority_Window + 5);
					broken_heart_sprite->SetZoomX(kSoulScale);
					broken_heart_sprite->SetZoomY(kSoulScale);

					int bw = static_cast<int>(std::round(bitmap->GetWidth() * kSoulScale));
					int bh = static_cast<int>(std::round(bitmap->GetHeight() * kSoulScale));
					broken_heart_sprite->SetX((SCREEN_TARGET_WIDTH - bw) / 2);
					broken_heart_sprite->SetY((SCREEN_TARGET_HEIGHT - bh) / 2);
				}
			}

			// Hide the normal soul, show broken version
			if (soul_sprite) soul_sprite->SetVisible(false);

			PlayEncounterSound("snd_break1");

			defeat_phase = DefeatPhase::BreakPause;
			defeat_frame = 0;
		}
		break;
	}
	case DefeatPhase::BreakPause: {
		// Slow pause — 80 frames (~1.3 seconds)
		if (defeat_frame >= 80) {
			// Hide broken heart, create shards
			if (broken_heart_sprite) {
				broken_heart_sprite->SetVisible(false);
			}

			PlayEncounterSound("snd_break2");

			// Load shard bitmap
			auto chaos_fs = FileFinder::ChaosAssets();
			BitmapRef shard_bmp;
			if (chaos_fs) {
				auto stream = chaos_fs.OpenFile("UndertaleMode.content/Sprite/Other", "soul_shard", FileFinder::IMG_TYPES);
				if (stream) {
					shard_bmp = Bitmap::Create(std::move(stream), true);
				}
			}

			if (shard_bmp) {
				// Center position
				float cx = static_cast<float>(SCREEN_TARGET_WIDTH) / 2.0f;
				float cy = static_cast<float>(SCREEN_TARGET_HEIGHT) / 2.0f;

				// 4 shards with different initial velocities
				// Two go left-down, two go right-down, with slight variation
				struct ShardInit {
					float vx, vy, rot_speed;
				};
				ShardInit inits[4] = {
					{ -1.8f, -3.0f,  0.06f },  // upper-left
					{  1.8f, -3.0f, -0.06f },  // upper-right
					{ -1.0f, -1.5f,  0.04f },  // lower-left
					{  1.0f, -1.5f, -0.04f },  // lower-right
				};

				defeat_shards.clear();
				for (int i = 0; i < 4; i++) {
					Shard s;
					s.sprite = std::make_unique<Sprite>();
					s.sprite->SetBitmap(shard_bmp);
					s.sprite->SetZ(Priority_Window + 6);
					s.sprite->SetZoomX(kSoulScale);
					s.sprite->SetZoomY(kSoulScale);

					int sw = static_cast<int>(std::round(shard_bmp->GetWidth() * kSoulScale));
					int sh = static_cast<int>(std::round(shard_bmp->GetHeight() * kSoulScale));
					// Set origin to center for rotation
					s.sprite->SetOx(shard_bmp->GetWidth() / 2);
					s.sprite->SetOy(shard_bmp->GetHeight() / 2);

					s.x = cx;
					s.y = cy;
					s.vx = inits[i].vx;
					s.vy = inits[i].vy;
					s.angle = 0.0f;
					s.rot_speed = inits[i].rot_speed;
					s.gravity = 0.12f;

					s.sprite->SetX(static_cast<int>(s.x));
					s.sprite->SetY(static_cast<int>(s.y));
					defeat_shards.push_back(std::move(s));
				}
			}

			defeat_phase = DefeatPhase::Shards;
			defeat_frame = 0;
		}
		break;
	}
	case DefeatPhase::Shards: {
		// Update shard physics
		bool all_offscreen = true;
		for (auto& s : defeat_shards) {
			s.vy += s.gravity;  // gravity pulls down
			s.x += s.vx;
			s.y += s.vy;
			s.angle += s.rot_speed;

			s.sprite->SetX(static_cast<int>(s.x));
			s.sprite->SetY(static_cast<int>(s.y));
			s.sprite->SetAngle(static_cast<double>(s.angle));

			if (s.y < SCREEN_TARGET_HEIGHT + 40) {
				all_offscreen = false;
			}
		}

		// After shards fall off screen (or after 120 frames max), proceed
		if (all_offscreen || defeat_frame >= 120) {
			// Clean up shard sprites
			defeat_shards.clear();
			defeat_phase = DefeatPhase::Done;
			defeat_frame = 0;
		}
		break;
	}
	case DefeatPhase::Done: {
		// Brief black pause before gameover
		if (defeat_frame >= 30) {
			Scene::Push(std::make_shared<Scene_Gameover>());
		}
		break;
	}
	}
}

} // namespace Chaos
