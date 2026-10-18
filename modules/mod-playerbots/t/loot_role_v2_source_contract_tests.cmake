# twow-repo#308 LOOT-ROLE-01 v2 (owner 17.10.2026): AiPlayerbot.Loot.RoleWeights = 2 applies the owner stat table v2 in
# memory. Every TSV row (except the two tank rows waiting for the owner's OK) must be in LootRoleV2Weights.h unchanged.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#308 v2: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#308 v2: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomItemMgr.cpp" items)
read_source("LootRoleV2Weights.h" table)
file(READ "${PB_SOURCE_DIR}/../../data/stat-weights/stat-priorities-v2.tsv" tsv)
string(REPLACE "\r" "" tsv "${tsv}")
string(REPLACE ";" "," tsv "${tsv}")   # notes contain semicolons, the CMake list separator

# Switch 0..2, default 0; v2 only at 2, after the scales are loaded, before the vendor/item caches use them.
require_text("${config_h}" "uint32 lootRoleWeights = 0;" "default 0")
require_text("${config_cpp}" "lootRoleWeights = std::min<uint32>(2, uint32(std::max<int32>(0, config.GetIntDefault(\"AiPlayerbot.Loot.RoleWeights\", 0))));" "0..2")
require_text("${conf_dist}" "AiPlayerbot.Loot.RoleWeights = 0" "documented")
require_text("${items}" "if (sPlayerbotAIConfig.lootRoleWeights >= 2)\n        ApplyOwnerWeightsV2();" "v2 only at 2")
require_order("${items}" "sLog.outError(\"Error loading item weight scales\");" "ApplyOwnerWeightsV2();" "after the scales load")
require_order("${items}" "ApplyOwnerWeightsV2();" "sLog.outString(\"Loading vendor item list...\");" "before the caches")
require_text("${items}" "if (ai::loot_v2::KeepFromOldScale(old.stat))" "only weapon DPS kept from the old scale")
require_text("${table}" "inline constexpr std::uint32_t PrioWeight[5] = { 100, 80, 60, 40, 20 };" "owner weights 100/80/60/40/20")

# Every TSV row in the table, unchanged.
string(REPLACE "\n" ";" lines "${tsv}")
set(rows 0)
foreach(line IN LISTS lines)
  if(line STREQUAL "" OR line MATCHES "^class_id")
    continue()
  endif()
  string(REPLACE "\t" ";" f "${line}")
  list(GET f 0 cls)
  list(GET f 3 spec)
  list(GET f 5 p1)
  list(GET f 6 p2)
  list(GET f 7 p3)
  list(GET f 8 p4)
  list(GET f 9 p5)
  list(GET f 11 school)
  if(spec STREQUAL "shaman tank" OR spec STREQUAL "rogue tank")
    # Owner 17.10.: weights proposed, until his OK the tank paths keep enhance/combat.
    string(FIND "${table}" "\"${spec}\"" present)
    if(NOT present EQUAL -1)
      message(FATAL_ERROR "#308 v2: '${spec}' must stay out until the owner's OK")
    endif()
    continue()
  endif()
  require_text("${table}" "{ ${cls}, \"${spec}\", { \"${p1}\", \"${p2}\", \"${p3}\", \"${p4}\", \"${p5}\" }, \"${school}\", \"" "TSV row ${cls}/${spec}")
  math(EXPR rows "${rows}+1")
endforeach()
if(NOT rows EQUAL 28)
  message(FATAL_ERROR "#308 v2: expected 28 TSV rows in the table, checked ${rows}")
endif()

message(STATUS "loot_role_v2 source contract passed (28 rows against the TSV)")
