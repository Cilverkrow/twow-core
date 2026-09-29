-- twow-repo#408 (train 8), owner decision 2026-09-29 (twow-repo#408, relayed by OB-00):
-- 1. Shade of Medivh (59994) no longer drops boss epics. The Echo of Medivh script
--    (boss_echo_of_medivh.cpp) summons the Shade; the Echo (61958, loot 2000213) already
--    carries the same eight epics (55094/55107/55111/55112 and 55108/55109/55110/55276 in two
--    groups), so the Shade doubled them. The Shade keeps its template, only its loot id goes.
-- 2. Echo of Sargeras (60063) and Corrupted Ashbringer (60064) are kill-credit stand-ins of
--    quest 41637 (never spawned) and pointed at the Echo's full epic table 2000213.
-- Loot rows stay untouched (no deletes); only creature_template.loot_id changes.
-- Replay-safe: each UPDATE only matches the old value.
-- Rollback:
--   UPDATE `creature_template` SET `loot_id` = 59994   WHERE `entry` = 59994 AND `loot_id` = 0;
--   UPDATE `creature_template` SET `loot_id` = 2000213 WHERE `entry` IN (60063, 60064) AND `loot_id` = 0;

UPDATE `creature_template` SET `loot_id` = 0 WHERE `entry` = 59994 AND `loot_id` = 59994;
UPDATE `creature_template` SET `loot_id` = 0 WHERE `entry` IN (60063, 60064) AND `loot_id` = 2000213;
