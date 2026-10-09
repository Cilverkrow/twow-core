# twow-repo#541 (OB-00 go 10.10.2026, measurement only): with AiPlayerbot.BotUpdateTrace the engine trace
# carries the microseconds of the value-update + trigger phase ("TIME:values Nus triggers Nus") and of each
# evaluated action ("A:<name> - <outcome> Nus"), so a [BotUpdateSlow] line attributes its time. Off: no clock
# read and the old trace text ("A:<name> - <outcome>").

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 trace us: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 trace us: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("strategy/Engine.cpp" engine)
read_source("BotUpdateTrace.h" trace)

string(FIND "${engine}" "bool Engine::DoNextAction(" start)
string(SUBSTRING "${engine}" ${start} -1 rest)
string(FIND "${rest}" "\nvoid Engine::" end)
string(SUBSTRING "${rest}" 0 ${end} walk)

require_text("${walk}" "bool const traceUs = sPlayerbotAIConfig.botUpdateTrace;" "same switch as [BotUpdate]")
# No clock read with the switch off.
require_text("${walk}" "std::uint64_t const phaseStartUs = traceUs ? ai::bot_update::NowUs() : 0;" "phase clock behind the switch")
require_text("${walk}" "std::uint64_t const actionStartUs = traceUs ? ai::bot_update::NowUs() : 0;" "action clock behind the switch")
require_order("${walk}" "std::uint64_t const phaseStartUs" "aiObjectContext->Update();" "phase starts before the value update")
require_order("${walk}" "ProcessTriggers(minimal);" "LogAction(\"TIME:values %lluus triggers %lluus\"" "phase entry after the triggers")
require_order("${walk}" "if (traceUs)\n        LogAction(\"TIME:values" "PushDefaultActions();" "phase entry only with the switch")
# Off: the old text exactly.
require_text("${walk}" "else\n            LogAction(\"A:%s - %s\", action->getName().c_str(), outcome);" "old outcome text when off")
require_text("${walk}" "LogAction(\"A:%s - %s %lluus\", action->getName().c_str(), outcome, (unsigned long long)ai::bot_update::SinceUs(startUs));" "us when on")
foreach(outcome PREREQ OK FAILED IMPOSSIBLE USELESS)
  require_text("${walk}" "logOutcome(action, \"${outcome}\", actionStartUs);" "outcome ${outcome} through logOutcome")
  string(FIND "${walk}" "LogAction(\"A:%s - ${outcome}\"" direct)
  if(NOT direct EQUAL -1)
    message(FATAL_ERROR "#541 trace us: ${outcome} still logged without the timing helper")
  endif()
endforeach()
require_order("${walk}" "std::uint64_t const actionStartUs" "Action* action = InitializeAction(actionNode);" "action clock includes InitializeAction")

require_text("${trace}" "std::chrono::steady_clock::now()" "monotonic clock")
require_text("${trace}" "return now > startUs ? now - startUs : 0;" "no underflow")

message(STATUS "trace_action_us source contract passed")
