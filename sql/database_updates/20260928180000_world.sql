-- Issue twow-repo#348 round 3 (owner, 2026-09-28, video evidence ws-30/348-voidboss-video2):
-- the Voidlord's pulse (Shadow Nova 45559) shows the golden Holy Nova look in the client
-- (its client visual 3643 uses holynova_impact_base). The owner prefers the warlock's Hellfire.
-- Spell visuals come from the client Spell.dbc by spell ID, so the boss now casts 2951
-- "Hellfire III" (an old NPC spell with the Hellfire visual 781; no trainer, creature_spells,
-- EventAI or C++ uses it) and 2951 gets the nova's values server-side. A purple colour would
-- need a client patch (#409) and is not part of this change. The boss is also 20 % larger.
--
-- Rollback (values before this migration):
--   UPDATE spell_template SET school = 2, castingTimeIndex = 14, rangeIndex = 4, manaCost = 32,
--     effectImplicitTargetA1 = 16, effectImplicitTargetB1 = 0, effectRadiusIndex1 = 13,
--     effectBasePoints1 = 87, effectDieSides1 = 15, effectBonusCoefficient1 = 1 WHERE entry = 2951;
--   UPDATE creature_template SET scale = 4.5 WHERE entry IN (65201, 65202);
-- Replay-safe: fixed values.

UPDATE `spell_template`
   SET `school` = 5, `castingTimeIndex` = 1, `rangeIndex` = 1, `manaCost` = 0,
       `effectImplicitTargetA1` = 22, `effectImplicitTargetB1` = 15, `effectRadiusIndex1` = 14,
       `effectBasePoints1` = 235, `effectDieSides1` = 42, `effectBonusCoefficient1` = -1
 WHERE `entry` = 2951;

UPDATE `creature_template` SET `scale` = 5.4 WHERE `entry` IN (65201, 65202);
