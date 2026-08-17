-- Northwind: make the Cutpurse Warren deal a paid gossip action.
-- SCRIPT_COMMAND_CREATE_ITEM datalong3 is the optional copper cost.

-- Remove the temporary vendor-backed workaround, if it was applied locally.
DELETE FROM `gossip_menu_option`
WHERE `menu_id` = 30348 AND `id` = 0;

DELETE FROM `npc_vendor`
WHERE `entry` = 62298 AND `item` = 41606;

UPDATE `creature_template`
SET `npc_flags` = `npc_flags` & ~128
WHERE `entry` = 62298;

UPDATE `item_template`
SET `buy_price` = 0
WHERE `entry` = 41606 AND `buy_price` = 2000;

-- The deal is available only while the prerequisite quest is active.
INSERT INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 9, 41636, 1, 0, 0, 0 FROM DUAL
WHERE NOT EXISTS (
    SELECT 1 FROM `conditions`
    WHERE `type` = 9 AND `value1` = 41636 AND `value2` = 1
);

-- Threat branch: close gossip, become temporarily hostile, then attack.
INSERT INTO `gossip_scripts`
    (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`)
SELECT 6229805, 0, 0, 22, 14, 3,
       'Cutpurse Warren threat: become hostile and attack'
FROM DUAL
WHERE NOT EXISTS (
    SELECT 1 FROM `gossip_scripts`
    WHERE `id` = 6229805 AND `command` = 22
);

INSERT INTO `gossip_scripts`
    (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`)
SELECT 6229805, 0, 1, 26, 0, 0,
       'Cutpurse Warren threat: attack the player'
FROM DUAL
WHERE NOT EXISTS (
    SELECT 1 FROM `gossip_scripts`
    WHERE `id` = 6229805 AND `command` = 26
);

-- Paid branch: create the quest item only after validating inventory and 20 silver.
INSERT INTO `gossip_scripts`
    (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `datalong3`, `data_flags`, `comments`)
SELECT 6229806, 0, 0, 17, 41606, 1, 2000, 8,
       'Cutpurse Warren deal: charge 20 silver and create the donated-books crate'
FROM DUAL
WHERE NOT EXISTS (
    SELECT 1 FROM `gossip_scripts`
    WHERE `id` = 6229806 AND `command` = 17
);

UPDATE `gossip_menu_option`
SET `action_menu_id` = -1,
    `action_script_id` = 6229805
WHERE `menu_id` = 62298 AND `id` = 1;

INSERT INTO `gossip_menu_option`
    (`menu_id`, `id`, `option_icon`, `option_text`, `option_broadcast_text`,
     `option_id`, `npc_option_npcflag`, `action_menu_id`, `action_poi_id`,
     `action_script_id`, `box_coded`, `box_money`, `box_text`,
     `box_broadcast_text`, `condition_id`)
SELECT 30348, 0, 6, 'Take it and get out of Northwind <Pay 20 silver>', 0,
       1, 1, -1, 0,
       6229806, 0, 0, '',
       0,
       (SELECT `condition_entry` FROM `conditions`
        WHERE `type` = 9 AND `value1` = 41636 AND `value2` = 1
        ORDER BY `condition_entry` DESC LIMIT 1)
FROM DUAL;
