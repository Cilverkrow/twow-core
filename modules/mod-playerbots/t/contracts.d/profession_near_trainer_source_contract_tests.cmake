# twow-repo#333 Berufe P1-n (CLI-BERUFE 26.10.2026, OB-00 go after M1 FUTILE): AiPlayerbot.ProfessionTraining.NearRadius, yards,
# default 0 (off), at most 1000, ConfigEcho. The gap trip of P1-a starts only while the quest route backoff runs and no turn-in
# waits - 26 rejections in 57 min for 30 bots, 0 trainer requests in M1 and in cr2-C. The near trip sends a bot that has not
# learned a planned phase-one skill yet to a trainer of it within the radius, without a gap, at 6.36 (above grind 6.35 and the
# quest request 6.3), with the turn-in, band walk, money and hourly-limit gates of the gap trip and a throttled gate trace.
# The rule behaviour is tested in t/profession_travel_policy_tests.cpp; this contract pins the wiring.

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
    message(FATAL_ERROR "#333 P1-n NearRadius: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-n NearRadius: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-n NearRadius: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("ProfessionTravelPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/values/TravelValues.h" values_h)
read_source("strategy/values/TravelValues.cpp" values_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("strategy/generic/TravelStrategy.cpp" strategy_cpp)

# Switch: yards, default 0, clamped, documented, echoed; the mode and cohort stay ProfessionTraining.Travel's.
require_text("${config_h}" "uint32 professionTrainingNearRadius = 0;" "default off")
require_text("${config_cpp}" "professionTrainingNearRadius = ai::profession_travel::ClampNearRadius(config.GetIntDefault(\"AiPlayerbot.ProfessionTraining.NearRadius\", 0));" "config read, clamped")
require_text("${config_cpp}" "{\"ProfessionTraining.NearRadius\", Value(professionTrainingNearRadius)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.ProfessionTraining.NearRadius = 0\n" "documented, 0 by default")
forbid_text("${conf_dist}" "\nAiPlayerbot.ProfessionTraining.NearRadius = 3" "a radius by default in the dist")
require_text("${policy}" "return std::uint32_t(configured) > kMaxNearRadius ? kMaxNearRadius : std::uint32_t(configured);" "at most kMaxNearRadius")
require_text("${policy}" "constexpr std::uint32_t kMaxNearRadius = 1000;" "1000 yd cap")

# Pure rules.
forbid_text("${policy}" "#include \"playerbot/" "game headers in the pure policy")
require_text("${policy}" "return IsPhaseOneSkill(skill) && !hasSkill;" "only an unlearned phase-one skill (\"once\")")
require_text("${policy}" "return nearRadius > 0 && distance >= 0.0f && distance <= float(nearRadius);" "within the radius, 0 = off")
require_text("${policy}" "return now + kNearHoldSeconds;" "the near hold from the request")
require_text("${policy}" "constexpr std::uint32_t kNearHoldSeconds = 600;" "10 min hold")
require_text("${policy}" "return lastCheck == 0 || now >= lastCheck + kNearCheckSeconds;" "one destination walk per check interval")
# The gate order: eligible, hourly limit, turn-in, band walk, money, trainers, radius, reachable.
require_text("${policy}" "    if (!eligible)\n        return \"not_eligible\";\n    if (!cooldownOver)\n        return \"cooldown\";\n    if (turnInWaiting)\n        return \"turn_in\";\n    if (bandWalk)\n        return \"band\";\n    if (!money)\n        return \"money\";\n    if (!trainers)\n        return \"no_trainer\";\n    if (!nearTrainers)\n        return \"not_near\";\n    if (!reachableTrainers)\n        return \"unreachable\";\n    return nullptr;" "the gate order")

# Trainers: unlearned skills only on request, the radius on the bot's map, then the P1-b2 filter.
require_text("${values_h}" "std::vector<int32> NearProfessionTrainers(PlayerbotAI* ai, uint32 radius, ProfessionTripDrops* drops = nullptr," "shared near trainer list")
require_text("${values_h}" "uint32 inRadius = 0;" "no identifier 'near' (a windows.h macro under MSVC)")
require_text("${values_cpp}" "if (onlyUnlearned && !ai::profession_travel::NearTripSkill(skill, ai->HasSkill(SkillType(skill))))" "only unlearned skills")
require_text("${values_cpp}" "std::vector<int32> const trainers = PhaseOneProfessionTrainers(ai, true);" "the near list asks for unlearned skills")
require_text("${values_cpp}" "if (destination->HasPointsOnMap(center) && ai::profession_travel::WithinNearRadius(destination->DistanceTo(center), radius) &&" "the radius rule on each destination of the bot's map")
require_text("${values_cpp}" "return inRadius.empty() ? inRadius : ProfessionTripTargets(ai, inRadius, TravelDestinationPurpose::Trainer, drops);" "hostile / unreachable dropped (P1-b2)")

# Start: no quest gap, but the turn-in, the band walk, the money and the trainer trip's hourly limit at every calculation;
# nothing while a target is prepared or active; one destination walk per interval, its result counts until the next walk.
require_text("${values_cpp}" "else if (name == \"trainer profession near\" || name == \"trainer profession near hold\")" "the near value")
require_order("${values_cpp}" "else if (name == \"trainer profession near\" || name == \"trainer profession near hold\")" "else if (name.find(\"trainer\") == 0)" "the near value before the generic trainer branch")
require_text("${values_cpp}" "ProfessionTripHolds(ai, ai::profession_travel::kTrainerTripValue, \"trainer\", !PhaseOneProfessionTrainers(ai, true).empty());" "the near hold lasts while an unlearned skill is to learn")
require_text("${values_cpp}" "if (travelTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE || AI_VALUE(bool, \"travel target active\"))\n        return false;" "nothing while a target is prepared or active")
require_text("${values_cpp}" "uint32(std::max(0, AI_VALUE2(int, \"manual int\", ai::profession_travel::kTrainerTripValue))));" "the hourly limit of the trainer trip")
require_text("${values_cpp}" "turnIn = ProfessionTripTurnInWaiting(bot, travelTarget);" "an open turn-in goes first")
require_text("${values_cpp}" "band = travelTarget->GetBandTripEnd() != 0;" "a band walk goes first")
# OB-10 26.10.: a bot in a group starts no near trip (the gap trip has the same gate, profession_travel contract).
require_text("${values_cpp}" "bool const rosterOnItsOwn = applies && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&\n        !bot->GetGroup();\n    bool const eligible = ai::profession_travel::MayStartTrip(applies, rosterOnItsOwn, bot->GetLevel(), true);" "no near trip in a group")
require_order("${values_cpp}" "bool const cheapGates = eligible && cooldownOver && !turnIn && !band && money;" "SET_AI_VALUE2(int, \"manual int\", ai::profession_travel::kNearCheckValue, int(now));" "the cheap gates before the walk slot is spent")
require_order("${values_cpp}" "SET_AI_VALUE2(int, \"manual int\", ai::profession_travel::kNearCheckValue, int(now));" "found = !ai::NearProfessionTrainers(ai, radius, &drops, &counts).empty();" "the check time before the destination walk")
require_text("${values_cpp}" "found = AI_VALUE2(int, \"manual int\", ai::profession_travel::kNearResultValue) != 0;" "between two walks the last result counts (pre-review)")
require_text("${values_cpp}" "sLog.outBasic(\"[ProfTravel] state=gate kind=trainer path=near bot=%u level=%u cohort=%c result=%s money=%u need=%u trainers=%u in_radius=%u dropped_hostile=%u dropped_unreachable=%u radius=%u\"," "the gate trace")
require_text("${values_cpp}" "if (sPlayerbotAIConfig.professionTrainingTrace && eligible && (walked || !cheapGates) &&" "the trace only with ProfessionTraining.Trace, eligible bots, from a walk or a refusing cheap gate")
require_text("${values_cpp}" "AI_VALUE2(int, \"manual int\", key + ai::profession_travel::kNearSuffix) ? \"near\" : \"gap\");" "path= on the end line")

# Request: the same trip value, an empty near request spends nothing, the near hold, the whole trip as hold_over_s, path=.
require_text("${choose_cpp}" "NearProfessionTrainers(ai, sPlayerbotAIConfig.professionTrainingNearRadius, &drops) :" "the near request entries")
require_order("${choose_cpp}" "if (nearTrip && entries.empty())\n        {\n            SET_AI_VALUE2(int, \"manual int\", ai::profession_travel::kNearResultValue, 0);\n            return false;\n        }" "SET_AI_VALUE2(int, \"manual int\", key, int(now));" "an empty near request does not spend the hourly limit")
require_text("${choose_cpp}" "uint32 const backoffUntil = nearTrip ? now : uint32(std::max(0, AI_VALUE2(int, \"manual int\", \"quest route backoff until\")));" "the near trip counts its whole time as hold_over_s")
require_text("${choose_cpp}" "uint32 const holdUntil = nearTrip ? ai::profession_travel::NearHoldUntil(now) : ai::profession_travel::HoldUntil(now, backoffUntil);" "the near hold")
require_order("${choose_cpp}" "now - endedAt, AI_VALUE2(int, \"manual int\", key + ai::profession_travel::kNearSuffix) ? \"near\" : \"gap\");" "SET_AI_VALUE2(int, \"manual int\", key + ai::profession_travel::kNearSuffix, nearTrip ? 1 : 0);" "the lost line names the old trip's path before the new one is stored")
require_text("${choose_cpp}" "travelName == \"trainer profession near\" ? \"should travel named::trainer profession near hold\" :" "the near hold condition")
require_text("${choose_cpp}" "else if (name == \"trainer profession near\")   // P1-n\n        return true;" "isAllowed")
require_text("${choose_cpp}" "uint32(entries.size()), drops.hostile, drops.unreachable, holdUntil - now, nearTrip ? \"near\" : \"gap\");" "path= on the request line")

# Strategy: only with both switches, at 6.36 - above grind 6.35 and the quest request 6.3.
require_text("${strategy_cpp}" "if (sPlayerbotAIConfig.professionTrainingTravel && sPlayerbotAIConfig.professionTrainingNearRadius)\n        triggers.push_back(new TriggerNode(\n            travel_request::TriggerName(\"val::should travel named::trainer profession near\", gateRequests),\n            NextAction::array(0, new NextAction(\"request named travel target::trainer profession near\", 6.36f), NULL)));" "near row")
require_text("${strategy_cpp}" "{\"\",TravelDestinationPurpose::Grind, 6.35f}," "grind stays at 6.35")

message(STATUS "#333 P1-n NearRadius source contract passed")
