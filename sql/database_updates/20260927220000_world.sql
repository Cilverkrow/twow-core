-- Issue twow-repo#408: secure the Scarlet Citadel (map 45). Owner decision 2026-09-27
-- ("Ruhen lassen + absichern", #408 issuecomment-5859592538): map 45 exists in no Turtle
-- client (no Map.dbc entry, no WDT/ADT), so a teleport there hangs the client on the loading
-- screen. The content (23 creatures, 12 gameobjects, bosses, loot) stays in the DB for later.
-- Read-only check 2026-09-27: no areatrigger_teleport, spell_target_position or script
-- teleport (command 6) leads to map 45; the only ways in are the portal and two .tele entries.
--
-- Backup of the changed rows (also in evidence\ws-30\408-scarlet-citadel\backup-rows-map45.sql):
--   INSERT INTO `game_tele` VALUES (500,32.5495,13.2999,16.869,6.28138,45,'ScarletCitadel');
--   INSERT INTO `game_tele` VALUES (819,83.6082,-2.90589,16.8695,1.73362,45,'sizetest');
--   gameobject_template 112920 "Scarlet Citadel (Entrance)": flags 0 (restore: flags = 0).
-- Replay-safe: fixed-value UPDATE and DELETE by primary key.

-- The entrance portal (spawn guid 4014219 in Tirisfal Glades) stays visible but cannot be
-- used, even once custom_dungeon_portal is implemented for the other Turtle portals.
UPDATE `gameobject_template` SET `flags` = `flags` | 16 WHERE `entry` = 112920;

-- GM teleports into the map that no client can load.
DELETE FROM `game_tele` WHERE `id` IN (500, 819) AND `map` = 45;
