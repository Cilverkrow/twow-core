-- Cutpurse Warren: make the choice one-time for the player.
-- Reuse the existing condition system: quest 41636 is active/incomplete
-- and the donated-books crate is not already in the player's inventory.

INSERT INTO `conditions`
(`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 2, 41606, 1, 0, 0, 1
FROM DUAL
WHERE NOT EXISTS (
    SELECT 1
    FROM `conditions`
    WHERE `type` = 2
      AND `value1` = 41606
      AND `value2` = 1
      AND `value3` = 0
      AND `value4` = 0
      AND `flags` = 1
);

INSERT INTO `conditions`
(`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT -1,
       (SELECT `condition_entry`
        FROM `conditions`
        WHERE `type` = 9 AND `value1` = 41636 AND `value2` = 1
        ORDER BY `condition_entry` DESC LIMIT 1),
       (SELECT `condition_entry`
        FROM `conditions`
        WHERE `type` = 2 AND `value1` = 41606 AND `value2` = 1 AND `flags` = 1
        ORDER BY `condition_entry` DESC LIMIT 1),
       0, 0, 0
FROM DUAL
WHERE NOT EXISTS (
    SELECT 1
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
);

-- Hide both initial choices after the crate has been obtained.
UPDATE `gossip_menu_option`
SET `condition_id` = (
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
WHERE `menu_id` = 62298 AND `id` IN (0, 1);

-- Also prevent a stale second-page click from charging or creating another crate.
UPDATE `gossip_menu_option`
SET `condition_id` = (
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
WHERE `menu_id` = 30348 AND `id` = 0;

UPDATE `gossip_scripts`
SET `condition_id` = (
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
WHERE `id` = 6229806 AND `command` = 17;
