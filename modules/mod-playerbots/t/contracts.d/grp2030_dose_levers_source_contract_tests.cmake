# twow-repo#627 GRP2030 dose levers 1 and 2 of the group manager (OB-00 (A) 27.11.2026; default 0 = no behaviour change):
# AiPlayerbot.QuestGroups.Manager.WishScope (a normal quest may be a bot's manager group wish) and
# AiPlayerbot.QuestGroups.Manager.DeadQuestEnds / DeadQuestHours (a quest whose QUEST groups end N times in a row without any
# progress is dead for H hours: no candidate, the cohort's bots wish another quest).
# This contract pins the keys (dist, member, read, [ConfigEcho] sorted), the cohort binding of both levers in ReadWish
# (group_mgr::OwnsBot, the other cohort keeps the old wish), the old wish rule kept for scope 0 (BetterWish), the dead-quest
# store in the engine (counted only for QUEST groups, published to the bots' view, also checked in QuestBlocked), the line
# format and the unit test.
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1; AiPlayerbot.QuestGroups.Manager.WishSkipBlocked, default 0): the key (dist,
# member, read, [ConfigEcho] sorted), the skip in ReadWish (owned only, right after the dead skip), the blocked pairs in the
# view (own atomic, sorted, no lock while off), the hold in the queue (ForgetStale keeps a blocked entry and one with attempts
# up to the DeadQuestHours horizon after its last failure, Failed gives an exhausted one the DeadQuestHours horizon; the
# default hold is the old code path), the minute line's wish_blocked field.

cmake_policy(VERSION 3.16)

set(TAG "GRP2030 dose levers")

if(NOT DEFINED PB_SOURCE_DIR OR NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "${TAG}: PB_SOURCE_DIR and PB_MODULE_DIR are required")
endif()

function(read_source path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "${TAG}: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  set(rest "${text}")
  set(n 0)
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR cut "${at} + ${len}")
    string(SUBSTRING "${rest}" ${cut} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "${TAG}: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

function(region text start end out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "${TAG}: region start not found: ${start}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "${TAG}: region end not found after: ${start}")
  endif()
  string(LENGTH "${end}" elen)
  math(EXPR blen "${b} + ${elen}")
  string(SUBSTRING "${tail}" 0 ${blen} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/ManagerFacts.cpp" facts_cpp)
read_source("${PB_SOURCE_DIR}/QuestGroupPolicy.h" qg_policy)
read_source("${PB_SOURCE_DIR}/GroupDeadQuestPolicy.h" dead_policy)
read_source("${PB_SOURCE_DIR}/GroupDirectivePolicy.h" engine)
read_source("${PB_SOURCE_DIR}/GroupManager.cpp" mgr_cpp)
read_source("${PB_SOURCE_DIR}/GroupManagerView.h" view_h)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)

# 1. The keys: default 0 / 0 / 6, one read each, [ConfigEcho] sorted right after QuestGroups.Enable.
require_text("${dist}" "\nAiPlayerbot.QuestGroups.Manager.WishScope = 0\nAiPlayerbot.QuestGroups.Manager.DeadQuestEnds = 0\nAiPlayerbot.QuestGroups.Manager.DeadQuestHours = 6\n" "dist defaults (0, 0, 6)")
require_text("${config_h}" "    uint32 questGroupsManagerWishScope = 0;\n    uint32 questGroupsManagerDeadQuestEnds = 0;\n    uint32 questGroupsManagerDeadQuestHours = 6;" "member defaults")
require_text("${config_cpp}" "    questGroupsManagerWishScope = uint32(config.GetIntDefault(\"AiPlayerbot.QuestGroups.Manager.WishScope\", 0));" "WishScope read with 0")
require_text("${config_cpp}" "    questGroupsManagerDeadQuestEnds = uint32(config.GetIntDefault(\"AiPlayerbot.QuestGroups.Manager.DeadQuestEnds\", 0));" "DeadQuestEnds read with 0")
require_text("${config_cpp}" "    questGroupsManagerDeadQuestHours = uint32(config.GetIntDefault(\"AiPlayerbot.QuestGroups.Manager.DeadQuestHours\", int32(ai::dead_quest::kHoursDefault)));" "DeadQuestHours read with the default")
require_text("${config_cpp}" "        questGroupsManagerDeadQuestHours = ai::dead_quest::ClampHours(hoursRaw);" "hours clamp")
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): WishSkipBlocked default 0, 0/1 (other = 0 + error line), after DeadQuestHours.
require_text("${dist}" "\nAiPlayerbot.QuestGroups.Manager.DeadQuestHours = 6\n# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1" "dist: the wish skip block right after the lever 2 keys")
require_text("${dist}" "\nAiPlayerbot.QuestGroups.Manager.WishSkipBlocked = 0\n" "dist default WishSkipBlocked 0")
require_text("${config_h}" "    uint32 questGroupsManagerDeadQuestHours = 6;\n" "member DeadQuestHours")
require_text("${config_h}" "    uint32 questGroupsManagerWishSkipBlocked = 0;" "member default WishSkipBlocked 0")
require_text("${config_cpp}" "    questGroupsManagerWishSkipBlocked = uint32(config.GetIntDefault(\"AiPlayerbot.QuestGroups.Manager.WishSkipBlocked\", 0));\n    if (questGroupsManagerWishSkipBlocked > 1)\n    {" "WishSkipBlocked read with 0, above 1 caught")
require_text("${config_cpp}" "        sLog.outError(\"[GroupMgr] config key=QuestGroups.Manager.WishSkipBlocked value=%d result=0 (0 or 1)\", int32(questGroupsManagerWishSkipBlocked));\n        questGroupsManagerWishSkipBlocked = 0;" "WishSkipBlocked: other values = 0 + one error line")
require_order("${config_cpp}" "        questGroupsManagerDeadQuestHours = ai::dead_quest::ClampHours(hoursRaw);" "    questGroupsManagerWishSkipBlocked = uint32(config.GetIntDefault(" "WishSkipBlocked read after the hours clamp")
foreach(key WishScope DeadQuestEnds DeadQuestHours WishSkipBlocked)
  count_text("${config_cpp}" "\"AiPlayerbot.QuestGroups.Manager.${key}\"" 1 "one read of ${key}")
  count_text("${dist}" "AiPlayerbot.QuestGroups.Manager.${key} =" 1 "one dist line of ${key}")
endforeach()
require_text("${config_cpp}" "            {\"QuestGroups.Enable\", Value(questGroupsEnable)},\n            {\"QuestGroups.Manager.DeadQuestEnds\", Value(questGroupsManagerDeadQuestEnds)},\n            {\"QuestGroups.Manager.DeadQuestHours\", Value(questGroupsManagerDeadQuestHours)},\n            {\"QuestGroups.Manager.WishScope\", Value(questGroupsManagerWishScope)},\n            {\"QuestGroups.Manager.WishSkipBlocked\", Value(questGroupsManagerWishSkipBlocked)},\n            {\"QuestGroups.OneHeavyStep\", Value(questGroupsOneHeavyStep)}," "[ConfigEcho] entries in sorted place")
count_text("${config_cpp}" "{\"QuestGroups.Manager.WishSkipBlocked\"," 1 "one [ConfigEcho] entry of WishSkipBlocked")
require_text("${qg_policy}" "constexpr std::uint32_t QuestTypeElite = 1;" "quest type constants")
require_text("${dead_policy}" "constexpr std::uint32_t kHoursDefault = 6;" "hours default 6")
require_text("${dead_policy}" "constexpr std::uint32_t kHoursMax = 48;" "hours max 48")
# GroupDeadQuestPolicy.h is pure (engine header include, a3s6_t30_directives check_pure allowlist): std headers only, no game names.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" dead_includes "${dead_policy}")
foreach(inc IN LISTS dead_includes)
  if(NOT inc MATCHES "^#include <(cstdint|unordered_map|vector|algorithm)>$")
    message(FATAL_ERROR "${TAG}: GroupDeadQuestPolicy.h has a non-pure include: ${inc}")
  endif()
endforeach()
foreach(word "sPlayerbotAIConfig" "sLog" "Player*" "PlayerbotAI" "time(")
  forbid_text("${dead_policy}" "${word}" "GroupDeadQuestPolicy.h is pure, it names")
endforeach()

# 2. ReadWish: both levers only for a bot of the manager's cohort; the old rule is BetterWish's group-help branch.
region("${facts_cpp}" "bool ReadWish(Player* bot, PlayerbotAI* ai, manager_inbox::WishFacts& out)" "\n}\n" wish_fn)
require_text("${wish_fn}" "    bool const owned = (sPlayerbotAIConfig.questGroupsManagerWishScope || sPlayerbotAIConfig.questGroupsManagerDeadQuestEnds ||\n        sPlayerbotAIConfig.questGroupsManagerWishSkipBlocked) && group_mgr::OwnsBot(bot->GetGUIDLow());" "cohort binding (OwnsBot), keys first (GRP2030 wish skip included)")
require_text("${wish_fn}" "    bool const scopeAll = owned && sPlayerbotAIConfig.questGroupsManagerWishScope;" "scope only when owned")
require_text("${wish_fn}" "    bool const skipDead = owned && sPlayerbotAIConfig.questGroupsManagerDeadQuestEnds;" "dead skip only when owned")
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): only when owned, the bot's own guid, right after the dead skip, before the
# quest status reads.
require_text("${wish_fn}" "    bool const skipBlocked = owned && sPlayerbotAIConfig.questGroupsManagerWishSkipBlocked;" "blocked skip only when owned")
require_text("${wish_fn}" "        if (skipDead && group_mgr::QuestDeadForWish(questId))\n            continue;\n        if (skipBlocked && group_mgr::QuestBlockedForWish(bot->GetGUIDLow(), questId))\n            continue;\n        QuestStatusData const* st = bot->GetQuestStatusData(questId);" "blocked quests skipped right after the dead skip (manager view)")
require_order("${wish_fn}" "group_mgr::QuestDeadForWish(questId)" "group_mgr::QuestBlockedForWish(bot->GetGUIDLow(), questId)" "the blocked skip after the dead skip")
count_text("${wish_fn}" "group_mgr::QuestBlockedForWish(" 1 "one blocked lookup in ReadWish")
require_text("${wish_fn}" "        if (!help && (!scopeAll || q->GetType() == quest_groups::QuestTypeDungeon || q->GetType() == quest_groups::QuestTypeRaid ||\n            q->GetType() == quest_groups::QuestTypePvp))\n            continue;" "scope 0 = group-help only; never dungeon / raid / PvP")
require_text("${wish_fn}" "        if (skipDead && group_mgr::QuestDeadForWish(questId))\n            continue;" "dead quests skipped (manager view)")
require_text("${wish_fn}" "        if (!quest_groups::Waits(f) || !quest_groups::BetterWish(best, help, f.openObjectives, questId))\n            continue;" "Waits and the wish order")
require_order("${wish_fn}" "group_mgr::QuestDeadForWish(questId)" "QuestStatusData const* st = bot->GetQuestStatusData(questId);" "the dead skip before the quest status reads")
region("${qg_policy}" "inline bool BetterWish(WishPick const& best, bool help, std::uint32_t open, std::uint32_t questId)" "\n}\n" better_fn)
require_text("${better_fn}" "    if (!best.any)\n        return true;\n    if (help != best.help)\n        return help;\n    if (help)\n        return open > best.open;\n    return questId < best.questId;" "wish order: help first, old rule for help, lowest id for normal")
# ManagerFacts reads only the bot itself and the manager's view (M6): no other Player.
forbid_text("${facts_cpp}" "ObjectAccessor" "no other player lookups in ManagerFacts")
forbid_text("${facts_cpp}" "sObjectMgr.GetPlayer" "no other player lookups in ManagerFacts")

# 3. The view: a dead list of its own (own atomic, swap under the lock), a bot-side lookup.
require_text("${engine}" "    bool QuestDead(std::uint32_t questId) const\n    {\n        if (!anyDead_.load(std::memory_order_acquire) || !questId)\n            return false;" "view lookup: no lock while nothing is dead")
require_text("${engine}" "    void PublishDead(std::vector<std::uint32_t>& dead)" "view publish of the dead list")
require_text("${view_h}" "bool QuestDeadForWish(std::uint32_t questId);" "bot-side lookup declared")
require_text("${mgr_cpp}" "    return WorldManager().Directives().View().QuestDead(questId);" "bot-side lookup reads the view only")
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): the blocked pairs, the same pattern (own atomic, no lock while empty or off).
require_text("${engine}" "    bool BlockedForBot(std::uint32_t low, std::uint32_t questId) const\n    {\n        if (!anyBlocked_.load(std::memory_order_acquire) || !questId)\n            return false;" "view lookup: no lock while no pair is blocked")
require_text("${engine}" "        return quest_groups::BlockedIn(blocked_, low, questId);" "view lookup: binary search of the sorted pairs")
require_text("${engine}" "    void PublishBlocked(std::vector<std::uint64_t>& blocked)\n    {\n        bool const any = !blocked.empty();\n        if (!any && !anyBlocked_.load(std::memory_order_acquire))\n            return;" "view publish: no lock while nothing was or is blocked")
require_text("${view_h}" "bool QuestBlockedForWish(std::uint32_t low, std::uint32_t questId);" "bot-side blocked lookup declared")
require_text("${mgr_cpp}" "bool QuestBlockedForWish(std::uint32_t low, std::uint32_t questId)\n{\n    return WorldManager().Directives().View().BlockedForBot(low, questId);\n}" "bot-side blocked lookup reads the view only")
require_order("${view_h}" "bool QuestDeadForWish(std::uint32_t questId);" "bool QuestBlockedForWish(std::uint32_t low, std::uint32_t questId);" "blocked lookup next to the dead lookup")

# 4. The engine: keys into the rules, counted only for QUEST groups, published every pass, checked in QuestBlocked.
require_text("${mgr_cpp}" "    rules.deadQuestEnds = sPlayerbotAIConfig.questGroupsManagerDeadQuestEnds;" "rules: ends")
require_text("${mgr_cpp}" "    rules.deadQuestHours = sPlayerbotAIConfig.questGroupsManagerDeadQuestHours;" "rules: hours")
require_text("${engine}" "        if (g.type == GroupType::Quest)\n            NoteDeadQuest(g, why, io);" "only QUEST groups count")
region("${engine}" "    void NoteDeadQuest(ManagedGroup const& g, EndReason why, Io& io)" "\n    }\n" note_fn)
require_text("${note_fn}" "        bool const progress = g.anyProgress || why == EndReason::TurnedIn;" "progress or a turn-in resets")
require_text("${note_fn}" "        bool const deadEnd = why == EndReason::NoProgress && !g.anyProgress;" "dead end = no_progress without any progress")
require_text("${note_fn}" "\"[GroupMgr] event=quest_dead quest=%u group=%u ends=%u hours=%u\"" "quest_dead line format")
require_text("${engine}" "            g.lastProgressSec = nowSec_;\n            g.anyProgress = true;" "progress marks the group")
require_text("${engine}" "        if (deadEnds_ && deadQuests_.Dead(questId, nowSec))" "QuestBlocked checks dead quests")
require_text("${engine}" "        if (!deadEnds_)\n            deadQuests_.Clear();" "off keeps nothing")
require_text("${engine}" "        view_.Publish(viewBots_, viewCores_);\n        deadQuests_.DeadList(nowSec_, deadScratch_);" "dead list published with the view")
region("${dead_policy}" "    Note End(" "\n    }\n" end_fn)
require_text("${end_fn}" "        if (!endsNeeded || !questId)\n            return Note::Off;" "lever off keeps nothing")
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): the key into the rules, the hold (default = the old queue rules), the hold
# passed to Failed and ForgetStale, the pairs from the stores published with the view (nothing while off).
require_text("${mgr_cpp}" "    rules.wishSkipBlocked = sPlayerbotAIConfig.questGroupsManagerWishSkipBlocked != 0;" "rules: wish skip")
require_order("${mgr_cpp}" "    rules.deadQuestHours = sPlayerbotAIConfig.questGroupsManagerDeadQuestHours;" "    rules.wishSkipBlocked = " "rules: wish skip next to the lever 2 keys")
require_text("${engine}" "    bool wishSkipBlocked = false;" "Rules: wish skip default off")
require_text("${engine}" "        hold_ = quest_groups::BlockHold();\n        if (r.wishSkipBlocked)\n        {\n            hold_.keepBlocked = true;\n            hold_.maxAttempts = r.maxAttempts;\n            hold_.exhaustSeconds = r.deadQuestHours * 3600u;\n        }" "the hold: off = default, on = MaxAttempts and DeadQuestHours")
require_text("${engine}" "                questQueue_.Failed(m.low, g.questId, nowSec_, retryCooldownSec_, hold_);" "Failed gets the hold")
require_text("${engine}" "            questQueue_.ForgetStale(nowSec_, 600, hold_);" "ForgetStale gets the hold")
require_text("${engine}" "        view_.PublishDead(deadScratch_);\n        BlockedList(blockedScratch_);" "blocked pairs published with the view, after the dead list")
region("${engine}" "    void BlockedList(std::vector<std::uint64_t>& out) const" "\n    }\n" blocked_fn)
require_text("${blocked_fn}" "        out.clear();\n        if (!hold_.keepBlocked)\n            return;" "key off: nothing listed, no lock on the queue")
require_text("${blocked_fn}" "        questQueue_.BlockedKeys(nowSec_, hold_.maxAttempts, out);" "the queue's cooldown and exhausted entries")
require_text("${blocked_fn}" "        quest_groups::SortBlockKeys(out);" "sorted for the binary search")
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): wish_blocked appended at the end of the minute line (later pairs only
# append, OB-10 rule), one increment per fresh wish QuestBlocked blocks, with the key on or off (diagnosis).
require_text("${engine}" "    std::uint64_t wishes = 0;\n" "minute counter wishes")
require_text("${engine}" "    std::uint64_t wishBlocked = 0;\n    std::uint64_t noSupervisor = 0;" "minute counter wish_blocked")
require_text("${engine}" "            (unsigned long long)s.owned);\n    // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): wish_blocked appended at the end (later pairs only append, OB-10 rule).\n    if (n > 0 && std::size_t(n) < size)\n        n += std::snprintf(buf + n, size - std::size_t(n), \" wish_blocked=%llu\", (unsigned long long)m.wishBlocked);\n    return n;" "minute line: wish_blocked appended after owned")
forbid_text("${engine}" "wishes=%llu wish_blocked=%llu" "wish_blocked not mid-line (append only)")
require_text("${engine}" "                blocked = QuestBlocked(low, w->wish.questId, nowSec_, r.maxAttempts);\n                // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): wish_blocked, with the key on or off (diagnosis).\n                if (blocked)\n                    ++minute_.wishBlocked;" "wish_blocked counted right after QuestBlocked, not under the key")
count_text("${engine}" "++minute_.wishBlocked;" 1 "one wish_blocked increment")
count_text("${engine}" "wish_blocked=%llu" 1 "one wish_blocked field")
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): the pure hold (QuestGroupPolicy.h), the default is the old code path.
require_text("${qg_policy}" "    bool keepBlocked = false;" "BlockHold default off")
require_text("${qg_policy}" "    void ForgetStale(std::uint32_t now, std::uint32_t maxAgeSeconds, BlockHold const& hold = BlockHold())" "ForgetStale: default hold = old rule")
require_text("${qg_policy}" "            bool const stale = now - it->second.lastSeen > maxAgeSeconds;\n            bool const recent = it->second.attempts &&\n                (it->second.lastSeen > now || now - it->second.lastSeen <= hold.exhaustSeconds);\n            bool const keep = hold.keepBlocked && (it->second.blockedUntil > now || recent);\n            it = (stale && !keep) ? entries.erase(it) : std::next(it);" "ForgetStale keeps a blocked entry, and one with attempts up to the exhaust horizon after its last failure, only with the key on")
# twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): BlockedKeys lists only entries blocked now or exhausted (a kept entry whose
# cooldown ran out is no block: the bot may wish it again and its attempts add up).
require_text("${qg_policy}" "            if (kv.second.blockedUntil > now || (maxAttempts && kv.second.attempts >= maxAttempts))\n                out.push_back(kv.first);" "BlockedKeys: blocked now or exhausted only")
require_text("${qg_policy}" "        BlockHold const& hold = BlockHold())" "Failed: default hold = old rule")
require_text("${qg_policy}" "        if (hold.keepBlocked && hold.maxAttempts && e.attempts >= hold.maxAttempts)\n            e.blockedUntil = now + std::max(cooldownSeconds, hold.exhaustSeconds);" "Failed: the exhaust horizon only with the key on")

# 5. The unit test is registered.
require_text("${tests}" "add_executable(grp2030_dose_policy_tests\n  \"\${PB_MODULE_DIR}/t/grp2030_dose_policy_tests.cpp\")" "unit test target")
require_text("${tests}" "add_test(NAME grp2030_dose_policy\n  COMMAND grp2030_dose_policy_tests" "unit test registered")

message(STATUS "GRP2030_DOSE_LEVERS_SOURCE_CONTRACT=PASS")
