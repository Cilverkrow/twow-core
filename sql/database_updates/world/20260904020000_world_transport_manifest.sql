-- Canonical 1.18.1 moving-transport manifest. Safe to replay.
-- The period column is descriptive; the core calculates the authoritative
-- period from TaxiPathNode.dbc and validates it while loading.
INSERT INTO `transports` (`guid`, `entry`, `name`, `period`) VALUES
(1,  20808,  'Ratchet and Booty Bay',                         363740),
(14, 20809,  'Spadowprey Village and Moonhoof Village',      355277),
(2,  176244, 'Teldrassil and Auberdine',                     316341),
(3,  176231, 'Menethil Harbor and Theramore Isle',           329159),
(9,  181646, 'Stormwind and Auberdine',                      234460),
(5,  177233, 'Forgotten Coast and Feathermoon Stronghold',   316916),
(6,  164871, 'Orgrimmar and Undercity',                      356175),
(7,  175080, 'Grom''Gol Base Camp and Orgrimmar',            303309),
(8,  176495, 'Grom''Gol Base Camp and Undercity',            332878),
(10, 190549, 'Orgrimmar and Thunder Bluff',                  566367),
(11, 190550, 'Sparkwater Port and Revantusk Village',        244960),
(12, 190552, 'Orgrimmar and Kargath',                        373728),
(13, 176250, 'Alah''Thalas and Auberdine',                   300917)
ON DUPLICATE KEY UPDATE
    `guid` = VALUES(`guid`),
    `name` = VALUES(`name`),
    `period` = VALUES(`period`);

-- Released 1.18.1 TaxiPath ids. These replace obsolete development ids that
-- do not exist in the shipped DBC files.
UPDATE `gameobject_template` SET `data0` = 72  WHERE `entry` = 20808;
UPDATE `gameobject_template` SET `data0` = 348 WHERE `entry` = 20809;
UPDATE `gameobject_template` SET `data0` = 121 WHERE `entry` = 164871;
UPDATE `gameobject_template` SET `data0` = 110 WHERE `entry` = 175080;
UPDATE `gameobject_template` SET `data0` = 116 WHERE `entry` = 176231;
UPDATE `gameobject_template` SET `data0` = 117 WHERE `entry` = 176244;
UPDATE `gameobject_template` SET `data0` = 323 WHERE `entry` = 176250;
UPDATE `gameobject_template` SET `data0` = 120 WHERE `entry` = 176495;
UPDATE `gameobject_template` SET `data0` = 122 WHERE `entry` = 177233;
UPDATE `gameobject_template` SET `data0` = 294 WHERE `entry` = 181646;
UPDATE `gameobject_template` SET `data0` = 295 WHERE `entry` = 190549;
UPDATE `gameobject_template` SET `data0` = 296 WHERE `entry` = 190550;
UPDATE `gameobject_template` SET `data0` = 297 WHERE `entry` = 190552;
