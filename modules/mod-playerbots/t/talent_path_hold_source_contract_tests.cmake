function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/ChangeTalentsAction.cpp" talents)

# A bot whose specNo names an existing premade path continues that path and is
# never re-rolled (7.1/7.3, 4.0/4.3, feral/bear share their links; #357, #367).
require_text("${talents}" "if (path.id == int(specId))" "exact path match (getPremadePath falls back to path 0)")
require_text("${talents}" "if (!onKnownPath && (bot->CalculateTalentsPoints() > 0 || (!specNo && specLink.empty())))" "known path not re-rolled")
