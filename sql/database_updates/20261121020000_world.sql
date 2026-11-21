-- twow-repo#579 (DS4 quest NPCs without a spawn; owner 21.11.2026 via OB-00: "ja", main train, Ae15): two world-data fixes.
-- 1. The Wetlands line 40679 "Reunite Tribe-Tribe!" -> 40680 "The One Alpha" -> 40681 "Cursed Sagepaw!" (level 23-27)
--    had only Gowlfang 91243 as giver and ender. Turtle removed his spawn on purpose (upstream 20260829172251_world.sql,
--    gowlfang_removal.sql) and put Grimbite 62636 (questgiver, npc_flags 3) at the same camp; Gowlfang is now the dungeon
--    boss 62057. Grimbite becomes giver and ender of the three quests. All their targets exist (Mosshide gnolls 178
--    spawns incl. 5 Mosshide Alphas 1014, Sagepaw 91291, armband 81335 drops). Gowlfang's own relation rows stay (no
--    spawn, harmless); 55035 "Hidden-Hidden Reward" (its chest was not found) is left out.
-- 2. 80105 "Ventured Too Far" (starts from item 80114, ends at Nert Blastentom 80100) and 80208 "Sunblade Reunion" (given
--    by Vyrin Swiftwind 1156, ends at Kathy Wake 80202) end at NPCs without a spawn (legacy goblin / high-elf starts), so
--    a player can take them but never turn them in. Both are disabled: Method |= 1 (QUEST_METHOD_DISABLED, QuestDef.cpp
--    sets m_isActive = false). Method 2 -> 3, which the core normalizes to 1.
-- Replay-safe (INSERT IGNORE, guarded UPDATE). End-state CHECK. Evidence: evidence/ws-30/tiefe4-blocker/
-- npc-info-owner-20261121.md (52eb9d4b), ursachen-ob20-20261120.md (7b3ade07).
-- Rollback (exact):
--   DELETE FROM `creature_questrelation` WHERE `id` = 62636 AND `quest` IN (40679, 40680, 40681);
--   DELETE FROM `creature_involvedrelation` WHERE `id` = 62636 AND `quest` IN (40679, 40680, 40681);
--   UPDATE `quest_template` SET `Method` = 2 WHERE `entry` IN (80105, 80208) AND `Method` = 3;

INSERT IGNORE INTO `creature_questrelation` (`id`, `quest`) VALUES (62636, 40679), (62636, 40680), (62636, 40681);
INSERT IGNORE INTO `creature_involvedrelation` (`id`, `quest`) VALUES (62636, 40679), (62636, 40680), (62636, 40681);

UPDATE `quest_template` SET `Method` = `Method` | 1 WHERE `entry` IN (80105, 80208) AND `Method` = 2;

DROP TEMPORARY TABLE IF EXISTS `_q579_check`;
CREATE TEMPORARY TABLE `_q579_check` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `_q579_check` (`ok`) SELECT
  (SELECT COUNT(*) FROM `creature_questrelation` WHERE `id` = 62636 AND `quest` IN (40679, 40680, 40681)) = 3
  AND (SELECT COUNT(*) FROM `creature_involvedrelation` WHERE `id` = 62636 AND `quest` IN (40679, 40680, 40681)) = 3
  AND (SELECT COUNT(*) FROM `quest_template` WHERE `entry` IN (80105, 80208) AND (`Method` & 1) = 1) = 2;
DROP TEMPORARY TABLE IF EXISTS `_q579_check`;
