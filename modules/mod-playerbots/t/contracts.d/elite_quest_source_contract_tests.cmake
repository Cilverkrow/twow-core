cmake_policy(VERSION 3.16)
# WS30-ELITE-QUEST-01 (AiPlayerbot.QuestElite.Defer, default 0; OB-20 rule f6ab4497, OB-10 review 8b4ddc3c, design CLI-Deep v0.4
# a3/elite-quest/DESIGN-ELITE-QUEST-DEFER-20261128.md): elite quests are pursued only under rule 1a / 1b. Pins the keys (cohort
# mode, clamps, error line, echo, dist, default 0), the load-time scope table (A5: once, world thread, key-gated, published,
# no rebuild), the scope rank set {1, 2, 3} apart from the old `Rank > CREATURE_ELITE_NORMAL` veto (rank 4 keeps it), the travel
# gate E1 (Pursue AND the old readiness, OB-10 A1; focus bypass), E2 (its own value, subtracted only at the target choice and
# the QuestFirstOrder destination test; "needed quest entries" unchanged for its other readers) and the minute line E5.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "elite quest: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "elite quest: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "elite quest: forbidden ${description}: ${needle}")
  endif()
endfunction()

# needle by variable name: a ";" or a list separator in it must not split the argument.
function(count_text text needle_var expected description)
  set(needle "${${needle_var}}")
  string(LENGTH "${needle}" needle_len)
  set(rest "${text}")
  set(count 0)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "elite quest: ${description}: ${count} times, expected ${expected}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "elite quest: order ${description}: ${first} before ${second}")
  endif()
endfunction()

read_source("EliteQuestPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_mgr)
read_source("RandomPlayerbotMgr.cpp" rpm)
read_source("strategy/values/QuestValues.h" qv_h)
read_source("strategy/values/QuestValues.cpp" qv_cpp)
read_source("strategy/values/ValueContext.h" value_context)
read_source("strategy/values/GrindTargetValue.cpp" grind_target)

# 1) The keys: a cohort mode, default 0, clamps, one error line, echo (sorted), dist.
require_text("${config_h}" "    uint32 questEliteDefer = 0;" "Defer member, default 0")
require_text("${config_h}" "    uint32 questEliteMinGroup = 3;" "MinGroup member, default 3")
require_text("${config_h}" "    uint32 questEliteSoloLevelsAbove = 5;" "SoloLevelsAbove member, default 5")
require_text("${config_cpp}" "    questEliteDefer = uint32(config.GetIntDefault(\"AiPlayerbot.QuestElite.Defer\", 0));\n    if (!ai::elite_quest::ValidMode(questEliteDefer))\n    {" "loader with the mode check")
require_text("${config_cpp}" "[QuestElite] config key=QuestElite.Defer value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)" "error line")
require_text("${config_cpp}" "        questEliteDefer = 0;\n    }" "an invalid mode is off")
require_text("${config_cpp}" "ai::elite_quest::ClampMinGroup(uint32(std::max(0, config.GetIntDefault(\"AiPlayerbot.QuestElite.MinGroup\", 3))))" "MinGroup clamp")
require_text("${config_cpp}" "ai::elite_quest::ClampSoloLevelsAbove(uint32(std::max(0, config.GetIntDefault(\"AiPlayerbot.QuestElite.SoloLevelsAbove\", 5))))" "SoloLevelsAbove clamp")
require_text("${config_cpp}" "            {\"QuestBook.StaleCompleteSeconds\", Value(questBookStaleCompleteSeconds)},\n            {\"QuestElite.Defer\", Value(questEliteDefer)},\n            {\"QuestElite.MinGroup\", Value(questEliteMinGroup)},\n            {\"QuestElite.SoloLevelsAbove\", Value(questEliteSoloLevelsAbove)},\n            {\"QuestFeasibility.Blocklist.count\"" "ConfigEcho entries (sorted)")
require_text("${conf_dist}" "# groups). A flip to 1 only by a frozen ADOPT or an owner OK.\nAiPlayerbot.QuestElite.Defer = 0\n" "dist Defer, default 0")
require_text("${conf_dist}" "\nAiPlayerbot.QuestElite.MinGroup = 3\n" "dist MinGroup 3")
require_text("${conf_dist}" "\nAiPlayerbot.QuestElite.SoloLevelsAbove = 5\n" "dist SoloLevelsAbove 5")
require_text("${policy}" "return mode <= cohort::ModeOnlyA;" "modes 0..3")
require_text("${policy}" "return rosterOnItsOwn && cohort::Applies(ValidMode(mode) ? mode : cohort::ModeOff, guidLow);" "population via the shared cohort rule")

# 2) The scope rank set {1, 2, 3} (OB-20 f6ab4497) - its own test, never the old `Rank > CREATURE_ELITE_NORMAL` (which also takes 4).
require_text("${policy}" "    return rank == 1 || rank == 2 || rank == 3;" "scope rank set 1 / 2 / 3")
require_text("${policy}" "    if (questType == QuestTypeDungeon || questType == QuestTypeRaid)\n        return false;\n    return questType == QuestTypeElite || eliteKillObjective;" "scope: type 1 or an elite kill objective, never dungeon / raid")
set(n_old_rank "Rank > CREATURE_ELITE_NORMAL")
count_text("${policy}" n_old_rank 0 "the old rank test in the policy")
count_text("${ai_cpp}" n_old_rank 0 "the old rank test in the table build")

# 3) The table (A5): built once on the world thread at start, only with the key on, published (release / acquire), no rebuild.
require_text("${config_cpp}" "    if (sPlayerbotAIConfig.questEliteDefer)\n        PlayerbotAI::BuildEliteQuestScope();" "built only with the key on")
require_order("${config_cpp}" "        PlayerbotAI::BuildCraftOnlyQuests();" "        PlayerbotAI::BuildEliteQuestScope();" "built in Initialize next to the CraftOnly data")
require_text("${ai_cpp}" "    if (eliteQuestScope.load(std::memory_order_acquire))\n    {\n        sLog.outBasic(\"[QuestElite] init skipped: already built (no rebuild on reload)\");\n        return;\n    }" "no rebuild on reload")
require_text("${ai_cpp}" "            if (!target || !ai::elite_quest::EliteRank(target->Rank))\n                continue;" "table uses the scope rank set")
require_text("${ai_cpp}" "        e.level = ai::elite_quest::EffectiveLevel(quest->GetQuestLevel(), highest);" "effective level per quest")
require_text("${ai_cpp}" "    ai::elite_quest::Seal(*built);" "sealed before publishing")
require_order("${ai_cpp}" "    ai::elite_quest::Seal(*built);" "    eliteQuestScope.store(built.release(), std::memory_order_release);" "seal before the release store")
require_text("${ai_cpp}" "    std::vector<ai::elite_quest::ScopeEntry> const* const scope = eliteQuestScope.load(std::memory_order_acquire);" "readers load with acquire")
set(n_store "eliteQuestScope.store(")
count_text("${ai_cpp}" n_store 1 "one publish")

# 4) The predicate and the group facts: config first; members only on the bot's own Map object (HARM-RACE guard pair).
require_text("${ai_cpp}" "bool PlayerbotAI::IsEliteQuestDefer()\n{\n    if (!sPlayerbotAIConfig.questEliteDefer || !bot)\n        return false;" "config read first")
require_text("${ai_cpp}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !HasRealPlayerMaster());" "roster bot on its own only")
require_text("${ai_cpp}" "        if (!member || member == bot || !member->IsInWorld() || !SameMapObject(bot, member) || !member->IsAlive() ||\n            member->GetZoneId() != bot->GetZoneId())" "group facts guarded")
require_text("${ai_cpp}" "    bool const ready = members > 1 ? AI_VALUE(bool, \"can fight elite\") : AI_VALUE(bool, \"can fight equal\");" "deferral keeps the readiness (A1)")
require_text("${travel_mgr}" "    eliteRule = ai->IsEliteQuestDefer();\n    if (eliteRule)\n    {\n        eliteGuid = player->GetGUIDLow();\n        ai->EliteGroupFacts(eliteMembers, eliteMinLevel);\n    }" "snapshot on the bot thread, one group-facts implementation")
require_text("${travel_h}" "		bool eliteRule = false;" "snapshot bit default false")

# 5) E1, the travel gate: before the old tests, focus bypass, outside instances only, Pursue AND readiness, counted.
require_text("${travel_mgr}" "        (!forceThisQuest && info.HasEliteRule() && IsOverWorld(info.GetPosition())) ? PlayerbotAI::EliteQuestScope(GetQuestId()) : nullptr;" "scope only for a judged bot, no focus quest, outside instances")
require_text("${travel_mgr}" "        bool const ready = info.GetEliteMembers() > 1 ? info.GetBoolValue(\"can fight elite\") : info.GetBoolValue(\"can fight equal\");\n        if (!ready || !ai::elite_quest::Pursue(" "E1: Pursue AND the old readiness (A1)")
require_text("${travel_mgr}" "            PlayerbotAI::NoteEliteQuest(uint8(ai::elite_quest::Event::TravelDeferred), info.GetEliteGuid());\n            return false;" "deferred destination counted and refused")
require_order("${travel_mgr}" "PlayerbotAI::EliteQuestScope(GetQuestId())" "        if (info.HasQuestBook())\n        {\n            if (ai::questbook::Excluded(GetQuestId()))" "E1 before the old objective tests")
# The old rank veto stays for every target the scope does not cover (rank 4 with the key on; everything with the key off).
require_text("${travel_mgr}" "            if (cInfo->Rank > CREATURE_ELITE_NORMAL && !(eliteScope && ai::elite_quest::EliteRank(cInfo->Rank)))" "rank 4 keeps the old veto")
require_text("${travel_mgr}" "        if (!skipKillableCheck && !eliteScope && GetQuestTemplate()->GetType() == QUEST_TYPE_ELITE && !info.GetBoolValue(\"can fight elite\"))" "type 1 veto only outside the scope")
require_text("${travel_mgr}" "            if (cInfo && (int)cInfo->MaxLevel - (int)info.GetLevel() > 4)\n                return false;" "MaxLevel - level > 4 rule unchanged (OB-10)")
require_text("${travel_mgr}" "    if (eliteScope)\n        PlayerbotAI::NoteEliteQuest(uint8(ai::elite_quest::Event::Pursued), info.GetEliteGuid());\n    return true;" "pursued counted")

# 6) E2: its own value; subtracted only at the grind target choice and the QuestFirstOrder grind destination test.
require_text("${value_context}" "            creators[\"deferred elite entries\"] = [](PlayerbotAI* ai) { return new DeferredEliteEntriesValue(ai); };" "value registered")
require_text("${qv_cpp}" "std::vector<int32> DeferredEliteEntriesValue::Calculate()\n{\n\tstd::vector<int32> deferred;\n\tif (!ai->IsEliteQuestDefer())\n\t\treturn deferred;" "empty for every bot the rule does not judge")
require_text("${qv_cpp}" "\t\t\tif (isDeferred && target && ai::elite_quest::EliteRank(target->Rank))\n\t\t\t\tdeferred.push_back(entry);" "only elite targets of deferred quests")
require_text("${qv_cpp}" "[&keep](int32 entry) { return std::binary_search(keep.begin(), keep.end(), entry); }), deferred.end());" "entries another quest needs stay needed")
set(n_deferred_value "\"deferred elite entries\"")
count_text("${qv_h}" n_deferred_value 1 "value name in QuestValues.h")
count_text("${grind_target}" n_deferred_value 1 "target choice reader")
count_text("${travel_mgr}" n_deferred_value 1 "QuestFirstOrder grind destination reader")
count_text("${ai_cpp}" n_deferred_value 0 "no reader in PlayerbotAI.cpp")
require_text("${grind_target}" "            if (needForQuest && sPlayerbotAIConfig.questEliteDefer)\n            {" "target choice reads it only with the key on")
require_text("${travel_mgr}" "        if (!ai::quest_grind_mode::GrindDestinationActive(true, neededHere))" "QFO destination test with the subtraction")
# "needed quest entries" keeps its full set for its other readers: its Calculate never reads the deferral.
string(FIND "${qv_cpp}" "std::vector<int32> NeededQuestEntriesValue::Calculate()" needed_at)
string(SUBSTRING "${qv_cpp}" ${needed_at} 6000 needed_body)
forbid_text("${needed_body}" "EliteQuestDeferred" "deferral inside needed quest entries")
forbid_text("${needed_body}" "deferred elite entries" "deferred value inside needed quest entries")

# 7) E5, the minute line: once a minute only with the switch, never empty, fixed format.
require_text("${rpm}" "void ReportQuestElite()\n{\n    if (!sPlayerbotAIConfig.questEliteDefer)\n        return;" "minute line gated by the switch")
require_text("${rpm}" "    std::string const line = PlayerbotAI::TakeEliteQuestMinuteLine();\n    if (!line.empty())" "no empty line")
require_text("${rpm}" "    ReportLowLevelWindow();  // OB-10 22.11.: [LowLevelWindow] minute once per minute, not when empty (Grind.LowLevelWindow > 0)\n    ReportQuestElite();" "reported from the world tick")
require_text("${policy}" "    return \"[QuestElite] minute\" + pair(Event::TravelDeferred, \"travel_deferred\") + pair(Event::TargetSkipped, \"target_skipped\") +\n        pair(Event::WishSkipped, \"wish_skipped\") + pair(Event::FormRefused, \"form_refused\") + pair(Event::Pursued, \"pursued\");" "line format")
require_text("${ai_h}" "    static std::string TakeEliteQuestMinuteLine();" "minute line taker declared")

message(STATUS "ELITE_QUEST_SOURCE_CONTRACT=PASS")
