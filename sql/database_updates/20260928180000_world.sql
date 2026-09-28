-- Issue twow-repo#348 round 3 (owner, 2026-09-28, video evidence ws-30/348-voidboss-video2):
-- the Voidlord's pulse (Shadow Nova 45559) shows the golden Holy Nova look in the client
-- (its client visual 3643 uses holynova_impact_base). The owner prefers the warlock's Hellfire.
-- Spell visuals come from the client Spell.dbc by spell ID, so the boss now casts 2951
-- "Hellfire III" (an old NPC spell with the Hellfire visual 781; no trainer, creature_spells,
-- EventAI or C++ uses it) and 2951 gets the nova's values server-side. A purple colour would
-- need a client patch (#409) and is not part of this change. The boss is also 20 % larger.
-- Owner 2026-09-28 (#348 issuecomment-5875613225, "b es darf auch herausfordernd sein"): the
-- pulse reaches 10 yd (radius index 13) at the larger size, and the boss yells about the
-- Twisting Nether (own English lines; broadcast_text 6520101-6520111, yell, no sound).
--
-- Rollback (values before this migration):
--   UPDATE spell_template SET school = 2, castingTimeIndex = 14, rangeIndex = 4, manaCost = 32,
--     effectImplicitTargetA1 = 16, effectImplicitTargetB1 = 0, effectRadiusIndex1 = 13,
--     effectBasePoints1 = 87, effectDieSides1 = 15, effectBonusCoefficient1 = 1 WHERE entry = 2951;
--   UPDATE creature_template SET scale = 4.5 WHERE entry IN (65201, 65202);
-- Replay-safe: fixed values.

UPDATE `spell_template`
   SET `school` = 5, `castingTimeIndex` = 1, `rangeIndex` = 1, `manaCost` = 0,
       `effectImplicitTargetA1` = 22, `effectImplicitTargetB1` = 15, `effectRadiusIndex1` = 13,
       `effectBasePoints1` = 235, `effectDieSides1` = 42, `effectBonusCoefficient1` = -1
 WHERE `entry` = 2951;

UPDATE `creature_template` SET `scale` = 5.4 WHERE `entry` IN (65201, 65202);

-- Twisting Nether yells for the Voidlord (65201): aggro, taunts every 30-45 s, split,
-- the last split at 20 %, player killed, death. chat_type 1 = yell.
INSERT IGNORE INTO `broadcast_text`
  (`entry`, `male_text`, `female_text`, `chat_type`, `sound_id`, `language_id`,
   `emote_id1`, `emote_id2`, `emote_id3`, `emote_delay1`, `emote_delay2`, `emote_delay3`)
VALUES
(6520101, 'The Rift yawns open. You will fall into the Nether with me!', 'The Rift yawns open. You will fall into the Nether with me!', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520102, 'Can you hear it? The Nether sings beneath your feet.', 'Can you hear it? The Nether sings beneath your feet.', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520103, 'Reality frays at the edges. Soon it tears.', 'Reality frays at the edges. Soon it tears.', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520104, 'Every breath you take feeds the Void.', 'Every breath you take feeds the Void.', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520105, 'The storm between worlds never ends. It only waits.', 'The storm between worlds never ends. It only waits.', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520106, 'Cut me apart, and the Nether multiplies!', 'Cut me apart, and the Nether multiplies!', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520107, 'One rift becomes many!', 'One rift becomes many!', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520108, 'The Rift will swallow this world whole!', 'The Rift will swallow this world whole!', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520109, 'Another soul adrift in the Twisting Nether.', 'Another soul adrift in the Twisting Nether.', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520110, 'Drift, little mortal. Forever.', 'Drift, little mortal. Forever.', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6520111, 'The Rift... closes... but the Nether... remembers...', 'The Rift... closes... but the Nether... remembers...', 1, 0, 0, 0, 0, 0, 0, 0, 0);
