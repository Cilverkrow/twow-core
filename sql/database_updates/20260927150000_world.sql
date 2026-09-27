-- Issue twow-repo#357 (shaman tank, Enhancement rework phase 1), route A: owner decision O-10
-- (2026-09-27, relayed by OB-00 under Ä13, recorded in #357). Existing Elemental Weapons ranks
-- get the owner's values; this is a rule change for players and bots (tooltips stay old until
-- the phase-2 client patch). Effective value = basepoints + 1. Replay-safe: fixed values.
--
--                     rank 1 / 2 / 3   before
--   Flametongue       17 / 33 / 50 %   10 / 20 / 30   (Enkindled Flames 52970-52972, aura 79)
--   Frostbrand        16 / 33 / 50 %    8 / 16 / 25   (58248-58250, aura 107 chance of success)
--   Windfury           2 % per stack     1 % per stack (Rushing Winds 52967-52969; stacks 2/4/6 kept)
--   Rockbiter build   10 / 20 / 30 %   20 / 20 / 20   (Elemental Weapons 16266/29079/29080 effect 3)
--   Rockbiter absorb  15 / 20 / 25 %    5 / 10 / 15   (Earthen Bulwark 58128-58130)
-- The Earthen Bulwark cap (40 % of max health at 3/3) is code: spell_shaman.cpp.

UPDATE spell_template SET effectBasePoints1 = 16 WHERE entry = 52970;
UPDATE spell_template SET effectBasePoints1 = 32 WHERE entry = 52971;
UPDATE spell_template SET effectBasePoints1 = 49 WHERE entry = 52972;

UPDATE spell_template SET effectBasePoints1 = 15 WHERE entry = 58248;
UPDATE spell_template SET effectBasePoints1 = 32 WHERE entry = 58249;
UPDATE spell_template SET effectBasePoints1 = 49 WHERE entry = 58250;

UPDATE spell_template SET effectBasePoints1 = 1 WHERE entry IN (52967, 52968, 52969);

UPDATE spell_template SET effectBasePoints3 = 9 WHERE entry = 16266;
UPDATE spell_template SET effectBasePoints3 = 19 WHERE entry = 29079;
UPDATE spell_template SET effectBasePoints3 = 29 WHERE entry = 29080;

UPDATE spell_template SET effectBasePoints1 = 14 WHERE entry = 58128;
UPDATE spell_template SET effectBasePoints1 = 19 WHERE entry = 58129;
UPDATE spell_template SET effectBasePoints1 = 24 WHERE entry = 58130;
