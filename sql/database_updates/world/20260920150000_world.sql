-- ==============================================
-- FILE: tower_of_karazhan_bidirectional_portal.sql
-- GENERATED: 20260920150000
-- ==============================================

-- AreaTrigger 5340 is the exterior Tower of Karazhan entrance. The DBC
-- trigger already exists; this condition supplies the inventory key gate.
-- The unique condition payload is the authority, not a hard-coded condition ID.
INSERT INTO `conditions` (
    `type`,
    `value1`,
    `value2`,
    `value3`,
    `value4`,
    `flags`
) VALUES (
    2,
    61234,
    1,
    0,
    0,
    0
) ON DUPLICATE KEY UPDATE `condition_entry` = `condition_entry`;

-- DBC AreaTrigger 5340 and 5341 already define the exterior and interior
-- trigger volumes. Resolve the unique condition payload at application time.
INSERT INTO `areatrigger_teleport` (
    `id`,
    `name`,
    `message`,
    `required_level`,
    `required_condition`,
    `required_phase`,
    `target_map`,
    `target_position_x`,
    `target_position_y`,
    `target_position_z`,
    `target_orientation`
)
SELECT
    5340,
    'Tower of Karazhan - Entrance',
    'You must be level 60 and possess the Upper Karazhan Tower Key to enter.',
    60,
    `condition_entry`,
    0,
    814,
    -11044.265625,
    -1991.812134,
    98.740768,
    2.164079
FROM `conditions`
WHERE `type` = 2
  AND `value1` = 61234
  AND `value2` = 1
  AND `value3` = 0
  AND `value4` = 0
  AND `flags` = 0
ON DUPLICATE KEY UPDATE
    `name` = VALUES(`name`),
    `message` = VALUES(`message`),
    `required_level` = VALUES(`required_level`),
    `required_condition` = VALUES(`required_condition`),
    `required_phase` = VALUES(`required_phase`),
    `target_map` = VALUES(`target_map`),
    `target_position_x` = VALUES(`target_position_x`),
    `target_position_y` = VALUES(`target_position_y`),
    `target_position_z` = VALUES(`target_position_z`),
    `target_orientation` = VALUES(`target_orientation`);

INSERT INTO `areatrigger_teleport` (
    `id`,
    `name`,
    `message`,
    `required_level`,
    `required_condition`,
    `required_phase`,
    `target_map`,
    `target_position_x`,
    `target_position_y`,
    `target_position_z`,
    `target_orientation`
) VALUES (
    5341,
    'Tower of Karazhan - Exit',
    '',
    0,
    0,
    0,
    0,
    -10847.659180,
    -1866.291504,
    117.173531,
    1.722723
)
ON DUPLICATE KEY UPDATE
    `name` = VALUES(`name`),
    `message` = VALUES(`message`),
    `required_level` = VALUES(`required_level`),
    `required_condition` = VALUES(`required_condition`),
    `required_phase` = VALUES(`required_phase`),
    `target_map` = VALUES(`target_map`),
    `target_position_x` = VALUES(`target_position_x`),
    `target_position_y` = VALUES(`target_position_y`),
    `target_position_z` = VALUES(`target_position_z`),
    `target_orientation` = VALUES(`target_orientation`);
