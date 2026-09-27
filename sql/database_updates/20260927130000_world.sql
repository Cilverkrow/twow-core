-- Issue twow-repo#348, owner design round 2 (2026-09-27, after the video test):
-- the Voidlord of the Twisting Rift (65201) becomes 50 % larger (scale 3 -> 4.5) and is driven by
-- the C++ script boss_twisting_rift_voidlord (src/scripts/world/blasted_lands.cpp): Shadow Nova
-- instead of the invisible Miasma, and one more body per 20 % of the shared health pool lost.
-- The split bodies use the new template 65202 (copy of 65201, script npc_twisting_rift_voidsplit,
-- no loot: the encounter drops loot once, from the lord). The creature_spells list 6520100 is no
-- longer referenced; the script casts the abilities itself.
-- Replay-safe: the UPDATE sets fixed values, INSERT IGNORE keeps an existing 65202.

UPDATE creature_template
   SET scale = 4.5, script_name = 'boss_twisting_rift_voidlord', ai_name = '', spell_list_id = 0
 WHERE entry = 65201;

CREATE TEMPORARY TABLE tw_348_void_split AS SELECT * FROM creature_template WHERE entry = 65201;
UPDATE tw_348_void_split SET
    entry = 65202, script_name = 'npc_twisting_rift_voidsplit', loot_id = 0, gold_min = 0, gold_max = 0;
INSERT IGNORE INTO creature_template SELECT * FROM tw_348_void_split;
DROP TEMPORARY TABLE tw_348_void_split;
