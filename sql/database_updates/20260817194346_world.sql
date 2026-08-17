-- Northwind: School Assistance (41637) quiz credit.
-- Lloyd, Ellie, Randolph and Tio display their quiz dialogue and
-- validation replies but never award the four objectives. Wire each
-- correct-answer gossip option to a SCRIPT_COMMAND_KILL_CREDIT script
-- and gate the options behind the active-quest condition, matching the
-- Empty Houses (41643) pattern.
-- Correct answers: Lloyd -> Daria Balor, Ellie -> Gold veins,
-- Randolph -> Barathen Wrynn, Tio -> Prestor family.

DELETE FROM `gossip_scripts` WHERE `id` IN (62300, 62301, 62302, 62303);

INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`) VALUES
(62300, 0, 0, 8, 60078, 0, 'Lloyd - Quest 41637 objective credit (Daria Balor)'),
(62301, 0, 0, 8, 60063, 0, 'Ellie - Quest 41637 objective credit (Gold veins)'),
(62302, 0, 0, 8, 60064, 0, 'Randolph - Quest 41637 objective credit (Barathen Wrynn)'),
(62303, 0, 0, 8, 60065, 0, 'Tio - Quest 41637 objective credit (Prestor family)');

INSERT INTO `conditions` (`condition_entry`, `type`, `value1`, `value2`, `value3`, `value4`, `flags`) VALUES
(41637, 9, 41637, 1, 0, 0, 0)
ON DUPLICATE KEY UPDATE `type`=VALUES(`type`), `value1`=VALUES(`value1`), `value2`=VALUES(`value2`), `flags`=VALUES(`flags`);

UPDATE `gossip_menu_option` SET `action_script_id` = 62300, `condition_id` = 41637 WHERE `menu_id` = 62300 AND `id` = 2;
UPDATE `gossip_menu_option` SET `action_script_id` = 62301, `condition_id` = 41637 WHERE `menu_id` = 62301 AND `id` = 0;
UPDATE `gossip_menu_option` SET `action_script_id` = 62302, `condition_id` = 41637 WHERE `menu_id` = 62302 AND `id` = 1;
UPDATE `gossip_menu_option` SET `action_script_id` = 62303, `condition_id` = 41637 WHERE `menu_id` = 62303 AND `id` = 2;
