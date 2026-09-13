-- Issue #288 follow-up: the 16 rows in
-- modules/mod-dungeon-clear/data/bonus-loot-coverage-followup-02.csv are the
-- sealed WS-20 additions. This migration never touches normal loot-table data.
--
-- INSERT IGNORE preserves any pre-existing row verbatim and makes a replay a
-- no-op. The two explicitly audited trash/add keys are deleted only here;
-- repeating either deletion is also a no-op.
INSERT IGNORE INTO creature_loot_bonus_registry (creature_entry, map_id, category, note) VALUES
    (3886,33,'dungeon','WS20 coverage follow-up: Razorclaw the Butcher'),
    (3887,33,'dungeon','WS20 coverage follow-up: Baron Silverlaine'),
    (4274,33,'dungeon','WS20 coverage follow-up: Fenrus the Devourer'),
    (61969,33,'dungeon','WS20 coverage follow-up: Prelate Ironmane'),
    (1663,34,'dungeon','WS20 coverage follow-up: Dextren Ward'),
    (1716,34,'dungeon','WS20 coverage follow-up: Bazil Thredd'),
    (3653,43,'dungeon','WS20 coverage follow-up: Kresh'),
    (3671,43,'dungeon','WS20 coverage follow-up: Lady Anacondra'),
    (3673,43,'dungeon','WS20 coverage follow-up: Lord Serpentis'),
    (61965,43,'dungeon','WS20 coverage follow-up: Vangros'),
    (61968,43,'dungeon','WS20 coverage follow-up: Zandara Windhoof'),
    (4830,48,'dungeon','WS20 coverage follow-up: Old Serra''kis'),
    (4832,48,'dungeon','WS20 coverage follow-up: Twilight Lord Kelris'),
    (6243,48,'dungeon','WS20 coverage follow-up: Gelihast'),
    (62530,48,'dungeon','WS20 coverage follow-up: Velthelaxx the Defiler'),
    (61204,532,'raid','WS20 coverage follow-up: Dark Rider Champion');

DELETE FROM creature_loot_bonus_registry
WHERE (creature_entry, map_id) IN ((12129,249), (12119,409));
