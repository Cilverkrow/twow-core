# twow-repo#629 G1 (CLI-GRP 26.10.2026): the stock auto release asks the release veto (playerbot/ReleaseVeto.h) after its
# own dead/ghost checks and before any group rule, so a module (DC: DungeonClear.StayDeadInRun) can keep a dead run member
# a corpse in the instance - without overriding the stock action name (the 22.08. map-pool crash path).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "release veto: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_source("strategy/actions/ReleaseSpiritAction.h" release)
read_source("ReleaseVeto.cpp" veto_cpp)

# Inside AutoReleaseSpiritAction::isUseful: after the ghost check, before the group rules.
require_order("${release}" "            if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                return false;" "            if (ai::release_veto::Vetoed(bot))
                return false;" "veto after the ghost check")
require_order("${release}" "            if (ai::release_veto::Vetoed(bot))
                return false;" "            if (!bot->GetGroup())
                return true;" "veto before the group rules")
# Out of line, in the playerbots library: one atomic predicate, nothing inline a module could compile differently.
require_order("${veto_cpp}" "std::atomic<Predicate> g_predicate{nullptr};" "bool Vetoed(Player* bot)" "out-of-line atomic registry")
