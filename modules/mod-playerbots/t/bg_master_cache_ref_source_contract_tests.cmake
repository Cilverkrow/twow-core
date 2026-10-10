# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A09, card 13): behind AiPlayerbot.Perf.BgMasterCacheRef (default 0) the values
# "rpg bg type" (RpgBgTypeValue) and "bg masters" (BgMastersValue) read the global battlemaster cache
# through a const reference with find() instead of deep-copying the nested map (up to five copies per
# "rpg bg type" calculation). Same results and decisions; switch 0 keeps the old copy path verbatim.
# Equivalence of the lookup: t/bg_master_cache_policy_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A09: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A09: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A09: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_length)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A09: ${description}: '${needle}' found ${count} times, expected ${expected}")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A09: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A09: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.h" mgr_h)
read_source("BgMasterCachePolicy.h" policy_h)
read_source("strategy/values/PvpValues.cpp" pvp_cpp)

set(copy_line "std::map<Team, std::map<BattleGroundTypeId, std::list<uint32>>> battleMastersCache = sRandomPlayerbotMgr.getBattleMastersCache();")

# 1. Switch: default off everywhere, documented with 0.
require_text("${config_h}" "bool perfBgMasterCacheRef = false;" "member default off")
require_text("${config_cpp}" "perfBgMasterCacheRef = config.GetBoolDefault(\"AiPlayerbot.Perf.BgMasterCacheRef\", false);" "config read, default false")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.BgMasterCacheRef = 0\n" "documented key with value 0")
forbid_text("${conf_dist}" "AiPlayerbot.Perf.BgMasterCacheRef = 1" "shipped as on")

# 2. Getters: the old by-value getter is untouched (switch-off path), the new one is const& and const.
require_text("${mgr_h}" "std::map<Team, std::map<BattleGroundTypeId, std::list<uint32> > > getBattleMastersCache() { return BattleMastersCache; }" "old by-value getter unchanged")
require_text("${mgr_h}" "const std::map<Team, std::map<BattleGroundTypeId, std::list<uint32> > >& getBattleMastersCacheRef() const { return BattleMastersCache; }" "const reference getter")

# 3. Lookup helper: find() only, no operator[]/at(), no static state.
region("${policy_h}" "FindEntries(Cache const& cache," "bool Contains(" find_body)
require_text("${find_body}" "cache.find(team)" "team lookup by find")
require_text("${find_body}" "teamIt->second.find(bgType)" "bg type lookup by find")
require_text("${find_body}" "return nullptr;" "absent key -> no entries")
forbid_text("${find_body}" "[" "operator[] on the shared cache")
forbid_text("${find_body}" ".at(" "at() (throws on absent key)")
forbid_text("${policy_h}" "static" "static state in the lookup helper")
forbid_text("${policy_h}" "mutex" "locks in the lookup helper")

# 4. PvpValues.cpp: helper included, no static caches, no shared_mutex.
require_text("${pvp_cpp}" "#include \"playerbot/BgMasterCachePolicy.h\"" "helper include")
forbid_text("${pvp_cpp}" "static std::" "static cache")
forbid_text("${pvp_cpp}" "shared_mutex" "shared_mutex")

# 5. BgMastersValue: gated; on-path same team order, no copy, no operator[]; off-path verbatim.
region("${pvp_cpp}" "std::list<CreatureDataPair const*> BgMastersValue::Calculate()" "return bmGuids;" masters_body)
region("${masters_body}" "if (sPlayerbotAIConfig.perfBgMasterCacheRef)" "${copy_line}" masters_on)
require_text("${masters_on}" "sRandomPlayerbotMgr.getBattleMastersCacheRef();" "reference in bg masters")
require_order("${masters_on}" "FindEntries(battleMastersCacheRef, TEAM_BOTH_ALLOWED, bgTypeId)" "FindEntries(battleMastersCacheRef, ALLIANCE, bgTypeId)" "neutral before alliance")
require_order("${masters_on}" "FindEntries(battleMastersCacheRef, ALLIANCE, bgTypeId)" "FindEntries(battleMastersCacheRef, HORDE, bgTypeId)" "alliance before horde")
require_text("${masters_on}" "ai::bgmaster::Append(entries, ai::bgmaster::FindEntries(battleMastersCacheRef, TEAM_BOTH_ALLOWED, bgTypeId));" "neutral entries appended")
require_text("${masters_on}" "ai::bgmaster::Append(entries, ai::bgmaster::FindEntries(battleMastersCacheRef, ALLIANCE, bgTypeId));" "alliance entries appended")
require_text("${masters_on}" "ai::bgmaster::Append(entries, ai::bgmaster::FindEntries(battleMastersCacheRef, HORDE, bgTypeId));" "horde entries appended")
count_text("${masters_on}" "ai::bgmaster::Append(" 3 "exactly three appends in bg masters")
forbid_text("${masters_on}" "[" "operator[] on the reference")
forbid_text("${masters_on}" "getBattleMastersCache()" "copy on the on-path")
require_order("${masters_body}" "if (sPlayerbotAIConfig.perfBgMasterCacheRef)" "else" "on-path, then else")
region("${masters_body}" "else" "std::list<CreatureDataPair const*> bmGuids;" masters_off)
require_text("${masters_off}" "${copy_line}" "old copy kept on the off-path")
require_text("${masters_off}" "entries.insert(entries.end(), battleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId].begin(), battleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId].end());" "old neutral insert")
require_text("${masters_off}" "entries.insert(entries.end(), battleMastersCache[ALLIANCE][bgTypeId].begin(), battleMastersCache[ALLIANCE][bgTypeId].end());" "old alliance insert")
require_text("${masters_off}" "entries.insert(entries.end(), battleMastersCache[HORDE][bgTypeId].begin(), battleMastersCache[HORDE][bgTypeId].end());" "old horde insert")

# 6. RpgBgTypeValue: switch read once before the loop; on-block after every filter, same order,
#    'continue' so the copy is skipped; off-path copy and loops verbatim.
region("${pvp_cpp}" "BattleGroundTypeId RpgBgTypeValue::Calculate()" "Unit* FlagCarrierValue::Calculate()" rpg_body)
require_text("${rpg_body}" "sPlayerbotAIConfig.perfBgMasterCacheRef ? &sRandomPlayerbotMgr.getBattleMastersCacheRef() : nullptr;" "reference taken once, gated")
require_order("${rpg_body}" "if (!bot->HasFreeBattleGroundQueueId())" "sPlayerbotAIConfig.perfBgMasterCacheRef ?" "old early returns first")
require_order("${rpg_body}" "sPlayerbotAIConfig.perfBgMasterCacheRef ?" "for (uint32 i = 1; i < MAX_BATTLEGROUND_QUEUE_TYPES; i++)" "reference before the loop")
require_order("${rpg_body}" "if (bot->GetLevel() < bg->GetMinLevel())" "if (battleMastersCacheRef)" "level filter before the on-block")
require_order("${rpg_body}" "if (bot->InBattleGroundQueueForBattleGroundQueueType(queueTypeId))" "if (battleMastersCacheRef)" "queue filter before the on-block")
region("${rpg_body}" "if (battleMastersCacheRef)" "${copy_line}" rpg_on)
require_order("${rpg_on}" "FindEntries(*battleMastersCacheRef, TEAM_BOTH_ALLOWED, bgTypeId), guidPosition.GetEntry())" "FindEntries(*battleMastersCacheRef, bot->GetTeam(), bgTypeId), guidPosition.GetEntry())" "neutral before own team")
# Each match is directly followed by its own return (review: a 'break;' after the own-team match
# would still have passed a plain "return somewhere" check), then 'continue;' skips the copy.
set(rpg_on_neutral "if (ai::bgmaster::Contains(ai::bgmaster::FindEntries(*battleMastersCacheRef, TEAM_BOTH_ALLOWED, bgTypeId), guidPosition.GetEntry()))\n                    return bgTypeId;")
set(rpg_on_team "if (ai::bgmaster::Contains(ai::bgmaster::FindEntries(*battleMastersCacheRef, bot->GetTeam(), bgTypeId), guidPosition.GetEntry()))\n                    return bgTypeId;")
require_text("${rpg_on}" "${rpg_on_neutral}" "neutral match returns the bg type")
require_text("${rpg_on}" "${rpg_on_team}" "own-team match returns the bg type")
require_order("${rpg_on}" "${rpg_on_team}" "continue;" "own-team match, then continue")
count_text("${rpg_on}" "return bgTypeId;" 2 "exactly two returns in the on-block")
count_text("${rpg_on}" "continue;" 1 "exactly one continue in the on-block")
forbid_text("${rpg_on}" "break;" "break in the on-block")
forbid_text("${rpg_on}" "[" "operator[] on the reference")
forbid_text("${rpg_on}" "getBattleMastersCache()" "copy on the on-path")
require_text("${rpg_body}" "${copy_line}" "old copy kept on the off-path")
require_text("${rpg_body}" "for (auto& entry : battleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId])\n                if (entry == guidPosition.GetEntry())\n                    return bgTypeId;" "old neutral loop")
require_text("${rpg_body}" "for (auto& entry : battleMastersCache[bot->GetTeam()][bgTypeId])\n                if (entry == guidPosition.GetEntry())\n                    return bgTypeId;" "old team loop")
require_order("${rpg_body}" "if (battleMastersCacheRef)" "${copy_line}" "on-block before the old copy")
count_text("${rpg_body}" "getBattleMastersCache()" 1 "only the off-path copy in rpg bg type")
count_text("${masters_body}" "getBattleMastersCache()" 1 "only the off-path copy in bg masters")

message(STATUS "bg_master_cache_ref source contract passed")
