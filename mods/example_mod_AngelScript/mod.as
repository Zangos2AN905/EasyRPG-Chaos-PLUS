// ============================================================
// Example AngelScript Mod for EasyRPG Chaos Fork
// ============================================================
// AngelScript mods have FULL access:
//   - Undertale attacks (bullet patterns)
//   - Horror mode enemies
//   - Full gamemodes (custom scenes, UI hooks)
//   - Game state access
//
// API Reference:
//   chaos.log(string)               - Print to debug log
//   chaos.spawn_bullet(x,y,vx,vy,hw,hh)        - Spawn a bullet
//   chaos.spawn_bullet(x,y,vx,vy,hw,hh,sprite) - Spawn with custom sprite
//   chaos.box_x(), box_y(), box_w(), box_h()    - Battle box dimensions
//   chaos.frame()                               - Current frame number
//   chaos.is_tough()                            - Tough difficulty?
//   chaos.sin(v), cos(v)                        - Trig functions
//   chaos.rand01()                              - Random float 0..1
//   chaos.rand_int(lo, hi)                      - Random integer
//   register_attack(id, name, duration)         - Register an attack
//   register_enemy(id, name, hp, atk, def, sprite) - Register an enemy
//   register_gamemode(id, name, description)    - Register a gamemode
// ============================================================

// Called when the mod is loaded. Register all content here.
void init() {
    log("Example AngelScript mod loading...");

    // Register a spiral bullet attack
    register_attack("spiral", "Spiral Barrage", 360);

    // Register a horror enemy
    register_enemy("shadow_beast", "Shadow Beast", 150, 20, 8, "shadow_beast");

    // Register a custom gamemode
    register_gamemode("chaos_plus", "Chaos+",
        "An enhanced chaos mode with scripted encounters!");

    log("Example AngelScript mod loaded!");
}

// ============================================================
// Attack: spiral
// Fires bullets in a rotating spiral pattern
// ============================================================

void spiral_start() {
    log("Spiral attack starting!");
}

// Called every frame. Return true when done.
bool spiral_update() {
    int f = frame();
    int bx = box_x();
    int by = box_y();
    int bw = box_w();
    int bh = box_h();

    // Center of the box
    float cx = bx + bw / 2.0f;
    float cy = by + bh / 2.0f;

    // Spawn bullets in a spiral every 8 frames
    int interval = is_tough() ? 5 : 8;
    if (f % interval == 0) {
        float angle = f * 0.15f; // Rotate over time
        int arms = is_tough() ? 4 : 3;
        float speed = is_tough() ? 1.8f : 1.2f;

        for (int i = 0; i < arms; i++) {
            float a = angle + (i * 6.283185f / arms);
            float vx = cos(a) * speed;
            float vy = sin(a) * speed;
            spawn_bullet(cx, cy, vx, vy, 4, 4);
        }
    }

    return f >= 360; // End after 6 seconds
}

// ============================================================
// Enemy: shadow_beast
// A simple horror enemy with behavior hooks
// ============================================================

void shadow_beast_spawn() {
    log("Shadow Beast has appeared!");
}

void shadow_beast_update() {
    // Could add custom AI behaviors here
}

// ============================================================
// Gamemode: chaos_plus
// A custom gamemode with full scene control
// ============================================================

void chaos_plus_gamemode_start() {
    log("Chaos+ mode activated!");
}

void chaos_plus_gamemode_update() {
    // Per-frame gamemode logic
}

void chaos_plus_battle_start() {
    log("Chaos+ battle beginning!");
}

void chaos_plus_battle_end() {
    log("Chaos+ battle ended!");
}
