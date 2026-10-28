cmake_policy(VERSION 3.16)
# twow-repo#333 Berufe P1-y (OB-00 28.10.2026 after M1r run 5 FAIL on the tool criterion: mining 2/4, skinning 0/3). The tool trip
# started only in a quest gap: two skinners went 3 h 45 min without a knife and without one tool request. "tool vendor near" sends
# a bot with mining / skinning but without its tool to a vendor within ProfessionTraining.NearRadius without a gap, under the gates
# of the near trainer trip (P1-n) and the hourly limit of the tool trip. This contract pins the wiring: after learning without the
# tool exactly one near trip (the request writes the tool trip time, the lock refuses the next for 3600 s); with the tool or with
# the lock none; an unreachable vendor gives no request and at most one walk per kNearCheckSeconds (no loop).

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-y near tool: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-y near tool: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-y near tool: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The body of a function from its signature to the first closing brace at column 0.
function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "#333 P1-y near tool: ${signature} not found")
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "\n}\n" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#333 P1-y near tool: end of ${signature} not found")
  endif()
  string(SUBSTRING "${rest}" 0 ${end_at} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("ProfessionTravelPolicy.h" policy_h)
read_source("strategy/values/TravelValues.h" values_h)
read_source("strategy/values/TravelValues.cpp" values_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("strategy/generic/TravelStrategy.cpp" strategy_cpp)

# Pure rules: the gates of the near trainer trip, no_vendor for no_trainer, own check / trace / result values.
require_text("${policy_h}" "inline char const* NearToolGateReason(bool eligible, bool cooldownOver, bool turnInWaiting, bool bandWalk, bool money, bool vendors," "the gate reasons")
require_text("${policy_h}" "constexpr char const* kNearToolCheckValue = \"profession near tool check at\";" "its own check value")
require_text("${policy_h}" "constexpr char const* kNearToolResultValue = \"profession near tool found\";" "its own result value")
require_text("${values_h}" "std::vector<int32> NearGatherToolVendors(PlayerbotAI* ai, uint32 tool, uint32 radius, ProfessionTripDrops* drops = nullptr," "the near vendor list")

# The vendor list: the vendors of the tool within the radius, hostile / unreachable / home zone dropped as for every trip (P1-b2, P1-u).
function_body("${values_cpp}" "std::vector<int32> ai::NearGatherToolVendors(" vendors_fn)
require_text("${vendors_fn}" "std::vector<int32> const vendors = GatherToolVendors(ai, tool);" "the vendors of the tool")
require_text("${vendors_fn}" "NearTripEntries(ai, vendors, TravelDestinationPurpose::Vendor, radius);" "within the radius")
require_text("${vendors_fn}" "return inRadius.empty() ? inRadius : ProfessionTripTargets(ai, inRadius, TravelDestinationPurpose::Vendor, drops);" "the trip target filter")

# The start condition.
function_body("${values_cpp}" "bool ProfessionNearToolTripStartable(PlayerbotAI* ai)" start_fn)
# With the tool: none, before any gate, value or line.
require_order("${start_fn}" "uint32 const tool = ai::MissingGatherTool(ai);\n    if (!tool)\n        return false;" "bool const cheapGates" "with the tool no trip, before the gates")
require_order("${start_fn}" "if (!tool)\n        return false;" "sLog.outBasic(" "with the tool no gate line")
# No gap needed: the quest route backoff is not read.
forbid_text("${start_fn}" "quest route backoff until" "a quest gap condition")
forbid_text("${start_fn}" "InQuestGap(" "a quest gap condition")
# The switch of the tool trip and the hourly limit of the tool trip (shared with the gap trip): the 3600 s lock refuses.
require_text("${start_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.professionUseToolBuy, bot->GetGUIDLow())" "the ToolBuy mode")
require_text("${start_fn}" "ai::profession_travel::CooldownOver(now,\n        uint32(std::max(0, AI_VALUE2(int, \"manual int\", ai::profession_travel::kToolTripValue))));" "the tool trip lock")
forbid_text("${start_fn}" "kTrainerTripValue" "the trainer trip lock")
# The gates of the near trainer trip.
require_text("${start_fn}" "turnIn = ProfessionTripTurnInWaiting(bot, travelTarget);" "the open turn-in")
require_text("${start_fn}" "band = travelTarget->GetBandTripEnd() != 0;" "the band walk")
require_text("${start_fn}" "money = proto && AI_VALUE2(uint32, \"free money for\", (uint32)NeedMoneyFor::tradeskill) >= proto->BuyPrice;" "the money of the gap tool trip")
require_text("${start_fn}" "bool const cheapGates = eligible && cooldownOver && !turnIn && !band && money;" "the cheap gates")
require_text("${start_fn}" "if (travelTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE || AI_VALUE(bool, \"travel target active\"))\n        return false;" "nothing while a target is prepared or active")
# One walk per kNearCheckSeconds, its result counts between two walks (an unreachable vendor: no loop).
require_order("${start_fn}" "if (ai::profession_travel::NearCheckDue(now, uint32(std::max(0, AI_VALUE2(int, \"manual int\", ai::profession_travel::kNearToolCheckValue)))))" "found = !ai::NearGatherToolVendors(ai, tool, radius, &drops, &counts).empty();" "the walk due check")
require_order("${start_fn}" "SET_AI_VALUE2(int, \"manual int\", ai::profession_travel::kNearToolCheckValue, int(now));" "found = !ai::NearGatherToolVendors(ai, tool, radius, &drops, &counts).empty();" "the check time before the walk")
require_text("${start_fn}" "found = AI_VALUE2(int, \"manual int\", ai::profession_travel::kNearToolResultValue) != 0;" "the cached result between two walks")
require_text("${start_fn}" "char const* const refused = ai::profession_travel::NearToolGateReason(eligible, cooldownOver, turnIn, band, money," "the gate reason")
require_text("${start_fn}" "[ProfTravel] state=gate kind=tool path=near bot=%u" "the gate trace")
require_text("${start_fn}" "return !refused;" "starts only with every gate passed")

# The value: start condition and hold (while the tool is missing, on the tool trip value).
require_text("${values_cpp}" "else if (name == \"tool vendor near\" || name == \"tool vendor near hold\")\n    {\n        if (name == \"tool vendor near hold\")\n            return ai::cohort::Applies(sPlayerbotAIConfig.professionUseToolBuy, bot->GetGUIDLow()) &&\n                ProfessionTripHolds(ai, ai::profession_travel::kToolTripValue, \"tool\", MissingGatherTool(ai) != 0);\n\n        return ProfessionNearToolTripStartable(ai);\n    }" "the value")
require_text("${values_cpp}" "bool const trainerTrip = travelPurpose != \"tool vendor\" && travelPurpose != \"tool vendor near\";" "no_target: a tool trip")
require_text("${values_cpp}" "char const* const path = travelPurpose == \"trainer profession near\" || travelPurpose == \"tool vendor near\" ? \"near\" : \"gap\";" "no_target: path=near")

# The request: a near tool trip; a cached walk whose vendors are gone gives no request (the lock stays unspent, no loop).
require_text("${choose_cpp}" "bool const trainerTrip = travelName != \"tool vendor\" && travelName != \"tool vendor near\";" "a tool trip")
require_text("${choose_cpp}" "bool const nearTrip = travelName == \"trainer profession near\" || travelName == \"tool vendor near\";" "a near trip")
require_text("${choose_cpp}" "NearGatherToolVendors(ai, tool, sPlayerbotAIConfig.professionTrainingNearRadius, &drops)) :" "the near request entries")
require_order("${choose_cpp}" "SET_AI_VALUE2(int, \"manual int\", trainerTrip ? ai::profession_travel::kNearResultValue : ai::profession_travel::kNearToolResultValue, 0);\n            return false;" "SET_AI_VALUE2(int, \"manual int\", key, int(now));" "an empty near list before the lock is written")
require_text("${choose_cpp}" "travelName == \"tool vendor near\" ? \"should travel named::tool vendor near hold\" :" "the hold condition")
require_text("${choose_cpp}" "else if (name == \"tool vendor near\")   // P1-y\n        return true;" "isAllowed")
require_text("${choose_cpp}" "futureTravelPurpose == \"tool vendor\" || futureTravelPurpose == \"tool vendor near\" ? \"tool\" : \"trainer\"" "the target line kind")

# The trigger: behind ToolBuy and the near radius, just above the near trainer trip.
require_text("${strategy_cpp}" "if (sPlayerbotAIConfig.professionUseToolBuy && sPlayerbotAIConfig.professionTrainingNearRadius)\n        triggers.push_back(new TriggerNode(\n            travel_request::TriggerName(\"val::should travel named::tool vendor near\", gateRequests),\n            NextAction::array(0, new NextAction(\"request named travel target::tool vendor near\", 6.361f), NULL)));" "the trigger")

message(STATUS "#333 P1-y near tool source contract passed")
