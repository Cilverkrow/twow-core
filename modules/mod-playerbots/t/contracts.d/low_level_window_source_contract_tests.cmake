cmake_policy(VERSION 3.16)
# AiPlayerbot.Grind.LowLevelWindow (OB-10 22.11.2026 build OK, trace research1/L10-WINDOW-LEVER-TRACE-20261122 9d36421e): L1-10
# quest-first roster bots on their own take grind destinations in the GrindMode 1 window (LowLevelWindowPolicy.h). Pins the key
# (cohort mode, clamp + error line, echo, dist, default 0), the predicate (config first, no change for a gap filler), the three
# window seats (fetch snapshot, choice snapshot, trip / group checks), the flag's reset and copy, the per-cohort minute line and
# that the GrindMode 1 own-map skip does not come with it.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "low level window: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "low level window: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "low level window: forbidden ${description}: ${needle}")
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
    message(FATAL_ERROR "low level window: ${description}: ${count} times, expected ${expected}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "low level window: order ${description}: ${first} before ${second}")
  endif()
endfunction()

read_source("LowLevelWindowPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_mgr)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("RandomPlayerbotMgr.cpp" rpm)

# 1) The key: a cohort mode, default 0, clamp with one error line, echo, dist.
require_text("${config_h}" "    uint32 grindLowLevelWindow = 0;" "member, default 0")
require_text("${config_cpp}" "    grindLowLevelWindow = uint32(config.GetIntDefault(\"AiPlayerbot.Grind.LowLevelWindow\", 0));\n    if (!ai::low_level_window::ValidMode(grindLowLevelWindow))\n    {" "loader with the mode check")
require_text("${config_cpp}" "[LowLevelWindow] config key=Grind.LowLevelWindow value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)" "error line")
require_text("${config_cpp}" "        grindLowLevelWindow = 0;\n    }" "an invalid mode is off")
require_text("${config_cpp}" "            {\"Grind.GreyByLevel\", Value(grindGreyByLevel)},\n            {\"Grind.LowLevelWindow\", Value(grindLowLevelWindow)}," "ConfigEcho entry (sorted)")
require_text("${conf_dist}" "# same cohorts as QuestBook. Any other value = off (one error line at load).\nAiPlayerbot.Grind.LowLevelWindow = 0" "dist key, default 0")
require_text("${policy}" "return mode <= cohort::ModeOnlyA;" "modes 0..3")
require_text("${policy}" "constexpr std::uint32_t MaxLevel = 10;" "level 10 cap")
require_text("${policy}" "return !gapGrind && rosterOnItsOwn && level >= 1u && level <= MaxLevel &&\n        cohort::Applies(ValidMode(mode) ? mode : cohort::ModeOff, guidLow);" "population via the shared cohort rule")
require_text("${policy}" "return quest_grind_mode::GapWindowLowLevel(level, grayLevel);" "the GrindMode 1 window low, called not copied")
require_text("${policy}" "return quest_grind_mode::GapWindowHighLevel(level);" "the GrindMode 1 window high, called not copied")

# 2) The predicate: the config first; a gap filler is no change.
require_text("${ai_cpp}" "bool PlayerbotAI::IsLowLevelWindow()\n{\n    if (!sPlayerbotAIConfig.grindLowLevelWindow || !bot)\n        return false;" "config read first")
require_text("${ai_cpp}" "            !HasRealPlayerMaster(), bot->GetLevel(), IsQuestGapGrind());" "GrindMode 1 population excluded")

# 3) The fetch snapshot: the pinned GrindMode 1 block unchanged, the window only as its else branch.
require_text("${travel_mgr}" "        gapWindowHigh = ai::quest_grind_mode::GapWindowHighLevel(level);\n    }\n    // AiPlayerbot.Grind.LowLevelWindow" "lever right after the GrindMode 1 snapshot")
require_text("${travel_mgr}" "    else if (gapGrind && ai->IsLowLevelWindow())\n    {\n        gapWindowLow = ai::low_level_window::WindowLow(level, MaNGOS::XP::GetGrayLevel(level));\n        gapWindowHigh = ai::low_level_window::WindowHigh(level);\n    }" "window snapshot only for a window grind (gapGrind)")
set(n_ai_llw "ai->IsLowLevelWindow()")
count_text("${travel_mgr}" n_ai_llw 1 "one predicate read in TravelMgr.cpp")

# 4) The flag: reset on a new destination, copied with the choice; trip check with the window.
require_text("${travel_h}" "		bool lowLevelWindow = false;" "flag default false")
require_text("${travel_mgr}" "    if (tDestination1 != tDestination)\n        lowLevelWindow = false;" "reset on a new destination")
require_text("${travel_mgr}" "    gapWindowEnd = target->gapWindowEnd;\n    // Grind.LowLevelWindow: the choice's window flag (false for every target but a low-level window grind choice).\n    lowLevelWindow = target->lowLevelWindow;" "copied with the choice")
require_text("${travel_mgr}" "    return tDestination->IsActive(player, PlayerTravelInfo(player, gapWindowEnd != 0 || lowLevelWindow));" "trip judged with its window")

# 5) The choice: flag before the GrindMode 1 choice, same snapshot in SetBestTarget, group check; own-map skip mode 1 only.
require_text("${choose}" "    if (futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)) && ai->IsLowLevelWindow())\n        newTarget.SetLowLevelWindow(true);" "choice flag on a grind fetch only")
require_order("${choose}" "        newTarget.SetLowLevelWindow(true);" "    if (ai->IsQuestGapGrind() && futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)))" "lever choice before the GrindMode 1 choice")
require_text("${choose}" "    bool const gapGrindChoice = target->IsGapGrindChoice() || target->IsLowLevelWindow();" "choice window snapshot")
require_text("${choose}" "PlayerTravelInfo(player, groupTarget->GetGapWindowEnd() != 0 || groupTarget->IsLowLevelWindow())" "group member trip check")
count_text("${choose}" n_ai_llw 1 "one predicate read in the choose action")
set(n_llw "IsLowLevelWindow()")
# choice + none + trips + SetBestTarget + group check: the own-map skip and every other GrindMode 1 rule read IsGapGrindChoice only.
count_text("${choose}" n_llw 5 "window flag reads in the choose action")

# 6) The minute line: none / trips counted per cohort, reported once a minute only with the switch.
require_text("${choose}" "            PlayerbotAI::NoteLowLevelWindow(uint8(ai::low_level_window::Event::None), bot->GetGUIDLow());" "none counted")
require_text("${choose}" "        PlayerbotAI::NoteLowLevelWindow(uint8(ai::low_level_window::Event::Trips), bot->GetGUIDLow());" "trips counted")
require_order("${choose}" "    if (!setNewTarget(requester, &newTarget, travelTarget))\n        return false;" "        PlayerbotAI::NoteLowLevelWindow(uint8(ai::low_level_window::Event::Trips), bot->GetGUIDLow());" "trips only after the commit")
require_text("${rpm}" "void ReportLowLevelWindow()\n{\n    if (!sPlayerbotAIConfig.grindLowLevelWindow)\n        return;" "minute line gated by the switch")
require_text("${rpm}" "    std::string const line = PlayerbotAI::TakeLowLevelWindowMinuteLine();\n    if (!line.empty())" "no empty line")
require_text("${rpm}" "    ReportGrindWindow();  // twow-repo#601 S2 / S4: [GrindWindow] minute once per minute, not when empty (GrindMode = 1)\n    ReportLowLevelWindow();" "reported from the world tick")
require_text("${policy}" "return \"[LowLevelWindow] minute\" + pair(Event::Fetches, \"fetches\") + pair(Event::None, \"none\") + pair(Event::Trips, \"trips\");" "line format")

# 7) Nothing beyond the window: no request gate, trip clock or own-map skip for the lever.
forbid_text("${choose}" "IsLowLevelWindow() && ai->IsQuestGapGrind()" "GrindMode 1 rules joined to the lever")
forbid_text("${travel_mgr}" "lowLevelWindow ? " "a window-dependent trip clock")

message(STATUS "LOW_LEVEL_WINDOW_SOURCE_CONTRACT=PASS")
