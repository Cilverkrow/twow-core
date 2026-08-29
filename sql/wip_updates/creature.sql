-- Fix: Missing spawn point for custom Turtle WoW NPC Ralthas (Entry: 62635) on Heroes' Vigil
INSERT INTO `creature` (
    `guid`, `id`, `id2`, `id3`, `id4`, 
    `map`, `position_x`, `position_y`, `position_z`, `orientation`, 
    `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, 
    `health_percent`, `mana_percent`, `movement_type`, `spawn_flags`, `visibility_mod`
) VALUES (
    NULL, 62635, 0, 0, 0, 
    0, -9081.53, -1029.03, 72.313, 4.39111, 
    120, 120, 0, 
    100, 100, 0, 0, 0
);
