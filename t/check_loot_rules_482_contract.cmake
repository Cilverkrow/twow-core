if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#482 (train 9): owner rule table v5 runs behind Funserver.Loot.Rules482.Enabled
# (core default off, so a rollback needs no build), only for raids with a profile and dungeons
# with a band; raids never draw from the BoE pool.
file(READ "${TW_CORE_ROOT}/src/game/World.cpp" world)
string(FIND "${world}" "setConfig(CONFIG_BOOL_FUNSERVER_LOOT_RULES_482, \"Funserver.Loot.Rules482.Enabled\", false);" at)
if (at EQUAL -1)
  message(FATAL_ERROR "Funserver.Loot.Rules482.Enabled must default to off")
endif()

file(READ "${TW_CORE_ROOT}/src/game/LootMgr.cpp" loot)
foreach (required
    "if (HasFunserverLootRules482(unitsContent, bonusOwner->GetMapId()))"
    "tab->ProcessRules482(*this, loot_owner, bonusOwner, unitsContent"
    "return value.empty() ? std::string(fallback) : value;"
    "LootCounts const kept = Trim(counts, MAX_NR_LOOT_ITEMS, setNeed);")
  string(FIND "${loot}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#482 rules wiring missing: ${required}")
  endif()
endforeach()

# The raid branch of ProcessRules482 must not touch the BoE pool (owner: no BoE in raids).
string(FIND "${loot}" "// ---------------- raids: set pieces or tokens, own items, trim; no BoE ----------------" raid_at)
string(FIND "${loot}" "[LootRules482] raid map=" raid_end)
if (raid_at EQUAL -1 OR raid_end EQUAL -1 OR NOT raid_at LESS raid_end)
  message(FATAL_ERROR "raid branch of ProcessRules482 not found")
endif()
math(EXPR raid_len "${raid_end} - ${raid_at}")
string(SUBSTRING "${loot}" ${raid_at} ${raid_len} raid)
string(FIND "${raid}" "sFunserverBoePool" boe)
if (NOT boe EQUAL -1)
  message(FATAL_ERROR "raids must not draw from the BoE pool")
endif()

file(READ "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in" dist)
foreach (key "Funserver.Loot.Rules482.Enabled = 0" "Funserver.Loot.Raid.Maps = \"\"" "Funserver.Loot.Dungeon.Maps = \"\""
             "Funserver.Loot.Raid.Tokens = \"\"" "Loot.MaxItems = 16")
  string(FIND "${dist}" "${key}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "mangosd.conf.dist.in must document: ${key}")
  endif()
endforeach()

message(STATUS "LOOT_RULES_482_CONTRACT=PASS")
