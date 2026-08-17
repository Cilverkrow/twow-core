-- Northwind: link the Balor lore book pages.
-- The Siege of Balor (GO 2020127) and The Founding of Balor (GO 2020128)
-- only showed their first page because every page_text row had
-- next_page = 0. Chain the existing pages: Siege = 50734-50747
-- (14 pages), Founding = 50748-50757 (10 pages). The last page of each
-- chain keeps next_page = 0.

UPDATE `page_text` SET `next_page` = `entry` + 1 WHERE `entry` BETWEEN 50734 AND 50746;
UPDATE `page_text` SET `next_page` = `entry` + 1 WHERE `entry` BETWEEN 50748 AND 50756;
