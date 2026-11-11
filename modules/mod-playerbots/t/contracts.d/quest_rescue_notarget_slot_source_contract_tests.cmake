cmake_policy(VERSION 3.16)
# twow-repo#703 item 1 (AiPlayerbot.QuestRescue.NoTargetFreeSlot, CLI-PATH 11.11.2026; design ws-10/cli-pathfinding/szf/
# DESIGN-703-1-NOTARGET-SLOT-20261111.md v2, OB-10 review 1ef7451f): a quest rescue with no target takes no global limiter slot.
# Pins:
#   1. the key (member 0, read + invalid value off and logged, dist 0, echo sorted after KeepRequestWhenFull);
#   2. the pure policy (QuestRescueSlotPolicy.h: no game types, no config, no log) and its unit test registration;
#   3. the read-only peek (no exchange, no drop line);
#   4. key 0: the legacy order (limiter, then the take, then the targets) and the old no_target branch untouched;
#   5. key 1: peek and targets before the limiter; an empty list takes the band rescue (no requeue, no limiter call), logs
#      slot=0 and keeps the 2 h cooldown; a granted slot takes the rung after the limiter and reuses the early list only for
#      the peeked rung (rung_mismatch counted, list computed again); the minute line only with the key, before the loop (the
#      end of the loop stays as the quest_failure_class contract pins it).
# Negative probes at the end.

if(NOT DEFINED PB_SOURCE_DIR OR NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "rescue notarget slot: PB_SOURCE_DIR and PB_MODULE_DIR are required")
endif()

set(TAG "rescue notarget slot")

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/QuestRescueSlotPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf_dist)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_file("${PB_SOURCE_DIR}/QuestBand.cpp" band)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
read_file("${PB_MODULE_DIR}/tests.cmake" tests_cmake)

# Functions, not macros: a macro re-parses its arguments, and source text with quotes and backslashes would break it. Each helper
# sets r in the caller (check_all) only when r is still empty, so the first failure wins.
function(need text needle what)
  if(r)
    return()
  endif()
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(r "missing ${what}" PARENT_SCOPE)
  endif()
endfunction()

function(never text needle what)
  if(r)
    return()
  endif()
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    set(r "forbidden ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()

function(order text first second what)
  if(r)
    return()
  endif()
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(r "order: ${what}" PARENT_SCOPE)
  endif()
endfunction()

# Returns the first failure in out ("" = all checks passed).
function(check_all policy config_h config_cpp conf_dist ai_h band mgr tests_cmake out)
  set(r "")

  # 1. The key.
  need("${config_h}" "    uint32 questRescueNoTargetFreeSlot = 0;" "member default 0")
  need("${config_cpp}" "    questRescueNoTargetFreeSlot = config.GetIntDefault(\"AiPlayerbot.QuestRescue.NoTargetFreeSlot\", 0);\n    if (!ai::rescue_slot::ModeValid(int32(questRescueNoTargetFreeSlot)))\n    {\n        sLog.outError(\"[QuestRescue] config key=NoTargetFreeSlot value=%d result=off (0 off, 1 on)\", int32(questRescueNoTargetFreeSlot));\n        questRescueNoTargetFreeSlot = 0;\n    }" "read, an invalid value off and logged")
  need("${config_cpp}" "            {\"QuestRescue.KeepRequestWhenFull\", Value(questRescueKeepRequestWhenFull)},\n            {\"QuestRescue.NoTargetFreeSlot\", Value(questRescueNoTargetFreeSlot)},\n            {\"Rescue.RepopDeadOnly\", Value(rescueRepopDeadOnly)}," "echo sorted")
  need("${conf_dist}" "\nAiPlayerbot.QuestRescue.NoTargetFreeSlot = 0\n" "dist default 0")
  string(REGEX MATCHALL "AiPlayerbot\\.QuestRescue\\.NoTargetFreeSlot =" dist_lines "${conf_dist}")
  list(LENGTH dist_lines n_dist)
  if(NOT r AND NOT n_dist EQUAL 1)
    set(r "one dist line expected, found ${n_dist}")
  endif()

  # 2. The pure policy.
  need("${policy}" "inline std::uint32_t ClampMode(std::int64_t value)\n{\n    return value == 1 ? 1u : 0u;\n}" "clamp 0/1")
  need("${policy}" "inline bool TargetsFirst(std::uint32_t mode)\n{\n    return mode == 1;\n}" "targets first only with 1")
  need("${policy}" "inline bool NeedsSlot(bool targetsEmpty)\n{\n    return !targetsEmpty;\n}" "no slot for an empty list")
  need("${policy}" "    return earlyComputed && takenRungPlusOne == peekedRungPlusOne;" "reuse only for the peeked rung")
  foreach(bad "sLog" "sPlayerbotAIConfig" "Player*" "#include \"Player")
    never("${policy}" "${bad}" "a game type, config or log in the pure policy")
  endforeach()
  need("${tests_cmake}" "add_executable(quest_rescue_slot_policy_tests\n  \"\${PB_MODULE_DIR}/t/quest_rescue_slot_policy_tests.cpp\")" "unit test target")
  need("${tests_cmake}" "add_test(NAME quest_rescue_slot_policy\n  COMMAND quest_rescue_slot_policy_tests" "unit test registered")

  # 3. The peek: read only.
  need("${ai_h}" "    uint32 PeekBandRescueRung(uint32 now) const;" "const peek")
  between("${band}" "uint32 PlayerbotAI::PeekBandRescueRung(uint32 now) const\n{" "\n}\n" peek)
  if(NOT r AND peek STREQUAL "")
    set(r "peek body not found")
  endif()
  need("${peek}" "return ai::quest_band::UnpackRescue(bandRescue.load(std::memory_order_relaxed), now, dropped);" "peek = load + the same unpack rule")
  never("${peek}" "exchange" "an exchange in the peek")
  never("${peek}" "sLog" "a drop line in the peek")

  # 4. + 5. The loop.
  between("${mgr}" "void RandomPlayerbotMgr::ProcessQuestRescues()" "\nvoid RandomPlayerbotMgr::RandomTeleportForLevel(" loop)
  if(NOT r AND loop STREQUAL "")
    set(r "ProcessQuestRescues not found")
  endif()
  # the key-1 block, from its switch to the limiter
  between("${loop}" "        bool const targetsFirst = ai::rescue_slot::TargetsFirst(sPlayerbotAIConfig.questRescueNoTargetFreeSlot);" "        // The bot asks again next minute while it stays stuck." early)
  if(NOT r AND early STREQUAL "")
    set(r "key-1 block not found before the limiter")
  endif()
  need("${early}" "        if (targetsFirst)\n        {\n            peekedRung = ai->PeekBandRescueRung(now);\n            earlyTargets = peekedRung ? ai::quest_band::HubRescueTargets(bot, peekedRung - 1) : QuestRescueTargets(bot);" "peek, then the targets, only with the key")
  between("${early}" "            if (!ai::rescue_slot::NeedsSlot(earlyTargets.empty()))\n            {" "                continue;\n            }" empty_branch)
  if(NOT r AND empty_branch STREQUAL "")
    set(r "empty branch not found")
  endif()
  need("${empty_branch}" "                ai->TakeBandRescueRung(now);" "empty: the band rescue consumed")
  need("${empty_branch}" "[QuestRescue] state=no_target slot=0 bot=%u level=%u race=%u zone=%u" "empty: the no_target line with slot=0")
  need("${empty_branch}" "                ai->OnQuestRescued(now);" "empty: the 2 h cooldown as before")
  need("${empty_branch}" "noTargetNoSlot.fetch_add(1" "empty: counted")
  never("${early}" "RequeueBandRescue" "a requeue in the key-1 block")
  never("${early}" "TryAcquire" "a limiter call before the key-1 decision")
  never("${early}" "HasCapacity" "a limiter look in the key-1 block")
  # the legacy order stays: request, limiter (refusal requeues a band rescue), take, targets
  order("${loop}" "!ai->TakeQuestRescueRequest() || IsParkedBot(guid)" "bool const targetsFirst" "the key-1 block after the request is taken")
  order("${loop}" "        // The bot asks again next minute while it stays stuck." "        if (!questRescueLimiter.TryAcquire(now))" "the limiter after the key-1 block")
  order("${loop}" "        if (!questRescueLimiter.TryAcquire(now))" "        uint32 const bandRung = ai->TakeBandRescueRung(now);" "the take only after a granted slot")
  need("${loop}" "            if (sPlayerbotAIConfig.questFirstProgressionBandLadder && ai->HasBandRescuePending())\n                ai->RequeueBandRescue();\n            continue;\n        }\n\n        uint32 const bandRung = ai->TakeBandRescueRung(now);" "the refusal branch unchanged (requeue, continue)")
  need("${loop}" "        bool const reuseEarly = ai::rescue_slot::ReuseEarlyTargets(targetsFirst, bandRung, peekedRung);\n        if (targetsFirst && !reuseEarly)\n            ai::rescue_slot::SharedCounters().rungMismatch.fetch_add(1, std::memory_order_relaxed);\n        std::vector<WorldLocation> const targets = reuseEarly ? std::move(earlyTargets) :\n            bandRung ? ai::quest_band::HubRescueTargets(bot, bandRung - 1) : QuestRescueTargets(bot);" "reuse only for the peeked rung, else computed again (key 0: always the legacy computation)")
  need("${loop}" "        if (targets.empty())\n        {\n            sLog.outBasic(\"[QuestRescue] state=no_target bot=%u level=%u race=%u zone=%u\",\n                bot->GetGUIDLow(), bot->GetLevel(), uint32(bot->getRace()), bot->GetZoneId());\n            ai->OnQuestRescued(now);\n            continue;\n        }" "the legacy no_target branch untouched")
  # the minute line only with the key, after the 10 s gate and before the loop
  need("${loop}" "    if (ai::rescue_slot::TargetsFirst(sPlayerbotAIConfig.questRescueNoTargetFreeSlot))\n    {\n        ai::rescue_slot::Counters& slotCounters = ai::rescue_slot::SharedCounters();\n        if (slotCounters.LogDue(uint64(now)))\n            sLog.outBasic(\"[QuestRescueMin] notarget_noslot=%u rung_mismatch=%u\"," "the minute line only with the key")
  order("${loop}" "    lastQuestRescueScan = now;\n\n    // twow-repo#703 item 1: the minute line" "    for (auto const& [guid, bot] : GetAllBots())" "the minute line after the 10 s gate, before the loop")

  set(${out} "${r}" PARENT_SCOPE)
endfunction()

check_all("${policy}" "${config_h}" "${config_cpp}" "${conf_dist}" "${ai_h}" "${band}" "${mgr}" "${tests_cmake}" e)
if(e)
  message(FATAL_ERROR "${TAG}: ${e}")
endif()

# Negative probes: each broken copy must be caught.
set(caught 0)
function(probe what file_var from to)
  string(REPLACE "${from}" "${to}" broken "${${file_var}}")
  if(broken STREQUAL "${${file_var}}")
    message(FATAL_ERROR "${TAG}: probe '${what}' did not change the text")
  endif()
  foreach(v policy config_h config_cpp conf_dist ai_h band mgr tests_cmake)
    set(p_${v} "${${v}}")
  endforeach()
  set(p_${file_var} "${broken}")
  check_all("${p_policy}" "${p_config_h}" "${p_config_cpp}" "${p_conf_dist}" "${p_ai_h}" "${p_band}" "${p_mgr}" "${p_tests_cmake}" e)
  if(NOT e)
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
  math(EXPR n "${caught} + 1")
  set(caught ${n} PARENT_SCOPE)
endfunction()
probe("dist default 1" conf_dist "\nAiPlayerbot.QuestRescue.NoTargetFreeSlot = 0\n" "\nAiPlayerbot.QuestRescue.NoTargetFreeSlot = 1\n")
probe("requeue on empty" mgr "                ai->TakeBandRescueRung(now);\n                sLog.outBasic(\"[QuestRescue] state=no_target slot=0" "                ai->RequeueBandRescue();\n                sLog.outBasic(\"[QuestRescue] state=no_target slot=0")
probe("limiter before the decision" mgr "        if (targetsFirst)\n        {\n            peekedRung = ai->PeekBandRescueRung(now);" "        if (targetsFirst && questRescueLimiter.TryAcquire(now))\n        {\n            peekedRung = ai->PeekBandRescueRung(now);")
probe("take instead of peek" mgr "            peekedRung = ai->PeekBandRescueRung(now);" "            peekedRung = ai->TakeBandRescueRung(now);")
probe("no cooldown on empty" mgr "                ai::rescue_slot::SharedCounters().noTargetNoSlot.fetch_add(1, std::memory_order_relaxed);\n                ai->OnQuestRescued(now);\n                continue;" "                ai::rescue_slot::SharedCounters().noTargetNoSlot.fetch_add(1, std::memory_order_relaxed);\n                continue;")
probe("reuse for any rung" policy "    return earlyComputed && takenRungPlusOne == peekedRungPlusOne;" "    return earlyComputed;")
probe("peek with exchange" band "    return ai::quest_band::UnpackRescue(bandRescue.load(std::memory_order_relaxed), now, dropped);" "    return ai::quest_band::UnpackRescue(bandRescue.exchange(0, std::memory_order_relaxed), now, dropped);")
probe("minute line without the key" mgr "    if (ai::rescue_slot::TargetsFirst(sPlayerbotAIConfig.questRescueNoTargetFreeSlot))\n    {\n        ai::rescue_slot::Counters& slotCounters" "    if (true)\n    {\n        ai::rescue_slot::Counters& slotCounters")
probe("invalid value kept" config_cpp "        questRescueNoTargetFreeSlot = 0;\n    }" "    }")
probe("echo unsorted" config_cpp "            {\"QuestRescue.KeepRequestWhenFull\", Value(questRescueKeepRequestWhenFull)},\n            {\"QuestRescue.NoTargetFreeSlot\", Value(questRescueNoTargetFreeSlot)}," "            {\"QuestRescue.NoTargetFreeSlot\", Value(questRescueNoTargetFreeSlot)},\n            {\"QuestRescue.KeepRequestWhenFull\", Value(questRescueKeepRequestWhenFull)},")
if(NOT caught EQUAL 10)
  message(FATAL_ERROR "${TAG}: ${caught} negative probes caught, 10 expected")
endif()
message(STATUS "${TAG}: all checks passed, 10 negative probes caught")
