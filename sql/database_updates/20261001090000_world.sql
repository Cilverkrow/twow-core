-- twow-repo#455, train 8b (owner approval 2026-09-30 ~22:00Z): the 1.12 client and parts of
-- the protocol carry spell IDs in 16 bits (SendInitialSpells, SMSG_REMOVED_SPELL,
-- SMSG_SUPERCEDED_SPELL), so every custom spell >= 65536 reaches players as another spell
-- (90150 -> 24614 "Consuming Shadows", 90164 -> 24628 "Summon Amnennar"). Bots never use
-- those packets, which is why they were unaffected.
--
-- All 116 custom spells 90001-90219 move to 61002-61220: new = old - 28999 (the ID table in
-- #455, issuecomment-5920485149, sha256 09e4fbfe...; checked by OB-50). 61002-65535 is free in
-- spell_template and in the client Spell.dbc (both end at 61001). The Agitating Poison
-- enchantments move with them: 90141-90144 -> 3060-3063 (client SpellItemEnchantment.dbc
-- ends at 3059). Item entries (90140-90144) keep their IDs: item IDs travel as uint32.
--
-- Coupled release: client patch v3 and the server DBCs (Talent, SkillLineAbility,
-- SpellItemEnchantment) with the same IDs (OB-15), the character tables (character_spell,
-- character_aura, character_action, item_instance.enchantments ...; OB-40).
--
-- References moved here (world-refs scan of every spell column, #455):
-- spell_template entry, effectTriggerSpell1-3, tooltip variables ($90145 ...) and the
-- enchantment in effectMiscValue1 of the coating spells; spell_proc_event; npc_trainer;
-- skill_line_ability spell_id and superseded_by_spell; item_template spellid_1-5.
-- No hits in spell_chain, spell_affect, spell_learn_spell, spell_script_target, spell_mod,
-- creature_spells or the *_scripts tables (creature_spells 90160/90170 are list IDs).
--
-- Replay-safe: every statement only matches the old range. A leftover row in 61002-61220
-- makes the first UPDATE fail on the primary key instead of merging two spells.

UPDATE `spell_template` SET `entry` = `entry` - 28999 WHERE `entry` BETWEEN 90001 AND 90219;

UPDATE `spell_template` SET `effectTriggerSpell1` = `effectTriggerSpell1` - 28999 WHERE `effectTriggerSpell1` BETWEEN 90001 AND 90219;
UPDATE `spell_template` SET `effectTriggerSpell2` = `effectTriggerSpell2` - 28999 WHERE `effectTriggerSpell2` BETWEEN 90001 AND 90219;
UPDATE `spell_template` SET `effectTriggerSpell3` = `effectTriggerSpell3` - 28999 WHERE `effectTriggerSpell3` BETWEEN 90001 AND 90219;

-- Tooltip variables that name another custom spell ($90145s1 = Shadow Dance dodge buff ...).
UPDATE `spell_template` SET `description` = REPLACE(`description`, '$90145', '$61146'), `auraDescription` = REPLACE(`auraDescription`, '$90145', '$61146') WHERE `description` LIKE '%$90145%' OR `auraDescription` LIKE '%$90145%';
UPDATE `spell_template` SET `description` = REPLACE(`description`, '$90146', '$61147'), `auraDescription` = REPLACE(`auraDescription`, '$90146', '$61147') WHERE `description` LIKE '%$90146%' OR `auraDescription` LIKE '%$90146%';
UPDATE `spell_template` SET `description` = REPLACE(`description`, '$90191', '$61192'), `auraDescription` = REPLACE(`auraDescription`, '$90191', '$61192') WHERE `description` LIKE '%$90191%' OR `auraDescription` LIKE '%$90191%';
UPDATE `spell_template` SET `description` = REPLACE(`description`, '$90200', '$61201'), `auraDescription` = REPLACE(`auraDescription`, '$90200', '$61201') WHERE `description` LIKE '%$90200%' OR `auraDescription` LIKE '%$90200%';
UPDATE `spell_template` SET `description` = REPLACE(`description`, '$90201', '$61202'), `auraDescription` = REPLACE(`auraDescription`, '$90201', '$61202') WHERE `description` LIKE '%$90201%' OR `auraDescription` LIKE '%$90201%';
UPDATE `spell_template` SET `description` = REPLACE(`description`, '$90202', '$61203'), `auraDescription` = REPLACE(`auraDescription`, '$90202', '$61203') WHERE `description` LIKE '%$90202%' OR `auraDescription` LIKE '%$90202%';
UPDATE `spell_template` SET `description` = REPLACE(`description`, '$90203', '$61204'), `auraDescription` = REPLACE(`auraDescription`, '$90203', '$61204') WHERE `description` LIKE '%$90203%' OR `auraDescription` LIKE '%$90203%';

-- Agitating Poison coatings (now 61205-61208): enchantment 90141-90144 -> 3060-3063.
UPDATE `spell_template` SET `effectMiscValue1` = `effectMiscValue1` - 87081 WHERE `entry` BETWEEN 61205 AND 61208 AND `effect1` = 54 AND `effectMiscValue1` BETWEEN 90141 AND 90144;

UPDATE `spell_proc_event` SET `entry` = `entry` - 28999 WHERE `entry` BETWEEN 90001 AND 90219;

UPDATE `npc_trainer` SET `spell` = `spell` - 28999 WHERE `spell` BETWEEN 90001 AND 90219;

UPDATE `skill_line_ability` SET `spell_id` = `spell_id` - 28999 WHERE `spell_id` BETWEEN 90001 AND 90219;
UPDATE `skill_line_ability` SET `superseded_by_spell` = `superseded_by_spell` - 28999 WHERE `superseded_by_spell` BETWEEN 90001 AND 90219;

UPDATE `item_template` SET `spellid_1` = `spellid_1` - 28999 WHERE `spellid_1` BETWEEN 90001 AND 90219;
UPDATE `item_template` SET `spellid_2` = `spellid_2` - 28999 WHERE `spellid_2` BETWEEN 90001 AND 90219;
UPDATE `item_template` SET `spellid_3` = `spellid_3` - 28999 WHERE `spellid_3` BETWEEN 90001 AND 90219;
UPDATE `item_template` SET `spellid_4` = `spellid_4` - 28999 WHERE `spellid_4` BETWEEN 90001 AND 90219;
UPDATE `item_template` SET `spellid_5` = `spellid_5` - 28999 WHERE `spellid_5` BETWEEN 90001 AND 90219;
