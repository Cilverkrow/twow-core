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
read_source("strategy/actions/RewardAction.cpp" reward_action)
read_source("strategy/actions/TalkToQuestGiverAction.cpp" talk_action)

# 1) The keys: a cohort mode, default 0, clamps, one error line, echo (sorted), dist.
require_text("${config_h}" "    uint32 questEliteDefer = 0;" "Defer member, default 0")
require_text("${config_h}" "    uint32 questEliteMinGroup = 3;" "MinGroup member, default 3")
require_text("${config_h}" "    uint32 questEliteSoloLevelsAbove = 5;" "SoloLevelsAbove member, default 5")
require_text("${config_cpp}" "    questEliteDefer = uint32(config.GetIntDefault(\"AiPlayerbot.QuestElite.Defer\", 0));\n    if (!ai::elite_quest::ValidMode(questEliteDefer))\n    {" "loader with the mode check")
require_text("${config_cpp}" "[QuestElite] config key=QuestElite.Defer value=%d result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", int32(questEliteDefer));\n        questEliteDefer = 0;" "error line with the raw value, an invalid mode is off")
# Out of range: one error line and the default, never the nearest (most permissive) bound (ultracode review 28.11.).
require_text("${config_cpp}" "    if (!ai::elite_quest::ValidMinGroup(int32(questEliteMinGroup)))\n    {\n        sLog.outError(\"[QuestElite] config key=QuestElite.MinGroup value=%d result=%u (valid 2..5)\"" "MinGroup check and error line")
require_text("${config_cpp}" "        questEliteMinGroup = ai::elite_quest::DefaultMinGroup;" "MinGroup default fallback")
require_text("${config_cpp}" "    if (!ai::elite_quest::ValidSoloLevelsAbove(int32(questEliteSoloLevelsAbove)))\n    {\n        sLog.outError(\"[QuestElite] config key=QuestElite.SoloLevelsAbove value=%d result=%u (valid 0..99, 99 = rule 1b off)\"" "SoloLevelsAbove check and error line")
require_text("${config_cpp}" "        questEliteSoloLevelsAbove = ai::elite_quest::DefaultSoloLevelsAbove;" "SoloLevelsAbove default fallback")
set(n_clamp_load "ClampMinGroup(uint32(std::max(0")
count_text("${config_cpp}" n_clamp_load 0 "no silent clamp at load")
require_text("${policy}" "    return raw >= std::int32_t(MinGroupLow) && raw <= std::int32_t(MinGroupHigh);" "MinGroup range 2..5")
require_text("${policy}" "    return raw >= 0 && raw <= std::int32_t(SoloLevelsAboveHigh);" "SoloLevelsAbove range 0..99")
require_text("${config_cpp}" "            {\"QuestBook.StaleCompleteSeconds\", Value(questBookStaleCompleteSeconds)},\n            {\"QuestElite.Defer\", Value(questEliteDefer)},\n            {\"QuestElite.MinGroup\", Value(questEliteMinGroup)},\n            {\"QuestElite.SoloLevelsAbove\", Value(questEliteSoloLevelsAbove)},\n            {\"QuestFeasibility.Blocklist.count\"" "ConfigEcho entries (sorted)")
require_text("${conf_dist}" "# groups before the form floor is in (an A/B would judge the solo bots only). A flip to 1 only by a frozen ADOPT or an owner OK.\nAiPlayerbot.QuestElite.Defer = 0\n" "dist Defer, default 0, no A/B with manager / S7 groups before E4")
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
require_text("${ai_cpp}" "    focusQuestTravelList const focus = AI_VALUE(focusQuestTravelList, \"focus travel target\");\n    if (focus.find(questId) != focus.end())\n        return false;" "E2 mirrors the E1 focus bypass")
require_text("${ai_cpp}" "    Map const* const map = bot->FindMap();\n    if (!EliteRuleJudges() || !map || map->Instanceable())\n        return false;" "deferral: judged bot outside instances only (FindMap, #722)")
# A manager (T02-GROUP goal) or S7 formed group is judged at form time (E4), never by E1 / E2 mid-work (ultracode review 28.11.,
# OB-00 OK): no idle formed group before E4, no camp dropped on a member death.
require_text("${ai_cpp}" "bool PlayerbotAI::EliteFormedGroup()\n{\n    return ai::supervisor::GroupGoalSnapshot(this) != 0 || GetQuestGroupRole() != 0;" "formed group = group goal or S7 role")
require_text("${ai_cpp}" "bool PlayerbotAI::EliteRuleJudges()\n{\n    return IsEliteQuestDefer() && !EliteFormedGroup();\n}" "judged = population and not a formed group")
require_text("${travel_mgr}" "    eliteRule = ai->EliteRuleJudges();" "E1 snapshot uses the judged predicate")
require_text("${travel_mgr}" "    if (eliteRule)\n    {\n        eliteGuid = player->GetGUIDLow();\n        ai->EliteGroupFacts(eliteMembers, eliteMinLevel);\n    }" "snapshot on the bot thread, one group-facts implementation")
set(n_defer_pred "IsEliteQuestDefer()")
count_text("${travel_mgr}" n_defer_pred 0 "E1 never asks the bare population predicate")
count_text("${qv_cpp}" n_defer_pred 0 "E2 never asks the bare population predicate")
count_text("${grind_target}" n_defer_pred 0 "the target choice never asks the bare population predicate")
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
require_text("${value_context}" "            creators[\"pursued elite entries\"] = [](PlayerbotAI* ai) { return new PursuedEliteEntriesValue(ai); };" "pursued value registered")
require_text("${qv_cpp}" "std::vector<int32> DeferredEliteEntriesValue::Calculate()\n{\n\tstd::vector<int32> deferred;\n\tif (!ai->EliteRuleJudges())\n\t\treturn deferred;" "deferred: empty for every bot the rule does not judge")
require_text("${qv_cpp}" "std::vector<int32> PursuedEliteEntriesValue::Calculate()\n{\n\tstd::vector<int32> pursued;\n\tif (!ai->EliteRuleJudges())\n\t\treturn pursued;" "pursued: empty for every bot the rule does not judge")
# Both sets from the open objectives' quest destinations (kill targets and item droppers), like "needed quest entries".
require_text("${qv_cpp}" "(quest->ReqItemCount[objective] && status.m_itemcount[objective] < quest->ReqItemCount[objective]))" "open item objectives count")
require_text("${qv_cpp}" "\t\t\tfor (auto destination : sTravelMgr.GetDestinations(info, openMask, {int32(questId)}, false, 0))" "sets from the quest destinations")
require_text("${qv_cpp}" "\t\t\t\tif (isDeferred)\n\t\t\t\t{\n\t\t\t\t\tif (elite)\n\t\t\t\t\t\tdeferred.push_back(entry);\n\t\t\t\t}\n\t\t\t\telse\n\t\t\t\t{\n\t\t\t\t\tkeep.push_back(entry);\n\t\t\t\t\tif (inScope && elite)\n\t\t\t\t\t\tpursued.push_back(entry);" "deferred elite / keep / pursued split")
require_text("${qv_cpp}" "\tai::elite_quest::FinishEntrySets(deferred, pursued, keep);" "set algebra in the policy (unit-tested)")
require_text("${policy}" "[&keep](std::int32_t entry) { return std::binary_search(keep.begin(), keep.end(), entry); }), deferredElite.end());" "entries another quest needs stay needed")
set(n_deferred_value "\"deferred elite entries\"")
set(n_pursued_value "\"pursued elite entries\"")
count_text("${qv_h}" n_deferred_value 1 "value name in QuestValues.h")
count_text("${grind_target}" n_deferred_value 1 "target choice reader")
count_text("${travel_mgr}" n_deferred_value 1 "QuestFirstOrder grind destination reader")
count_text("${ai_cpp}" n_deferred_value 0 "no reader in PlayerbotAI.cpp")
count_text("${grind_target}" n_pursued_value 1 "pursued: one reader (target choice)")
count_text("${travel_mgr}" n_pursued_value 0 "pursued: no travel reader")
require_text("${grind_target}" "    if (sPlayerbotAIConfig.questEliteDefer && ai->EliteRuleJudges())\n    {\n        deferredElite = AI_VALUE(std::vector<int32>, \"deferred elite entries\");\n        pursuedElite = AI_VALUE(std::vector<int32>, \"pursued elite entries\");\n    }" "target choice reads both once per scan, key and judged only")
require_text("${grind_target}" "            if (needForQuest && !deferredElite.empty())\n            {" "E2 subtraction with the hoisted set")
# Rule 1b on site: a pursued elite target passes the old group-only "can fight elite" skip; everything else keeps it.
require_text("${grind_target}" "creature->GetCreatureInfo()->Rank > CREATURE_ELITE_NORMAL && !AI_VALUE(bool, \"can fight elite\") &&\n            !std::binary_search(pursuedElite.begin(), pursuedElite.end(), int32(creature->GetEntry())) &&" "1b target gate")
# the GrindMode 2 line stays byte-exact (quest_grind_mode contract); the elite test is its own block after it, key-gated
require_text("${travel_mgr}" "        if (!ai::quest_grind_mode::GrindDestinationActive(true, std::binary_search(needed.begin(), needed.end(), GetEntry())))
            return false;
        // WS30-ELITE-QUEST-01" "QFO destination: the pinned line, then the elite test")
require_text("${travel_mgr}" "        if (sPlayerbotAIConfig.questEliteDefer)
        {
            std::vector<int32> const& deferredElite = AI_VALUE(std::vector<int32>, \"deferred elite entries\");
            if (std::binary_search(deferredElite.begin(), deferredElite.end(), GetEntry()))
                return false;
        }" "QFO destination reads it only with the key on")
require_text("${grind_target}" "                if (std::binary_search(deferredElite.begin(), deferredElite.end(), int32(entry)))
                {
                    needForQuest = false;
                    PlayerbotAI::NoteEliteQuest(uint8(ai::elite_quest::Event::TargetSkipped), bot->GetGUIDLow());" "target_skipped counted at the skip")
set(n_skipped "Event::TargetSkipped")
count_text("${grind_target}" n_skipped 1 "target_skipped: one count site (target choice)")
count_text("${qv_cpp}" n_skipped 0 "no target_skipped count in the value rebuild")
# "needed quest entries" keeps its full set for its other readers: its Calculate never reads the deferral.
string(FIND "${qv_cpp}" "std::vector<int32> NeededQuestEntriesValue::Calculate()" needed_at)
string(SUBSTRING "${qv_cpp}" ${needed_at} -1 needed_rest)
string(FIND "${needed_rest}" "
}
" needed_end)   # the function ends at its first closing brace in column 0
if(needed_at EQUAL -1 OR needed_end EQUAL -1)
  message(FATAL_ERROR "elite quest: NeededQuestEntriesValue::Calculate not found")
endif()
string(SUBSTRING "${needed_rest}" 0 ${needed_end} needed_body)
forbid_text("${needed_body}" "EliteQuestDeferred" "deferral inside needed quest entries")
forbid_text("${needed_body}" "deferred elite entries" "deferred value inside needed quest entries")

# 7) E5, the minute line: once a minute only with the switch, never empty, fixed format.
require_text("${rpm}" "void ReportQuestElite()\n{\n    if (!sPlayerbotAIConfig.questEliteDefer)\n        return;" "minute line gated by the switch")
require_text("${rpm}" "    std::string const line = PlayerbotAI::TakeEliteQuestMinuteLine();\n    if (!line.empty())" "no empty line")
require_text("${rpm}" "    ReportLowLevelWindow();  // OB-10 22.11.: [LowLevelWindow] minute once per minute, not when empty (Grind.LowLevelWindow > 0)\n    ReportQuestElite();" "reported from the world tick")
require_text("${policy}" "    return \"[QuestElite] minute\" + pair(Event::TravelDeferred, \"travel_deferred\") + pair(Event::TargetSkipped, \"target_skipped\") +\n        pair(Event::WishSkipped, \"wish_skipped\") + pair(Event::FormRefused, \"form_refused\") + pair(Event::Pursued, \"pursued\");" "line format")
require_text("${ai_h}" "    static std::string TakeEliteQuestMinuteLine();" "minute line taker declared")


# 8) The reward line (OB-00 28.11.: accepted for the PREREG): both cohorts, roster bots on their own, scope quests, key on.
# Both reward sources (the a3s3 / questbook contracts pin NoteQuestRewarded byte-exact, so the line has its own calls): the
# announce = false sources right after their NoteQuestRewarded, the packet source (announce = true) in the
# SMSG_QUESTGIVER_QUEST_COMPLETE handler after the quest book's block, key-gated.
require_text("${reward_action}" "                    ai->NoteQuestRewarded(questID);   // twow-repo#724: announce = false sends no reward packet\n                    ai->NoteEliteQuestReward(questID);" "RewardAction source")
require_text("${talk_action}" "        ai->NoteQuestRewarded(quest->GetQuestId());   // twow-repo#724: announce = false sends no reward packet\n        ai->NoteEliteQuestReward(quest->GetQuestId());" "RewardNoItem source")
require_text("${ai_cpp}" "			questBook_->OnRewarded(questId);\n		}\n		// WS30-ELITE-QUEST-01 (QuestElite.Defer, default 0): the [QuestElite] reward line of the packet reward source" "packet source after the quest book block")
require_text("${ai_cpp}" "		if (sPlayerbotAIConfig.questEliteDefer && packet.size() >= 4)\n		{\n			WorldPacket p(packet);\n			p.rpos(0);\n			uint32 eliteQuestId;\n			p >> eliteQuestId;\n			NoteEliteQuestReward(eliteQuestId);\n		}\n		return;" "packet source key-gated")
set(n_reward_call "NoteEliteQuestReward(")
count_text("${ai_cpp}" n_reward_call 2 "reward line: definition + packet source in PlayerbotAI.cpp")
count_text("${reward_action}" n_reward_call 1 "reward line: RewardAction")
count_text("${talk_action}" n_reward_call 1 "reward line: RewardNoItem")
require_text("${ai_cpp}" "void PlayerbotAI::NoteEliteQuestReward(uint32 questId)\n{\n    if (!sPlayerbotAIConfig.questEliteDefer || !bot || !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) ||\n        HasRealPlayerMaster())\n        return;" "reward line: config first, roster bots on their own, no cohort filter")
require_text("${ai_cpp}" "    facts.judged = EliteRuleJudges();\n    facts.formed = EliteFormedGroup();" "judged / formed fields")
require_text("${policy}" "    return \"[QuestElite] reward guid=\" + std::to_string(f.guid) + \" cohort=\" + (cohort::InB(f.guid) ? \"b\" : \"a\") +" "reward line format")

message(STATUS "ELITE_QUEST_SOURCE_CONTRACT=PASS")
