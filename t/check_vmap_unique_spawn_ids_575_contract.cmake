cmake_policy(VERSION 3.16)
if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#575 (map data repair, owner 25.10.2026): vmap_assembler must not lose spawns that reuse an id.
# Turtle content reuses spawn ids across WMO and M2 spawns; TileAssembler::readMapSpawns kept the first spawn per id and wrote it into
# the tiles of every other spawn with that id (9,763 spawns lost on maps 0/1 in the 25.08.2026 data, among them 211 buildings).
# 1. readMapSpawns gives a spawn its unique id BEFORE it is inserted into UniqueEntries and TileEntries.
# 2. uniqueSpawnId: same spawn = same model name and position within 0.5 yd (squared 0.25); further distinct spawns get
#    NextUniqueId++ from UNIQUE_SPAWN_ID_BASE 0x40000000 (per map); repeated records of one spawn reuse its id; legacy mode returns the id.
# 3. vmap_assembler: "--legacy-ids" (control runs) is the only way back to the old behaviour; the default is unique ids.
set(cpp "${TW_CORE_ROOT}/src/game/vmap/TileAssembler.cpp")
set(hdr "${TW_CORE_ROOT}/src/game/vmap/TileAssembler.h")
set(tool "${TW_CORE_ROOT}/tools/vmap_assembler/vmap_assembler.cpp")

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#575 vmap ids ${label}: missing ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#575 vmap ids ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

foreach (f "${cpp}" "${hdr}" "${tool}")
  if (NOT EXISTS "${f}")
    message(FATAL_ERROR "#575 vmap ids: missing ${f}")
  endif()
endforeach()
file(READ "${cpp}" c)
file(READ "${hdr}" h)
file(READ "${tool}" t)

# 1. order in readMapSpawns
string(FIND "${c}" "bool TileAssembler::readMapSpawns()" rm)
if (rm EQUAL -1)
  message(FATAL_ERROR "#575 vmap ids: readMapSpawns not found")
endif()
string(SUBSTRING "${c}" ${rm} -1 body)
require_order("${body}" "spawn.ID = uniqueSpawnId(*current, spawn);" "current->UniqueEntries.insert(pair<uint32, ModelSpawn>(spawn.ID, spawn));" "unique id before UniqueEntries")
require_order("${body}" "spawn.ID = uniqueSpawnId(*current, spawn);" "current->TileEntries.insert(pair<uint32, uint32>(StaticMapTree::packTileID(tileX, tileY), spawn.ID));" "unique id before TileEntries")

# 2. the rule
require_text("${c}" "uint32 TileAssembler::uniqueSpawnId(MapSpawns& map, ModelSpawn const& spawn)" "definition")
require_text("${c}" "return other.name == spawn.name && (other.iPos - spawn.iPos).squaredLength() < 0.25f;" "same-spawn rule")
require_text("${c}" "if (iLegacySpawnIds || first == map.UniqueEntries.end() || sameSpawn(first->second))" "first spawn and legacy keep the id")
require_text("${c}" "copy.ID = map.NextUniqueId++;" "fresh id")
require_text("${h}" "static uint32 const UNIQUE_SPAWN_ID_BASE = 0x40000000;" "id base")
require_text("${h}" "uint32 NextUniqueId = UNIQUE_SPAWN_ID_BASE;" "per-map counter")
require_text("${h}" "bool iLegacySpawnIds = false;" "default is unique ids")

# 3. the tool flag
require_text("${t}" "if (strcmp(argv[i], \"--legacy-ids\") == 0)" "legacy flag")
require_text("${t}" "ta->setLegacySpawnIds(legacyIds);" "flag reaches the assembler")

message(STATUS "#575 vmap unique spawn ids contract OK")
