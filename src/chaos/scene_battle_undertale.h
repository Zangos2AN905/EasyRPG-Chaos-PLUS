/*
 * Chaos Fork: Undertale Battle Scene
 * Scene_Battle subclass implementing Undertale-style battle flow.
 * States: MainMenu, FightBar, Dialogue, ActMenu, MercyMenu, EnemySelect,
 *         ItemMenu, BulletDodge, Victory, Defeat.
 */

#ifndef EP_CHAOS_SCENE_BATTLE_UNDERTALE_H
#define EP_CHAOS_SCENE_BATTLE_UNDERTALE_H

#include "scene_battle.h"
#include "chaos/undertale_battle_ui.h"
#include "chaos/undertale_fight_bar.h"
#include "chaos/undertale_dialogue.h"
#include "chaos/undertale_choice.h"
#include "chaos/undertale_stats_bar.h"
#include "chaos/undertale_enemy_graphic.h"
#include "chaos/undertale_bullet.h"
#include "chaos/undertale_damage.h"
#include "bitmap.h"

#include <array>
#include <memory>
#include <vector>
#include <cmath>

namespace Chaos {

class Scene_Battle_Undertale : public Scene_Battle {
public:
	explicit Scene_Battle_Undertale(const BattleArgs& args);
	~Scene_Battle_Undertale() override;

	void Start() override;
	void vUpdate() override;
	void DrawBackground(Bitmap& dst) override;
	void TransitionIn(SceneType prev_scene) override;
	void TransitionOut(SceneType next_scene) override;

	enum class UndertaleState {
		/** Custom encounter intro with flashing soul */
		EncounterIntro,
		/** Player picks Fight / Act / Item / Mercy */
		MainMenu,
		/** Fight bar timing minigame */
		FightBar,
		/** Text dialogue display */
		Dialogue,
		/** Choosing an enemy to target (for Fight/ACT) */
		EnemySelect,
		/** ACT sub-menu (Check, specific actions) */
		ActMenu,
		/** Item selection list */
		ItemMenu,
		/** MERCY sub-menu (Spare/Flee) */
		MercyMenu,
		/** Bullet dodging phase (enemy turn) */
		BulletDodge,
		/** Battle victory */
		Victory,
		/** Battle defeat */
		Defeat,
	};

protected:
	void SetState(Scene_Battle::State new_state) override;
	void CreateUi() override;

private:
	void SetUndertaleState(UndertaleState new_state);
	void LoadSoulSprite();
	void SetUiVisible(bool visible);
	void UpdateEncounterIntro();
	void UpdateSoulSelectionPosition();
	void BeginSoulFall();
	void PlayEncounterSound(const char* name);

	void UpdateMainMenu();
	void UpdateFightBar();
	void UpdateDialogue();
	void UpdateEnemySelect();
	void UpdateActMenu();
	void UpdateItemMenu();
	void UpdateMercyMenu();
	void UpdateVictory();
	void UpdateBulletDodge();
	void UpdateDamageEffects();
	void UpdateDefeat();

	/** Apply fight bar damage to the selected enemy */
	void ApplyFightDamage(float multiplier);

	/** Start the bullet dodge phase (enemy turn) */
	void StartEnemyTurn();

	void LayoutButtons();
	void LayoutEnemies();
	void UpdateButtonSelection();
	void UpdateStatsBar();
	void RefreshStatsBar();

	/** Show encounter opening dialogue ("* Enemy draws near!" etc.) */
	void ShowEncounterDialogue();

	/** Populate ACT choices for the selected enemy */
	void BuildActChoices(int enemy_index);

	/** Called after dialogue completes — decides what happens next */
	void OnDialogueComplete();

	UndertaleState ut_state = UndertaleState::MainMenu;
	UndertaleState return_state_after_dialogue = UndertaleState::MainMenu;

	enum class EncounterIntroPhase {
		Flashing,
		Falling,
	};
	EncounterIntroPhase encounter_phase = EncounterIntroPhase::Flashing;
	int selected_button = 0; // 0=Fight, 1=Act, 2=Item, 3=Mercy
	int selected_enemy = 0;
	int encounter_frame = 0;
	int soul_start_x = 0;
	int soul_start_y = 0;
	int soul_target_x = 0;
	int soul_target_y = 0;

	std::array<std::unique_ptr<UndertaleButton>, 4> buttons;
	std::unique_ptr<UndertaleBattleBox> battle_box;
	std::unique_ptr<UndertaleFightBar> fight_bar;
	std::unique_ptr<Sprite> soul_sprite;
	std::unique_ptr<UndertaleDialogue> dialogue;
	std::unique_ptr<UndertaleChoiceMenu> choice_menu;
	std::unique_ptr<UndertaleStatsBar> stats_bar;
	std::unique_ptr<UndertaleBulletManager> bullet_manager;
	std::unique_ptr<UndertaleDamageRenderer> damage_renderer;
	UndertaleEnemyGroup enemy_group;

	// Player iframes tracking
	int iframe_frames = 0;
	static constexpr int kIframeDuration = 40; // ~0.67 seconds of invincibility
	static constexpr int kIframeFlashRate = 4;  // soul flashes every N frames

	// Soul movement speed during bullet dodge
	// Undertale: 4.305 px/frame at 30fps -> 2.1525 px/frame at 60fps
	static constexpr float kSoulMoveSpeed = 2.1525f;
	static constexpr float kSoulSlowFactor = 0.5f; // hold X to move at half speed

	// Sub-pixel soul position accumulator
	float soul_sub_x = 0.0f;
	float soul_sub_y = 0.0f;

	// Default battle box dimensions (set during LayoutButtons, restored after BulletDodge)
	int default_box_x = 0;
	int default_box_y = 0;
	int default_box_w = 0;
	int default_box_h = 0;

	// Battle box resize animation
	int box_anim_frame = 0;
	static constexpr int kBoxResizeFrames = 15;
	int box_from_x = 0, box_from_y = 0, box_from_w = 0, box_from_h = 0;
	int box_to_x = 0, box_to_y = 0, box_to_w = 0, box_to_h = 0;
	bool box_animating = false;
	bool box_anim_opening = false; // true = resizing to dodge, false = restoring

	/** Start animating the battle box toward the dodge arena size */
	void ResizeBoxForDodge();
	/** Start animating the battle box back to default size */
	void RestoreBoxDefault();
	/** Tick the box resize animation. Returns true when animation is done. */
	bool UpdateBoxAnimation();

	// Waiting for damage effects to finish before transitioning
	bool waiting_for_effects = false;
	UndertaleState post_effects_state = UndertaleState::MainMenu;

	// Victory state tracking
	bool victory_rewards_given = false;

	// Item menu: maps choice index -> item ID
	std::vector<int> item_menu_ids;

	// --- Defeat (pre-gameover) animation ---
	enum class DefeatPhase {
		BlackOut,    // Fade to black, show heart
		HeartBreak,  // Show broken heart + snd_break1
		BreakPause,  // Slow pause on broken heart
		Shards,      // Shards fly apart + snd_break2
		Done,        // Push gameover scene
	};
	DefeatPhase defeat_phase = DefeatPhase::BlackOut;
	int defeat_frame = 0;

	std::unique_ptr<Sprite> broken_heart_sprite;

	struct Shard {
		std::unique_ptr<Sprite> sprite;
		float x, y;       // position
		float vx, vy;     // velocity
		float angle;       // current rotation (radians)
		float rot_speed;   // rotation speed (radians/frame)
		float gravity;     // downward acceleration
	};
	std::vector<Shard> defeat_shards;
};

} // namespace Chaos

#endif
