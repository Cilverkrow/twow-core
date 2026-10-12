# twow-repo#541 (audit A18): behind AiPlayerbot.Perf.TravelRequestGate (default 0 = off) the travel request
# triggers of TravelStrategy become "travel request::<condition>" triggers: same qualifier (trigger name, event
# source, stored travel condition), but <condition> is not read while the travel target is prepared or active,
# the two states in which RequestTravelTargetAction::isUseful rejects every request anyway. CHANGES BEHAVIOUR
# (gather purpose start timing, no stale request baskets): enable only after the OB-30 measurement and the
# owner's go. Switch 0 keeps every "val::" trigger name. Pure policy: t/travel_request_policy_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A18: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A18: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A18: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A18: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#541 A18: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelRequestPolicy.h" policy_h)
read_source("strategy/generic/TravelStrategy.cpp" strategy_cpp)
read_source("strategy/triggers/TravelTriggers.h" triggers_h)
read_source("strategy/triggers/TravelTriggers.cpp" triggers_cpp)
read_source("strategy/triggers/TriggerContext.h" context_h)
read_source("strategy/triggers/GenericTriggers.h" generic_h)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("strategy/values/TravelValues.cpp" values_cpp)

# 1. Switch off by default, documented.
require_text("${config_h}" "bool perfTravelRequestGate = false;" "member default off")
require_text("${config_cpp}" "perfTravelRequestGate = config.GetBoolDefault(\"AiPlayerbot.Perf.TravelRequestGate\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Perf.TravelRequestGate = 0" "documented key")

# 2. Strategy: the switch only picks the trigger names; the off path keeps every "val::" name.
function_body("${strategy_cpp}" "void TravelStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)" init)
require_text("${strategy_cpp}" "#include \"playerbot/TravelRequestPolicy.h\"" "policy include")
require_text("${init}" "bool const gateRequests = sPlayerbotAIConfig.perfTravelRequestGate;" "switch read at strategy init")
require_text("${init}" "\"val::need travel purpose::\" + std::to_string((uint32)purpose)" "off-path purpose trigger name")
require_text("${init}" "travel_request::TriggerName(trigger, gateRequests)," "purpose triggers through the policy")
require_text("${init}" "travel_request::TriggerName(trigger, gateRequests && travel_request::IsRequestAction(action))," "named triggers gated only for request actions")
require_text("${init}" "{\"val::not::travel target active\",\"refresh travel target\", 6.7f}" "refresh row unchanged")
require_text("${init}" "{\"val::not::travel target active\",\"choose group travel target\", 6.65f}" "choose group row unchanged")
require_text("${init}" "NextAction(\"choose travel target\", 6.98f)" "choose travel target unchanged")
require_text("${init}" "NextAction(\"zone escape\", 7.0f)" "zone escape unchanged")
forbid_text("${init}" "\"travel request::" "hard-coded gated name (gated names only come from the policy)")

# 3. Policy is pure (no state, no game headers).
require_text("${policy_h}" "namespace ai::travel_request" "policy namespace")
require_text("${policy_h}" "if (!gate || valueTrigger.compare(0, 5, \"val::\") != 0)" "off or non-val name unchanged")
require_text("${policy_h}" "return \"travel request::\" + valueTrigger.substr(5);" "prefix swap keeps the qualifier")
require_text("${policy_h}" "return action.compare(0, 8, \"request \") == 0;" "request actions only")
require_text("${policy_h}" "return !prepare && !targetActive;" "gate mirrors isUseful")
forbid_text("${policy_h}" "static " "static state in the policy")
forbid_text("${policy_h}" "#include \"playerbot/" "game include in the policy")

# 4. Trigger: registered next to "val", read-only gate before the condition read.
require_text("${context_h}" "creators[\"travel request\"] = [](PlayerbotAI* ai) { return new TravelRequestTrigger(ai); };" "travel request creator")
require_text("${context_h}" "creators[\"val\"] = [](PlayerbotAI* ai) { return new ValueTrigger(ai); };" "val creator unchanged")
require_text("${triggers_h}" "class TravelRequestTrigger : public Trigger, public Qualified" "trigger class")
require_text("${triggers_h}" "TravelRequestTrigger(PlayerbotAI* ai) : Trigger(ai, \"travel request\", 1), Qualified() {}" "trigger constructor (check interval 1 as val)")
require_text("${triggers_cpp}" "#include \"playerbot/TravelRequestPolicy.h\"" "policy include in the trigger")
function_body("${triggers_cpp}" "bool TravelRequestTrigger::IsActive()" gate)
require_order("${gate}" "name = getQualifier();" "TravelStatus::TRAVEL_STATUS_PREPARE" "name set before the gate")
require_order("${gate}" "TravelStatus::TRAVEL_STATUS_PREPARE" "AI_VALUE(bool, \"travel target active\")" "prepare before active (isUseful order)")
require_order("${gate}" "if (!travel_request::MayCheck(prepare, active))" "return AI_VALUE(bool, getQualifier());" "gate before the condition read")
require_text("${gate}" "bool const prepare = target && target->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE;" "null-safe prepare check")
string(FIND "${gate}" "if (!travel_request::MayCheck(prepare, active))" gate_at)
string(SUBSTRING "${gate}" 0 ${gate_at} before_gate)
forbid_text("${before_gate}" "getQualifier())" "condition read before the gate")
forbid_text("${gate}" "SET_AI_VALUE" "value write in the trigger")
forbid_text("${gate}" "RESET_AI_VALUE" "value reset in the trigger")
forbid_text("${gate}" "static " "static state in the trigger")
forbid_text("${gate}" "perfTravelRequestGate" "switch read per tick (decided at strategy init only)")

# 5. The gate mirrors isUseful; ValueTrigger and the values stay untouched.
function_body("${choose_cpp}" "bool RequestTravelTargetAction::isUseful() {" useful)
require_order("${useful}" "TravelStatus::TRAVEL_STATUS_PREPARE" "if (AI_VALUE(bool, \"travel target active\"))" "isUseful rejects prepare and active")
require_text("${generic_h}" "virtual bool IsActive() override { name = getQualifier();  return AI_VALUE(bool, getQualifier()); }" "ValueTrigger unchanged")
forbid_text("${generic_h}" "perfTravelRequestGate" "switch in GenericTriggers.h")
function_body("${values_cpp}" "bool NeedTravelPurposeValue::Calculate()" need)
forbid_text("${need}" "travel target active" "gate in the value (TravelTarget::IsConditionsActive reads it)")
forbid_text("${need}" "perfTravelRequestGate" "switch in the value")
require_text("${need}" "declared.Start(skill, value, target, now);" "gather purpose start stays in Calculate (A18 does not move it)")

message(STATUS "travel_request_gate source contract passed")
