-- Issue twow-repo#348: quest completion summons that Turtle never published (owner decisions
-- F1/F2, 2026-09-26, relayed by OB-00 under Ä13, recorded in #348).
--
-- 1) New boss 65201 for 41936 "Twisting Rift Crystal" (Daio the Decrepit, Tainted Scar):
--    a voidwalker-like void lord. Owner spec: HP = 3x Lord Kazzak (3 x 486610), shadow AoE every
--    2 s for 200 damage, a shadow absorb bubble, Corruption at warlock max rank, a few Shadow Bolts,
--    no complicated mechanics. Built from Lord Kazzak's template (level 63, demon, rank 3, melee,
--    resistances), voidwalker model 1132, hostile faction 14, no script, no loot yet (loot is a
--    separate owner design, #348). The spells come from creature_spells 6520100 and run on the
--    default AI:
--      34769 Miasma            200 shadow damage to all enemies around the caster, every 2 s
--      22417 Shadow Shield     self absorb (the voidwalker's Sacrifice only targets its master)
--      25311 Corruption (r7)   137 shadow every 3 s for 18 s on a random enemy
--      25307 Shadow Bolt (r10) 482 shadow on a random enemy, now and then
-- 2) quest_end_scripts summon the boss 8 yd in front of the quest ender, facing him, for 30 min
--    or until it dies (TEMPSUMMON_TIMED_OR_DEAD_DESPAWN), at most one alive within 100 yd:
--      41936 -> 65201 (new void lord), 41935 -> 63107 Azuregos, 41966 -> 60686 Peroth'arn.
-- Replay-safe: INSERT IGNORE on keys, NOT EXISTS for quest_end_scripts (no primary key).

CREATE TEMPORARY TABLE tw_348_void_lord AS SELECT * FROM creature_template WHERE entry = 12397;
UPDATE tw_348_void_lord SET
    entry = 65201, display_id1 = 1132, display_id2 = 0, display_id3 = 0, display_id4 = 0,
    name = 'Voidlord of the Twisting Rift', subname = NULL,
    health_min = 1459830, health_max = 1459830, faction = 14, scale = 3,
    loot_id = 0, gold_min = 0, gold_max = 0, equipment_id = 0, movement_type = 0,
    script_name = '', ai_name = '', spell_list_id = 6520100;
INSERT IGNORE INTO creature_template SELECT * FROM tw_348_void_lord;
DROP TEMPORARY TABLE tw_348_void_lord;

INSERT IGNORE INTO creature_spells
    (entry, name,
     spellId_1, probability_1, castTarget_1, delayInitialMin_1, delayInitialMax_1, delayRepeatMin_1, delayRepeatMax_1,
     spellId_2, probability_2, castTarget_2, delayInitialMin_2, delayInitialMax_2, delayRepeatMin_2, delayRepeatMax_2,
     spellId_3, probability_3, castTarget_3, delayInitialMin_3, delayInitialMax_3, delayRepeatMin_3, delayRepeatMax_3,
     spellId_4, probability_4, castTarget_4, delayInitialMin_4, delayInitialMax_4, delayRepeatMin_4, delayRepeatMax_4)
VALUES
    (6520100, 'Twisting Rift - Voidlord of the Twisting Rift (#348)',
     34769, 100, 6, 2, 2, 2, 2,
     22417, 100, 6, 5, 8, 30, 30,
     25311, 100, 4, 3, 5, 8, 12,
     25307, 100, 4, 6, 9, 10, 15);

INSERT INTO quest_end_scripts
    (id, delay, priority, command, datalong, datalong2, datalong3, datalong4, target_param1, target_param2,
     target_type, data_flags, dataint, dataint2, dataint3, dataint4, x, y, z, o, condition_id, comments)
SELECT 41936, 0, 0, 10, 65201, 1800000, 1, 100, 0, 0, 0, 0, 8, 0, -1, 1, -11692.79, -2389.75, 0.30, 0.646, 0,
       'Twisting Rift Crystal - summon Voidlord of the Twisting Rift (#348)'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM quest_end_scripts WHERE id = 41936 AND command = 10);

INSERT INTO quest_end_scripts
    (id, delay, priority, command, datalong, datalong2, datalong3, datalong4, target_param1, target_param2,
     target_type, data_flags, dataint, dataint2, dataint3, dataint4, x, y, z, o, condition_id, comments)
SELECT 41935, 0, 0, 10, 63107, 1800000, 1, 100, 0, 0, 0, 0, 8, 0, -1, 1, 2523.16, -6257.83, 104.51, 6.073, 0,
       'Rite of Resurrection - summon Azuregos (#348)'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM quest_end_scripts WHERE id = 41935 AND command = 10);

INSERT INTO quest_end_scripts
    (id, delay, priority, command, datalong, datalong2, datalong3, datalong4, target_param1, target_param2,
     target_type, data_flags, dataint, dataint2, dataint3, dataint4, x, y, z, o, condition_id, comments)
SELECT 41966, 0, 0, 10, 60686, 1800000, 1, 100, 0, 0, 0, 0, 8, 0, -1, 1, 4462.08, -5040.95, 286.23, 4.527, 0,
       'Peroth''arn, Nightmare''s Herald - summon Peroth''arn (#348)'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM quest_end_scripts WHERE id = 41966 AND command = 10);
