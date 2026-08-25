// DERIVED from the navmesh itself, not from a live clear.
// Map 36, boss 647 (Captain Greenskin), 22 anchors over 213yd.
//
// The mesh carries this path - a plain Detour query walks it in 55
// polygons using the very filter the core queries with - but the
// module's own chunked builder returns an incomplete route and the
// party ends up in the water beside the ship. These anchors are the
// corner points of that Detour corridor, so following them walks the
// same ramp a player walks. A live clear that does better replaces
// this file through the ordinary shortest-wins rule.
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"

void RegisterRecordedRoute36_647()
{
    DungeonClearRouteRegistry::Register(36, DUNGEON_DIFFICULTY_NORMAL, 647,
        {
            { -23.00f, -797.00f, 20.39f },
            { -26.40f, -796.00f, 19.36f },
            { -42.67f, -789.33f, 18.66f },
            { -45.87f, -787.73f, 18.46f },
            { -49.33f, -786.67f, 18.36f },
            { -78.93f, -786.93f, 17.46f },
            { -106.67f, -787.47f, 17.26f },
            { -106.67f, -786.13f, 19.46f },
            { -100.27f, -782.67f, 22.06f },
            { -91.73f, -780.00f, 24.56f },
            { -88.80f, -780.27f, 26.96f },
            { -88.53f, -781.07f, 26.86f },
            { -88.80f, -781.87f, 26.86f },
            { -100.53f, -794.67f, 28.06f },
            { -97.60f, -799.47f, 30.76f },
            { -96.80f, -800.00f, 32.06f },
            { -58.67f, -793.07f, 39.06f },
            { -50.93f, -794.93f, 38.76f },
            { -45.87f, -797.07f, 39.36f },
            { -45.07f, -798.40f, 39.56f },
            { -48.53f, -804.00f, 42.76f },
            { -69.00f, -808.00f, 40.81f },
        });
}
