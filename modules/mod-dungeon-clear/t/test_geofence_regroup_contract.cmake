# CLI-GRP G1 dry-r3/pilot 25.10.2026 (Refs twow-repo#629): the geo fence teleported a member that the stock death-loop
# "repop" had removed from the group straight back - without the group bind it landed in a NEW instance copy (102 next to
# the tank's 101) and was lost to the run, while the fence kept re-teleporting it. Now: back into the run's group (and its
# personal bind to another copy dropped) BEFORE the teleport. Plus OB-10's #686 follow-up: the pool claim records
# guildBefore too. Source contract (the job is core-coupled, not in the fast suite).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

file(READ "${DC_SOURCE_DIR}/src/TestRun/DcTestRunJob.cpp" job)
string(REPLACE "\r" "" job "${job}")

function(require_order first second description)
  string(FIND "${job}" "${first}" a)
  string(FIND "${job}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "geo fence regroup: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

require_order("                                if (runGroup && !runGroup->IsMember(bot->GetObjectGuid()))"
              "                                bot->TeleportTo(tankMap->GetId(), tank->GetPositionX(),"
              "regroup before the teleport")
require_order("                                    bool const back = runGroup->AddMember(bot->GetObjectGuid(), bot->GetName());"
              "                                    sInstanceSaveMgr->PlayerUnbindInstance(bot->GetObjectGuid(), _mapId,"
              "re-added, then its own bind to another copy dropped")
# Pre-review: the core's RemoveFromGroup (it deletes a group the removal emptied), and no unbind/teleport when the re-add
# failed (a groupless teleport opens yet another copy).
require_order("                                    if (bot->GetGroup())
                                        bot->RemoveFromGroup();"
              "                                    bool const back = runGroup->AddMember(bot->GetObjectGuid(), bot->GetName());"
              "leave any other group through the core's wrapper first")
require_order("                                    if (!back)
                                        continue;"
              "                                    sInstanceSaveMgr->PlayerUnbindInstance(bot->GetObjectGuid(), _mapId,"
              "no unbind or teleport after a failed re-add")
string(FIND "${job}" "other->RemoveMember(bot->GetObjectGuid(), 0);" raw)
if(NOT raw EQUAL -1)
  message(FATAL_ERROR "geo fence regroup: raw RemoveMember is back (leaks an emptied group)")
endif()
require_order("        slot.guid = guid;"
              "        slot.guildBefore = sCharacterCache->GetCharacterGuildIdByGuid(guid);"
              "pool claim records guildBefore")
