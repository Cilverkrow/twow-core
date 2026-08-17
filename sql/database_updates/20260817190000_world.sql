-- Cutpurse Warren: show the payment/threat choices only after the
-- player selects the initial crate handover option.

-- Remove the threat choice from the first gossip menu.
DELETE FROM `gossip_menu_option`
WHERE `menu_id` = 62298 AND `id` = 1;

-- Add the threat choice beside the payment choice in the follow-up menu.
UPDATE `gossip_menu_option`
SET `option_icon` = 7,
    `option_text` = 'As if! Today you''ll draw your last breath!',
    `option_broadcast_text` = 0,
    `option_id` = 1,
    `npc_option_npcflag` = 1,
    `action_menu_id` = -1,
    `action_poi_id` = 0,
    `action_script_id` = 6229805,
    `box_coded` = 0,
    `box_money` = 0,
    `box_text` = '',
    `box_broadcast_text` = 0,
    `condition_id` = (
        SELECT `condition_entry`
        FROM `conditions`
        WHERE `type` = -1
          AND `value1` = (SELECT `condition_entry`
                          FROM `conditions`
                          WHERE `type` = 9 AND `value1` = 41636 AND `value2` = 1
                          ORDER BY `condition_entry` DESC LIMIT 1)
          AND `value2` = (SELECT `condition_entry`
                          FROM `conditions`
                          WHERE `type` = 2 AND `value1` = 41606 AND `value2` = 1 AND `flags` = 1
                          ORDER BY `condition_entry` DESC LIMIT 1)
          AND `value3` = 0
          AND `value4` = 0
          AND `flags` = 0
        ORDER BY `condition_entry` DESC LIMIT 1
    )
WHERE `menu_id` = 30348 AND `id` = 1;

INSERT INTO `gossip_menu_option`
    (`menu_id`, `id`, `option_icon`, `option_text`, `option_broadcast_text`,
     `option_id`, `npc_option_npcflag`, `action_menu_id`, `action_poi_id`,
     `action_script_id`, `box_coded`, `box_money`, `box_text`,
     `box_broadcast_text`, `condition_id`)
SELECT 30348, 1, 7, 'As if! Today you''ll draw your last breath!', 0,
       1, 1, -1, 0,
       6229805, 0, 0, '', 0,
       (
           SELECT `condition_entry`
           FROM `conditions`
           WHERE `type` = -1
             AND `value1` = (SELECT `condition_entry`
                             FROM `conditions`
                             WHERE `type` = 9 AND `value1` = 41636 AND `value2` = 1
                             ORDER BY `condition_entry` DESC LIMIT 1)
             AND `value2` = (SELECT `condition_entry`
                             FROM `conditions`
                             WHERE `type` = 2 AND `value1` = 41606 AND `value2` = 1 AND `flags` = 1
                             ORDER BY `condition_entry` DESC LIMIT 1)
             AND `value3` = 0
             AND `value4` = 0
             AND `flags` = 0
           ORDER BY `condition_entry` DESC LIMIT 1
       )
FROM DUAL
WHERE NOT EXISTS (
    SELECT 1
    FROM `gossip_menu_option`
    WHERE `menu_id` = 30348 AND `id` = 1
);
