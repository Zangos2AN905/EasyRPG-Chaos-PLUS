-- ============================================================
-- Example Lua Mod for EasyRPG Chaos Fork
-- ============================================================
-- Lua mods have LIMITED access:
--   - Undertale attacks (bullet patterns)
--   - Horror mode enemies
--   - Basic gamemodes (name + hooks only)
--
-- API Reference (via the "chaos" global table):
--   chaos.log(msg)                    - Print to debug log
--   chaos.spawn_bullet(x,y,vx,vy,hw,hh)        - Spawn a bullet
--   chaos.spawn_bullet(x,y,vx,vy,hw,hh,sprite) - With custom sprite
--   chaos.box_x(), box_y(), box_w(), box_h()    - Battle box dims
--   chaos.frame()                               - Current frame
--   chaos.is_tough()                            - Tough mode?
--   chaos.sin(v), cos(v)                        - Trig
--   chaos.rand01()                              - Random 0..1
--   chaos.rand_int(lo, hi)                      - Random integer
--   chaos.register_attack(id, name, duration)   - Register attack
--   chaos.register_enemy(id, name, hp, atk, def, sprite)
--   chaos.register_gamemode(id, name, desc)     - Basic gamemode
-- ============================================================

chaos.log("Example Lua mod loading...")

-- Register a rain bullet attack
chaos.register_attack("rain", "Bullet Rain", 300)

-- Register a horror enemy
chaos.register_enemy("rawberry_clone", "Rawberry Clone", 80, 12, 4, "rawberry")

chaos.log("Example Lua mod loaded!")

-- ============================================================
-- Attack: rain
-- Drops bullets from the top of the box at random positions
-- ============================================================

function rain_start()
    chaos.log("Bullet Rain starting!")
end

-- Called every frame. Return true when the attack is done.
function rain_update()
    local f = chaos.frame()
    local bx = chaos.box_x()
    local by = chaos.box_y()
    local bw = chaos.box_w()

    -- Spawn rate depends on difficulty
    local interval = 6
    if chaos.is_tough() then
        interval = 3
    end

    -- Drop bullets from random X positions at the top
    if f % interval == 0 then
        local count = 1
        if chaos.is_tough() then count = 2 end

        for i = 1, count do
            local x = bx + chaos.rand_int(8, bw - 8)
            local y = by + 4
            local speed = 0.8 + chaos.rand01() * 0.8
            if chaos.is_tough() then
                speed = speed * 1.4
            end
            chaos.spawn_bullet(x, y, 0, speed, 4, 4)
        end
    end

    -- Occasionally spawn a sideways bullet for variety
    if f % 45 == 0 and f > 0 then
        local bh = chaos.box_h()
        local y = by + chaos.rand_int(10, bh - 10)
        local from_left = chaos.rand01() > 0.5
        local x = from_left and (bx + 2) or (bx + bw - 2)
        local vx = from_left and 1.5 or -1.5
        chaos.spawn_bullet(x, y, vx, 0, 4, 4)
    end

    return f >= 300
end

-- ============================================================
-- Enemy: rawberry_clone
-- ============================================================

function rawberry_clone_spawn()
    chaos.log("A Rawberry Clone appeared!")
end

function rawberry_clone_update()
    -- Custom AI goes here
end
