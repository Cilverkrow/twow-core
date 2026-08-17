-- Northwind: remove duplicate quiz options from Randolph's menu (62302).
-- The menu contained authoring leftovers: a second "Barathen Wrynn."
-- (id 3, no reply menu or script) and a second "Llane Wrynn." (id 4,
-- pointing at the wrong-answer text menu 30355). Removing them leaves
-- one option per answer; the single "Barathen Wrynn." (id 1) carries the
-- School Assistance (41637) credit script.

DELETE FROM `gossip_menu_option` WHERE `menu_id` = 62302 AND `id` IN (3, 4);
