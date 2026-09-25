-- Issue twow-repo#345: reviewed boss reward chests for the funserver bonus loot.
-- Source: modules/mod-dungeon-clear/data/boss-chest-bonus-345-a.csv from the read-only live chest
-- census (2026-09-25): chest type, <= 2 spawns in its instance, own non-shared loot table with
-- blue+ items, boss/event reward. Random chests, resource nodes and quest objects are excluded.
-- Consumed only with Funserver.Loot.Bonus.Enabled and Funserver.Loot.Bonus.BossChest. No loot-table
-- change; INSERT IGNORE makes a replay a no-op.
CREATE TABLE IF NOT EXISTS gameobject_loot_bonus_registry (
    gameobject_entry MEDIUMINT UNSIGNED NOT NULL,
    map_id SMALLINT UNSIGNED NOT NULL,
    category VARCHAR(8) NOT NULL,
    note VARCHAR(255) NOT NULL,
    PRIMARY KEY (gameobject_entry, map_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO gameobject_loot_bonus_registry (gameobject_entry, map_id, category, note) VALUES
    (141596,209,'dungeon','#345 boss chest: Witch Doctor''s Chest'),
    (161495,230,'dungeon','#345 boss chest: Secret Safe'),
    (169243,230,'dungeon','#345 boss chest: Chest of The Seven'),
    (181074,230,'dungeon','#345 boss chest: Arena Spoils'),
    (176944,289,'dungeon','#345 boss chest: Old Treasure Chest'),
    (179703,409,'raid','#345 boss chest: Cache of the Firelord'),
    (179501,429,'dungeon','#345 boss chest: Knot Thimblejack''s Cache'),
    (179564,429,'dungeon','#345 boss chest: Gordok Tribute'),
    (300400,429,'dungeon','#345 boss chest: Gordok Tribute'),
    (300401,429,'dungeon','#345 boss chest: Gordok Tribute'),
    (181366,533,'raid','#345 boss chest: Four Horsemen Chest'),
    (379545,800,'dungeon','#345 boss chest: Half-Buried Treasure Chest'),
    (2020027,815,'dungeon','#345 boss chest: Harlow Family Chest');
