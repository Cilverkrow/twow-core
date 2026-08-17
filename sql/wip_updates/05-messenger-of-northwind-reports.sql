-- Northwind: The Messenger of Northwind (41768) report handover.
-- Sir Amberwood (62164) and Bailiff Lancaster (62153) both display their
-- report dialogue but never create the required quest items. Wire the two
-- gossip options to SCRIPT_COMMAND_CREATE_ITEM scripts (script id =
-- NPC entry * 100 + option id, matching the existing Bolvar/Magni report
-- pattern), and close Lancaster's gossip window after the handover
-- (action_menu_id = -1). Lord Amberwood's Report (41864) is obtained by
-- opening the Amberwood Chest given on quest accept (SrcItemId 36669) and
-- needs no change.

DELETE FROM `gossip_scripts` WHERE `id` IN (6216401, 6215301);

INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`) VALUES
(6216401, 0, 0, 17, 41865, 1, 'Sir Amberwood - Quest 41768: give Sir Amberwood''s Report'),
(6215301, 0, 0, 17, 41866, 1, 'Bailiff Lancaster - Quest 41768: give Bailiff Lancaster''s Report');

UPDATE `gossip_menu_option` SET `action_script_id` = 6216401 WHERE `menu_id` = 62164 AND `id` = 0;
UPDATE `gossip_menu_option` SET `action_script_id` = 6215301, `action_menu_id` = -1 WHERE `menu_id` = 62153 AND `id` = 1;
