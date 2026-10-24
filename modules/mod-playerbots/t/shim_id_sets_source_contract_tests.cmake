# twow-repo#510 (part A + A2): AiPlayerbot.Shim.IdSets and AiPlayerbot.Shim.FactionIdSet (default 0). The creature/GO
# scans iterate Ids() (the old count with 0, the sorted template ids with 1); the two faction loops iterate FactionIds()
# (ids below GetNumRows() with 0, the bot's reputation list with 1). [ShimBounds] reports the exposure once per boot.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(read_source path out_var)
  read_file("${PB_SOURCE_DIR}/${path}" text)
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "shim id sets: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "shim id sets: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "shim id sets: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected description)
  string(REGEX MATCHALL "${needle}" hits "${text}")
  list(LENGTH hits count)
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "shim id sets: ${description}: expected ${expected}, found ${count}")
  endif()
endfunction()

# The proxy body from "struct <name>" to the closing "};".
function(proxy_body text name out_var)
  string(FIND "${text}" "struct ${name}\n{" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "shim id sets: proxy not found: ${name}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "\n};" b)
  string(SUBSTRING "${rest}" 0 ${b} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_file("${PB_MODULE_DIR}/cmangos-compat-shim.h" shim)
read_source("ShimIdRange.h" range)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel_mgr)

# 1. Caps and proxies: creature/GO have no GetMaxEntry (a new cap loop fails to compile), item and faction keep
#    their fixed caps (the faction cap is the A2 off path); FactionTemplate (1500) and AreaTrigger (10000) stay.
require_text("${shim}" "constexpr uint32 kLegacyItemCap = 100000;" "item cap")
require_text("${shim}" "constexpr uint32 kLegacyCreatureCap = 100000;" "creature cap")
require_text("${shim}" "constexpr uint32 kLegacyGOCap = 200000;" "GO cap")
require_text("${shim}" "constexpr uint32 kLegacyFactionCap = 100;" "faction cap")
require_text("${shim}" "inline std::atomic<bool> g_idSets{false};" "id-set mode off until WarmIdSets")
proxy_body("${shim}" "CmangosCreatureStorageProxy" creature)
proxy_body("${shim}" "CmangosGOStorageProxy" go)
proxy_body("${shim}" "CmangosItemStorageProxy" item)
proxy_body("${shim}" "CmangosFactionStoreProxy" faction)
forbid_text("${creature}" "GetMaxEntry()" "the creature proxy must not offer GetMaxEntry")
forbid_text("${go}" "GetMaxEntry()" "the GO proxy must not offer GetMaxEntry")
require_text("${creature}" "cmangos_shim::IdRange Ids(uint32 lo = 0, uint32 hi = cmangos_shim::kLegacyCreatureCap) const" "creature Ids() defaults to the old cap")
require_text("${go}" "cmangos_shim::IdRange Ids(uint32 lo = 0, uint32 hi = cmangos_shim::kLegacyGOCap) const" "GO Ids() defaults to the old cap")
foreach(body creature go)
  require_text("${${body}}" "return cmangos_shim::g_idSets.load(std::memory_order_relaxed) ? cmangos_shim::IdRange::Set(AllIds(), lo) : cmangos_shim::IdRange::Count(lo, hi);" "${body}: set only in mode 1, else the old count")
endforeach()
require_text("${creature}" "cmangos_shim::SortedKeys(sObjectMgr.GetCreatureInfoMap())" "creature set from the template map")
require_text("${go}" "cmangos_shim::SortedKeys(sObjectMgr.GetGameObjectInfoMap())" "GO set from the template map")
require_text("${item}" "uint32 GetMaxEntry() const { return cmangos_shim::kLegacyItemCap; }" "item cap unchanged")
require_text("${faction}" "uint32 GetNumRows() const { return cmangos_shim::kLegacyFactionCap; }" "faction cap unchanged (A2 off path)")
require_text("${shim}" "T const* LookupEntry(uint32 id) const { return sObjectMgr.GetFactionTemplateEntry(id); }\n    uint32 GetNumRows() const { return 1500; }" "FactionTemplate stub out of scope")
require_text("${shim}" "uint32 GetNumRows() const { return 10000; } // upper bound stub" "AreaTrigger stub out of scope")
# A2 (OB-10 review): the faction lookup itself has no cap - only the count of the off path stops at 100.
require_text("${faction}" "T const* LookupEntry(uint32 id) const { return sObjectMgr.GetFactionEntry(id); }" "faction lookup without a cap")
forbid_text("${faction}" "kLegacyFactionCap ?" "capped faction lookup")

# 2. WarmIdSets: the mode is set once; off builds no set.
string(FIND "${shim}" "inline void WarmIdSets(bool on)" warm_at)
string(SUBSTRING "${shim}" ${warm_at} 900 warm)
require_order("${warm}" "if (warmed.exchange(true))\n        return;" "if (!on)" "the first call fixes the mode")
require_order("${warm}" "if (!on)\n    {\n        sLog.outString(\"[ShimBounds] warm mode=0\");\n        return;\n    }" "AllIds()" "off returns before any set is built")
require_order("${warm}" "AllIds()" "g_idSets.store(true, std::memory_order_relaxed);" "sets built before the mode turns on")

# 3. Counting mode is the old loop, set mode walks the sorted ids from lo (t/shim_id_range_tests.cpp runs them).
require_text("${range}" "static IdRange Count(std::uint32_t lo, std::uint32_t hi) { return IdRange(iterator(nullptr, lo), iterator(nullptr, std::max(lo, hi))); }" "counting mode [lo, hi)")
require_text("${range}" "iterator(std::lower_bound(first, last, lo), 0), iterator(last, 0)" "set mode from lo to the end")
require_text("${range}" "iterator end() const { return iterator(m_list.end(), m_on ? 0 : m_cap, m_on); }" "faction off path counts to the cap")
require_text("${shim}" "return FactionIdRange<StateList>(list, on, sFactionStore.GetNumRows());" "faction off path cap is GetNumRows()")

# 4. Switches: member, config read and documented default 0, both keys.
require_text("${config_h}" "bool shimIdSets = false;" "IdSets member default off")
require_text("${config_h}" "bool shimFactionIdSet = false;" "FactionIdSet member default off")
require_text("${config_cpp}" "shimIdSets = config.GetBoolDefault(\"AiPlayerbot.Shim.IdSets\", false);" "IdSets config default 0")
require_text("${config_cpp}" "shimFactionIdSet = config.GetBoolDefault(\"AiPlayerbot.Shim.FactionIdSet\", false);" "FactionIdSet config default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Shim.IdSets = 0\n" "IdSets documented as 0")
require_text("${conf_dist}" "\nAiPlayerbot.Shim.FactionIdSet = 0\n" "FactionIdSet documented as 0")

# 5. WarmIdSets runs before the first creature/GO scan of the boot.
require_order("${config_cpp}" "shimIdSets = config.GetBoolDefault(" "cmangos_shim::WarmIdSets(shimIdSets);" "switch read before the warm-up")
require_order("${config_cpp}" "cmangos_shim::WarmIdSets(shimIdSets);" "    PlayerbotFactory::Init();\n    sRandomItemMgr.Init();" "warm-up before PlayerbotFactory and RandomItemMgr")

# 6. The 13 creature/GO scans iterate Ids(); no cap loop is left (fixed file list, ahbot via PB_MODULE_DIR).
set(callers
  "${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp|2|creature:1 go:1"
  "${PB_SOURCE_DIR}/strategy/values/LootValues.cpp|2|creature:1 go:1"
  "${PB_SOURCE_DIR}/strategy/values/VendorValues.cpp|1|creature:1"
  "${PB_SOURCE_DIR}/strategy/values/TrainerValues.cpp|1|creature:1"
  "${PB_SOURCE_DIR}/strategy/actions/AutoLearnSpellAction.cpp|1|creature:1"
  "${PB_SOURCE_DIR}/PlayerbotFactory.cpp|2|creature:2"
  "${PB_SOURCE_DIR}/RandomItemMgr.cpp|2|creature:1 go:1"
  "${PB_SOURCE_DIR}/TravelNode.cpp|1|go:1"
  "${PB_MODULE_DIR}/src/ahbot/TradeCategory.cpp|1|creature:1")
set(total 0)
foreach(entry ${callers})
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 path)
  list(GET parts 1 expected)
  read_file("${path}" text)
  forbid_text("${text}" "sCreatureStorage.GetMaxEntry()" "cap loop left in ${path}")
  forbid_text("${text}" "sGOStorage.GetMaxEntry()" "cap loop left in ${path}")
  string(REGEX MATCHALL "for \\(uint32 [a-z]+ : s(Creature|GO)Storage\\.Ids\\([0-9, ]*\\)\\)" hits "${text}")
  list(LENGTH hits count)
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "shim id sets: ${path}: expected ${expected} Ids() loops, found ${count}")
  endif()
  math(EXPR total "${total} + ${count}")
endforeach()
if(NOT total EQUAL 13)
  message(FATAL_ERROR "shim id sets: expected 13 Ids() loops, found ${total}")
endif()
read_source("TravelNode.cpp" travel_node)
require_text("${travel_node}" "for (uint32 entry : sGOStorage.Ids(1, 200001))" "transport nodes keep the old 1..200000 count in mode 0")

# 7. A2: both faction loops take the switch before the loop; MANGOSBOT_ONE keeps its own loop.
read_source("strategy/triggers/GenericTriggers.cpp" triggers)
read_source("strategy/actions/FactionAction.cpp" faction_action)
foreach(name triggers faction_action)
  require_text("${${name}}" "#ifndef MANGOSBOT_ONE\n    // twow-repo#510 A2: AiPlayerbot.Shim.FactionIdSet = 1 walks the reputation list (factions >= 100 too); 0 = ids below GetNumRows() as before.\n    for (uint32 id : cmangos_shim::FactionIds(mgr.GetStateList(), sPlayerbotAIConfig.shimFactionIdSet))\n#else\n    for (uint32 id = 0; id < sFactionStore.GetMaxEntry(); ++id)\n#endif" "${name}: faction loop behind the switch")
  forbid_text("${${name}}" "for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)" "${name}: the old faction loop without the switch is left")
  # OB-10 review: the body resolves the faction through the uncapped lookup, so ids >= the cap stay in (see section 1).
  require_text("${${name}}" "#endif\n    {\n#ifndef MANGOSBOT_ONE\n        const FactionEntry* factionEntry = sFactionStore.LookupEntry(id);" "${name}: faction entry from sFactionStore.LookupEntry")
endforeach()

# 8. [ShimBounds] once at the end of LoadQuestTravelTable, the same in both modes.
string(FIND "${travel_mgr}" "void TravelMgr::LoadQuestTravelTable()" load_at)
string(SUBSTRING "${travel_mgr}" ${load_at} -1 load)
string(FIND "${load}" "\n}\n" load_end)
string(SUBSTRING "${load}" 0 ${load_end} load)
require_count("${load}" "LogShimBounds\\(guidpMap\\)" 1 "one [ShimBounds] report per boot")
foreach(type creature go faction quest_loot)
  require_text("${travel_mgr}" "\"[ShimBounds] mode=%u type=${type} " "[ShimBounds] type=${type}")
endforeach()
require_text("${travel_mgr}" "cap_only_quests=%u cap_only_min_level_le29=%u cap_only_items=%u cap_only_resolved=%u" "quest_loot acceptance fields")
require_text("${travel_mgr}" "\"[ShimBounds] mode=%u type=cap_only quest=%u min_level=%u item=%u items=%u resolved=%u\"" "per-quest list for the dump cross-check")

message(STATUS "shim_id_sets source contract passed")
