#pragma once

namespace ai::gather_node
{
// twow-repo#414 (train 7, 2026-09-28): Shanie (L12, Tirisfal) died 19 times at the
// same spot without a killer while her travel target was "gather from Beached Sea
// Creature" - quest objects on Kalimdor (Darkshore). Their lock 259 has a
// herbalism slot next to "open kneeling", so the gather logic took them for herbs;
// the bot ran from Tirisfal into the open sea and died of fatigue (bots have
// infinite breath, not fatigue immunity). A world object is a gathering node only
// when it is a chest-type object (herbs and veins are) whose lock asks for nothing
// but a profession skill. Lock 259 ("open kneeling" + herbalism) also marks quest
// plants (Serpentbloom, Bloodpetal Sprout, Gloom Weed ...): quest travel still
// reaches them, they are just no herbalism skill-up targets.
inline bool IsGatherNode(bool isChestType, bool lockHasNonProfessionSkill)
{
    return isChestType && !lockHasNonProfessionSkill;
}

// #414: gathering (herbs, ore, skinning, fishing) stays on the bot's own map - a
// node on another continent is never worth the trip, and the direct way there
// runs into the open sea (fatigue). Other purposes keep their cross-map routes.
inline bool StaysOnMap(bool gatherPurpose, unsigned botMap, unsigned targetMap)
{
    return !gatherPurpose || botMap == targetMap;
}
}
