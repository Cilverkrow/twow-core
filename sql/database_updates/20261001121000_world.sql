-- twow-repo#459 Frostmane Hollow (map 822), train 9. Owner decisions 2026-10-01 relayed by OB-00
-- (Ä13; Oboka: #459 issuecomment-5928240998 "Obokas axe bitte bei beiden integrieren ich
-- entscheide später bei wem sie aktiv bleibt").
--   1. GM mark game_tele 811 "frostmane" pointed to the empty map 806; it now uses the target of
--      the instance entrance, areatrigger 5662 (#455 A5).
--   2. Oboka's Axe (item 184) also drops from Handler Oboka (63132, had no loot at all); it stays
--      in Tan'sha the Sleek's (63133) table unchanged. Oboka: 33.33 %, the same per-kill chance
--      the axe has at Tan'sha.
--   3. One special ability each for the pure melee trash (owner rule: at most one per trash
--      type), plus Battlemaster Ubukaz, whose only ability was an enrage at 25 %:
--        108   Frostmane Warrior (elite, 10 spawns)    Sunder Armor (11971) on the victim
--        19    Undermarket Mercenary (elite, 5)        Shield Slam (8242), the template's own spell_id1
--        96    Frostmane Ritualist (normal, 8)         Curse of Weakness R2 (1108), dispellable
--        63131 Battlemaster Ubukaz (boss)              Cleave (15496)
--      All four only spawn on map 822. EventAI runs creature_spells lists in combat
--      (CreatureEventAI::UpdateAI -> UpdateSpellsList); creatures skip weapon/shield and, without
--      mana, mana-cost checks (Spell::CheckItems, Spell::CheckPower). List ids = creature entry,
--      as for the other Frostmane lists; 19, 96, 108 and 63131 were unused.
--      Frostmane Slave (51) stays without an ability (owner audit #459).
-- Replay-safe: backup tables first, INSERT IGNORE, guarded UPDATEs.
-- Rollback:
--   UPDATE `game_tele` t JOIN `game_tele_bak_459` b ON b.`id` = t.`id`
--   SET t.`position_x` = b.`position_x`, t.`position_y` = b.`position_y`, t.`position_z` = b.`position_z`,
--       t.`orientation` = b.`orientation`, t.`map` = b.`map`;
--   UPDATE `creature_template` t JOIN `creature_template_bak_459` b ON b.`entry` = t.`entry`
--   SET t.`loot_id` = b.`loot_id`, t.`spell_list_id` = b.`spell_list_id`;
--   DELETE FROM `creature_loot_template` WHERE `entry` = 63132 AND `item` = 184;
--   DELETE FROM `creature_spells` WHERE `entry` IN (19, 96, 108, 63131);

CREATE TABLE IF NOT EXISTS `game_tele_bak_459` LIKE `game_tele`;
INSERT IGNORE INTO `game_tele_bak_459` SELECT * FROM `game_tele` WHERE `id` = 811 AND `map` = 806;

CREATE TABLE IF NOT EXISTS `creature_template_bak_459` LIKE `creature_template`;
INSERT IGNORE INTO `creature_template_bak_459`
  SELECT * FROM `creature_template` WHERE `entry` IN (19, 96, 108, 63131, 63132);

-- 1. game_tele 811 -> Frostmane Hollow entrance (areatrigger 5662 target).
UPDATE `game_tele`
SET `map` = 822, `position_x` = -7522.73, `position_y` = -3588.76, `position_z` = 199.981, `orientation` = 2.2022
WHERE `id` = 811 AND `map` = 806;

-- 2. Handler Oboka gets his own loot table with Oboka's Axe.
INSERT IGNORE INTO `creature_loot_template`
  (`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`, `condition_id`)
VALUES (63132, 184, 33.33, 0, 1, 1, 0);
UPDATE `creature_template` SET `loot_id` = 63132 WHERE `entry` = 63132 AND `loot_id` = 0;

-- 3. One ability each (castTarget 1 = current victim).
INSERT IGNORE INTO `creature_spells`
  (`entry`, `name`, `spellId_1`, `probability_1`, `castTarget_1`,
   `delayInitialMin_1`, `delayInitialMax_1`, `delayRepeatMin_1`, `delayRepeatMax_1`)
VALUES
  (108,   'Frostmane Hollow - Frostmane Warrior',     11971, 100, 1, 3, 6,  8, 12),
  (19,    'Frostmane Hollow - Undermarket Mercenary', 8242,  100, 1, 5, 8, 14, 18),
  (96,    'Frostmane Hollow - Frostmane Ritualist',   1108,  100, 1, 1, 3, 25, 30),
  (63131, 'Frostmane Hollow - Battlemaster Ubukaz',   15496, 100, 1, 6, 9,  9, 12);
UPDATE `creature_template` SET `spell_list_id` = `entry`
WHERE `entry` IN (19, 96, 108, 63131) AND `spell_list_id` = 0;
