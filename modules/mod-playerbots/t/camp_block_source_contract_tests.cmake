# Re-deaths at camps (CLI-Deep 25.10.2026, OB-00 go; CLI-ROT adds analysis): AiPlayerbot.DestinationDeaths.CampBlock (default 0) keys on
# the death position of a roster bot - destinations near a recent camp death are skipped, quest destinations only rank lower - and
# AiPlayerbot.DangerMap.CountAddDeaths (default 0) lets a death with enough living attackers count in the shared danger map whatever
# the killer's level, for the querying bot's cohort.

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
    message(FATAL_ERROR "camp block: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "camp block: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "camp block: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "camp block: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "camp block: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_source)
read_source("TravelMgr.cpp" travel)
read_source("TravelMgr.h" travel_h)
read_source("CampBlockPolicy.h" policy)
read_source("DangerMapPolicy.h" danger)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/actions/MovementActions.cpp" movement)

# 1. Keys: neutral switch defaults, an unknown mode is off, bounded parameters, documented in conf.dist.
require_text("${config_h}" "uint32 destinationDeathsCampBlock = 0;" "CampBlock member default off")
require_text("${config_h}" "uint32 dangerMapCountAddDeaths = 0;" "CountAddDeaths member default off")
require_text("${config_cpp}" "destinationDeathsCampBlock = config.GetIntDefault(\"AiPlayerbot.DestinationDeaths.CampBlock\", 0);" "CampBlock default 0")
require_text("${config_cpp}" "if (destinationDeathsCampBlock > 2)" "unknown CampBlock mode checked")
require_text("${config_cpp}" "dangerMapCountAddDeaths = config.GetIntDefault(\"AiPlayerbot.DangerMap.CountAddDeaths\", 0);" "CountAddDeaths default 0")
require_text("${config_cpp}" "if (dangerMapCountAddDeaths > 2)" "unknown CountAddDeaths mode checked")
require_text("${config_cpp}" "\"AiPlayerbot.DestinationDeaths.CampBlock.Seconds\", 1800)" "Seconds default 1800")
require_text("${config_cpp}" "\"AiPlayerbot.DestinationDeaths.CampBlock.Radius\", 60.0f)" "Radius default 60")
require_text("${config_cpp}" "\"AiPlayerbot.DestinationDeaths.CampBlock.MinAttackers\", 2)" "MinAttackers default 2")
require_text("${config_cpp}" "\"AiPlayerbot.DangerMap.AddDeathMinAttackers\", 3)" "AddDeathMinAttackers default 3 (OB-00)")
foreach(key "DestinationDeaths.CampBlock = 0" "DestinationDeaths.CampBlock.Seconds = 1800" "DestinationDeaths.CampBlock.Radius = 60"
        "DestinationDeaths.CampBlock.MinAttackers = 2" "DangerMap.CountAddDeaths = 0" "DangerMap.AddDeathMinAttackers = 3")
  require_text("${conf_dist}" "\nAiPlayerbot.${key}\n" "documented ${key}")
endforeach()

# 2. The death: living attackers counted once, only with a switch on; the danger map records them; the camp ring gets the position
#    whatever the travel target (not behind IsActiveForDeathAttribution).
between("${ai_source}" "void PlayerbotAI::OnDeath()" "\nvoid PlayerbotAI::" on_death)
require_text("${on_death}" "if (sPlayerbotAIConfig.destinationDeathsCampBlock || sPlayerbotAIConfig.dangerMapCountAddDeaths)" "attackers only read with a switch on")
require_text("${on_death}" "ai::death_adds::MergeAdds(live, deathAdds, ai::death_adds::Fresh(deathAdds, WorldTimer::getMSTime())," "adds from the fight snapshot")
require_text("${on_death}" "if (deathTarget && deathTarget->IsAlive())\n                    ++deathAttackers;" "the living target counts")
require_text("${on_death}" "RecordDangerMapDeath(bot, AI_VALUE(Unit*, \"current target\"), deathAttackers);" "attackers into the danger map")
require_text("${ai_source}" "death.attackers = uint8(std::min<uint32>(255, attackers));" "recorded attacker count")
require_order("${on_death}" "travelTarget->OnDeathAtDestination();" "travelTarget->OnCampDeath(deathAttackers);" "camp record next to the destination record")

between("${travel}" "void TravelTarget::OnCampDeath(uint32 attackers)" "\n}\n" camp)
require_text("${camp}" "ai::cohort::Applies(sPlayerbotAIConfig.destinationDeathsCampBlock, bot->GetGUIDLow())" "cohort mode")
require_text("${camp}" "!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) || ai->HasRealPlayerMaster() || bot->GetMap()->IsDungeon()" "roster bots on their own in the open world")
require_text("${camp}" "camp_block::Counts(attackers, sPlayerbotAIConfig.destinationDeathsCampBlockMinAttackers)" "MinAttackers")
require_text("${camp}" "\"[CampBlock] state=record bot=%u level=%u map=%u zone=%u x=%.0f y=%.0f attackers=%u radius=%.0f seconds=%u\"" "greppable line")
forbid_text("${camp}" "IsActiveForDeathAttribution" "the position rule must not depend on the travel target")
forbid_text("${camp}" "tDestination" "the position rule must not depend on the travel target")
require_text("${travel_h}" "camp_block::Ring campDeaths;" "bounded per-bot ring")
require_text("${policy}" "constexpr std::size_t MaxDeaths = 4;" "ring size")
forbid_text("${policy}" "mutex" "per-bot state needs no lock")

# 3. The choice: after the skip predicate, forced targets never judged, quest destinations only a fallback that is taken when
#    nothing else is selected, one deferral line per touched choice.
between("${choose}" "bool ChooseTravelTargetAction::SetBestTarget(" "\n}\n" best)
require_order("${best}" "switch (CandidateSkipOf(ai, bot, target, persistentTarget, excludedTurnInTarget, destination, position))" "persistentTarget->IsCampBlocked(position)" "after the skip predicate")
require_text("${best}" "bool const campBlocked = !target->IsForced() && persistentTarget && persistentTarget->IsCampBlocked(position);" "forced targets never judged")
require_text("${best}" "camp_block::Judge(true, dynamic_cast<QuestTravelDestination const*>(destination) != nullptr)" "quest destinations rank lower")
require_order("${best}" "if (campVerdict == camp_block::Verdict::Fallback)" "target->SetTarget(destination, position);" "fallback held back before the selection")
require_text("${best}" "if (!hasTarget && !chooseBudgetExceeded && campFallbackDestination)" "fallback only when nothing else is chosen")
require_text("${best}" "\"[QuestFirstRoute] state=deferred bot=%u level=%u reason=camp_block skipped=%u ranked_lower=%u fallback_taken=%u selected_other=%u\"" "greppable deferral line")
require_text("${policy}" "return questDestination ? Verdict::Fallback : Verdict::Skip;" "never a veto for quest destinations")

# 4. Danger map: the add-death rule only through Params, set per query for the bot's cohort; the level rule unchanged.
require_text("${danger}" "std::uint32_t addDeathMinAttackers = 0;" "rule off by default")
require_text("${danger}" "return death.killerLevel >= botLevel + params.levelMargin ||\n        (params.addDeathMinAttackers && death.attackers >= params.addDeathMinAttackers);" "level rule or crowd")
require_text("${choose}" "if (bot && ai::cohort::Applies(sPlayerbotAIConfig.dangerMapCountAddDeaths, bot->GetGUIDLow()))\n        params.addDeathMinAttackers = sPlayerbotAIConfig.dangerMapAddDeathMinAttackers;" "cohort per query (travel choice)")
require_text("${movement}" "if (ai::cohort::Applies(sPlayerbotAIConfig.dangerMapCountAddDeaths, bot->GetGUIDLow()))\n            params.addDeathMinAttackers = sPlayerbotAIConfig.dangerMapAddDeathMinAttackers;" "cohort per query (far-move arrival)")
forbid_text("${choose}" "DangerMapParams())" "every travel-choice query passes the bot")

message(STATUS "camp_block source contract passed")
