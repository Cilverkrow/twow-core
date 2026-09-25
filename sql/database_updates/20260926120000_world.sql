-- Issue twow-repo#347: Prince Thunderaan (14435) must be attackable.
-- The Turtle template re-import world/20260510092659_world.sql changed his faction from 91
-- (hostile elemental, as in sql/base/tw_world_creature_template.sql) to 35 (friendly to all).
-- npc_prince_thunderaanAI never changes faction, so the summoned boss cannot be fought and the
-- Thunderfury chain (7786 -> 7787) is blocked. Restore the base value. The guard keeps the
-- update a no-op on replay and does not override any later deliberate change.
UPDATE creature_template SET faction = 91 WHERE entry = 14435 AND faction = 35;
