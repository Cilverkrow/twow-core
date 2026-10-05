-- twow-repo#484 train 9, owner assignment 2026-10-03 (list point 9, "GM-Teleports: game_tele-Eintraege
-- fuer die 13 Maps ohne Client-Map.dbc ... sperren bzw. entfernen"; owner tests #484
-- issuecomment-5972156860): remove the GM .tele marks that lead to maps no Turtle client can load.
-- These maps have no Map.dbc entry and no WDT in the client (#408/#409), so a teleport there hangs
-- the client on the loading screen. game_tele has no disable flag and renaming a mark does not
-- stop `.tele`, so the marks are deleted; every row is kept in `game_tele_bak_484` first.
--
-- Scope: 41 marks on the 11 maps 13, 25, 29, 31, 37, 42, 49, 50, 150, 804, 809.
-- Not here: maps 44 (game_tele 621, 622, 809, 810, 827) and 806 (811) are handled by core#252
-- (20261001120000/20261001121000, redirected by UPDATE). They are excluded by the explicit
-- (id, map) list below, whether #252 runs before this file, after it, or not at all.
-- OB-00 2026-10-04 (core#295 comments 5978302467 / 5978303950): only the GM teleports are blocked,
-- all map data (map_template, spawns) stays as a template for own content (#412). Map 45 (Scarlet
-- Citadel) belongs to the same block: its marks 500/819 were already removed in train 8 (#408,
-- 20260927220000), so this file only asserts that map 45 has no mark left (end-state CHECK).
-- 806 "frostmane" is redirected to map 822 by core#252, not blocked here.
-- No areatrigger_teleport or spell_target_position leads to these maps, and no code or script
-- uses these names (LFT FindInstanceEntrance only loses non-matching candidates; read-only check
-- on cli484-db 2026-10-03). Spawns on these maps stay untouched. `.go xyz <map>` stays possible.
-- Coupling: client patch 8 (twow-repo, ops/clientpatch/consistency/server.toml) drops the now
-- stale map accepts for 29, 31, 49, 50, 804 (and 44/806 with #252); no client data changes here.
--
-- Replay-safe: CREATE TABLE IF NOT EXISTS + INSERT IGNORE for the backup; the DELETE only removes
-- (id, map) pairs that are in the backup; a second run deletes nothing.
-- Rollback (exact):
--   INSERT IGNORE INTO `game_tele` SELECT * FROM `game_tele_bak_484`;
--   -- optional after a confirmed rollback: DROP TABLE `game_tele_bak_484`;
--
-- Removed marks (id name / map):
--   13  Testing:              413 ScottTest, 502 prison1, 699 GhostTest
--   25  Scott Test:           548 snowbg, 550 kalidarbg, 557 arenakalidar, 566 lordaeronarena,
--                             567 ringofbones, 568 ogrearena, 586 alphakarazhan, 623 bearcave
--   29  CashTest:             503 prison2
--   31  PVPZone01OG:          706 originalalteracvalley
--   37  Azshara Crater:       415 AzsharaCrater, 431 CratereAzshara, 527 arenagw, 528 gw1, 529 arenahorde
--   42  Collin's Test:        504 prison3, 512 SunstriderIsle, 518 silvermooncity, 519 SaltherilsHaven,
--                             524 alphaironforge, 538 shalandisisle, 576 eversong, 577 ghostlands,
--                             583 zulaman, 822 torwatha
--   49  Quel'Thalas Cut Scene: 608 cutscenezulaman, 609 cutsceneshalandis
--   50  Silvermoon City Raid: 610 silvermoonraid
--   150 Tamamo Map:           560 realoutland, 564 watcherrise, 565 illidaripoint, 579 outlandgraveyard,
--                             581 Outlanddarkportal, 582 poolsofaggonar, 613 magharpost
--   804 Eldrethalas:          705 eldrethalas
--   809 Gnomeshrink:          820 gnshrink2, 821 gnshrink1

-- Backup: the 41 rows as they are (old-value guard: id and map must both match).
CREATE TABLE IF NOT EXISTS `game_tele_bak_484` LIKE `game_tele`;
INSERT IGNORE INTO `game_tele_bak_484`
  SELECT * FROM `game_tele` WHERE (`id`, `map`) IN (
    (413, 13), (502, 13), (699, 13),
    (548, 25), (550, 25), (557, 25), (566, 25), (567, 25), (568, 25), (586, 25), (623, 25),
    (503, 29),
    (706, 31),
    (415, 37), (431, 37), (527, 37), (528, 37), (529, 37),
    (504, 42), (512, 42), (518, 42), (519, 42), (524, 42), (538, 42), (576, 42), (577, 42), (583, 42), (822, 42),
    (608, 49), (609, 49),
    (610, 50),
    (560, 150), (564, 150), (565, 150), (579, 150), (581, 150), (582, 150), (613, 150),
    (705, 804),
    (820, 809), (821, 809));

-- Remove only what is in the backup (same id and map).
DELETE t FROM `game_tele` t
  JOIN `game_tele_bak_484` b ON b.`id` = t.`id` AND b.`map` = t.`map`;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
-- No mark is left on the 11 maps, all 41 are in the backup, and none of the core#252 marks
-- (621, 622, 809, 810, 827, 811) was backed up or removed here.
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_484_tele` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_484_tele` (`ok`)
SELECT (SELECT COUNT(*) FROM `game_tele` WHERE `map` IN (13, 25, 29, 31, 37, 42, 49, 50, 150, 804, 809)) = 0
   AND (SELECT COUNT(*) FROM `game_tele_bak_484`
         WHERE `map` IN (13, 25, 29, 31, 37, 42, 49, 50, 150, 804, 809)) = 41
   AND (SELECT COUNT(*) FROM `game_tele_bak_484` WHERE `id` IN (621, 622, 809, 810, 827, 811)) = 0
   AND (SELECT COUNT(*) FROM `game_tele` WHERE `map` = 45) = 0;
