function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/ReleaseSpiritAction.h" release)

# #277: "no corpse" must no longer be read as "alive".
reject_text("${release}" "sServerFacade.IsAlive(bot) || !bot->GetCorpse()" "no-corpse treated as alive")
require_text("${release}" "corpse_run::Classify(sServerFacade.IsAlive(bot)" "corpse run state classification")
require_text("${release}" "state == corpse_run::State::DeadUnreleased" "release-first branch")
require_text("${release}" "HandleRepopRequestOpcode(packet);" "release uses the normal repop request")
require_text("${release}" "SET_AI_VALUE(bool, \"corpse run\", true);" "corpse run flag still set")
