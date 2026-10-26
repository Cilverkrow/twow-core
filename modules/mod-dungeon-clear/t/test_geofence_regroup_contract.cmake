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
# The failed re-add block ends in continue - before any unbind or teleport (pre-review: pinning only "if (!back) {" let a
# deleted continue pass, i.e. the #708 bug).
require_order("                                    if (!back)
                                    {"
              "                                        continue;
                                    }
                                    // A personal bind"
              "failed re-add ends in continue")
require_order("                                        continue;
                                    }
                                    // A personal bind"
              "                                    sInstanceSaveMgr->PlayerUnbindInstance(bot->GetObjectGuid(), _mapId,"
              "no unbind or teleport after a failed re-add")
# #708 follow-up (OB-10 26.10.): the re-add is throttled per member (DcRunRobust::RegroupMayTry, gtest), checked BEFORE the
# member leaves another group; the streak starts over when the member is back in the group by any other way; 5 failures
# end the run right after the sweep in either tick.
require_order("                                    if (!DcRunRobust::RegroupMayTry(rg, nowRg))
                                        continue;"
              "                                    if (bot->GetGroup())
                                        bot->RemoveFromGroup();"
              "throttle before leaving any other group")
require_order("                                    bool const back = runGroup->AddMember(bot->GetObjectGuid(), bot->GetName());"
              "                                    bool const giveUp = DcRunRobust::RegroupRecord(rg, back, nowRg);"
              "every attempt recorded")
require_order("                                else if (runGroup)" "                                    _regroup.erase(slot.guid.GetCounter());" "streak reset when back in the group")
require_order("void DcTestRunJob::TickStarting()" "    SweepPartyGeometry();
    if (!_regroupGiveUp.empty())" "give-up checked right after the sweep (starting)")
require_order("    if (!_regroupGiveUp.empty())   // #708 follow-up: a member that cannot rejoin the run's group ends the set-up
    {
        FailSetup(_regroupGiveUp);
        return;" "void DcTestRunJob::TickMonitoring(uint32 dt)" "give-up ends the set-up")
require_order("void DcTestRunJob::TickMonitoring(uint32 dt)" "    SweepPartyGeometry();
    if (!_regroupGiveUp.empty())   // #708 follow-up: a member that cannot rejoin the run's group ends the run
    {
        Finish(DcTestRun::Verdict::FailAborted, _regroupGiveUp);
        return;" "give-up ends the run right after the sweep (monitoring)")
string(FIND "${job}" "other->RemoveMember(bot->GetObjectGuid(), 0);" raw)
if(NOT raw EQUAL -1)
  message(FATAL_ERROR "geo fence regroup: raw RemoveMember is back (leaks an emptied group)")
endif()
require_order("        slot.guid = guid;"
              "        slot.guildBefore = sCharacterCache->GetCharacterGuildIdByGuid(guid);"
              "pool claim records guildBefore")
