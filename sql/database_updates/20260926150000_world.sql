-- Issue twow-repo#322 force-spawn list A1 (owner decision F5, 2026-09-26, recorded in #322).
-- Nine open-world rares whose spawns were dropped when the Grim Reaches (3590000-3591718) and the
-- Kalimdor 2700000-2702528 ranges were re-imported ("a_big_nuke" migrations 20260614120836 and
-- 20260618114919). Each row restores the original values from 20260513233443_world.sql /
-- 20260530203924_world.sql (position, orientation, respawn, flags) under a new guid in the unused
-- range 2990001-2990009. The rares also join creature_rare_respawn_registry (#298) so the
-- funserver respawn applies. INSERT IGNORE keeps both inserts replay-safe; nothing else changes.
INSERT IGNORE INTO `creature`
    (`guid`, `id`, `id2`, `id3`, `id4`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
     `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `health_percent`, `mana_percent`,
     `movement_type`, `spawn_flags`, `visibility_mod`)
VALUES
    (2990001, 62639, 0, 0, 0, 0, -5172.672000, -5277.265000, 176.096782, 0.588432, 50400, 50400, 0.0, 100.0, 0.0, 0, 0, 0.0),
    (2990002, 62640, 0, 0, 0, 0, -6086.592000, -4555.407000, 230.627944, 4.423658, 50400, 50400, 0.0, 100.0, 0.0, 0, 0, 0.0),
    (2990003, 62641, 0, 0, 0, 0, -5688.768000, -5131.816000, 228.019775, 5.689327, 172800, 172800, 0.0, 100.0, 100.0, 0, 0, 0.0),
    (2990004, 62642, 0, 0, 0, 0, -5312.448000, -4717.017000, 227.346998, 5.596633, 50400, 50400, 0.0, 100.0, 0.0, 0, 0, 0.0),
    (2990005, 62643, 0, 0, 0, 0, -3584.960000, -4253.735000, 248.790374, 3.660245, 50400, 50400, 0.0, 100.0, 0.0, 0, 0, 0.0),
    (2990006, 62644, 0, 0, 0, 0, -4506.048000, -5368.844000, 168.661175, 1.529100, 50400, 50400, 0.0, 100.0, 0.0, 0, 0, 0.0),
    (2990007, 62753, 0, 0, 0, 0, -6126.016000, -2222.836000, 451.958831, 3.031962, 85000, 85000, 0.0, 100.0, 0.0, 0, 0, 0.0),
    (2990008, 63028, 0, 0, 0, 1, 7545.2407, -5703.8672, 168.971865, 5.565151, 300, 300, 0.0, 100.0, 100.0, 0, 0, 0.0),
    (2990009, 63032, 0, 0, 0, 1, 9376.9702, -4922.9808, 5.45462, 4.285642, 300, 300, 0.0, 100.0, 100.0, 0, 0, 0.0);

INSERT IGNORE INTO creature_rare_respawn_registry (guid, creature_entry, map_id, classification, note) VALUES
    (2990001,62639,0,'static','#322 A1 restored: Bagalosh (old guid 2591614)'),
    (2990002,62640,0,'static','#322 A1 restored: Arashna (old guid 2591615)'),
    (2990003,62641,0,'static','#322 A1 restored: Emastrasz (old guid 2591616)'),
    (2990004,62642,0,'static','#322 A1 restored: Razorscale (old guid 2591617)'),
    (2990005,62643,0,'static','#322 A1 restored: Dorosh Headsplitter (old guid 2591618)'),
    (2990006,62644,0,'static','#322 A1 restored: Bruhm Cinderfist (old guid 2591619)'),
    (2990007,62753,0,'static','#322 A1 restored: Dark Iron Surveillance Bot (old guid 2591717)'),
    (2990008,63028,1,'static','#322 A1 restored: Larexxa Foulheart (old guid 2701616)'),
    (2990009,63032,1,'static','#322 A1 restored: Glurgill (old guid 2701617)');
