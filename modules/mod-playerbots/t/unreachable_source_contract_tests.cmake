function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ReachTargetActions.h" reach)
file(READ "${PB_SOURCE_DIR}/strategy/values/GrindTargetValue.cpp" grind)

# Hotfix 8.2: unreachable targets dropped and skipped, skill-ups count as progress.
require_text("${reach}" "if (!isFriend && ai->CheckReachProgress(target, distanceToTarget))" "reach progress checked")
require_text("${reach}" "if (ai->IsUnreachableTarget(target->GetObjectGuid()))" "no reach to an ignored target")
require_text("${grind}" "if (ai->IsUnreachableTarget(unit->GetObjectGuid()))" "grind skips ignored targets")
require_text("${ai_cpp}" "if (!target || HasRealPlayerMaster())" "bots without a real player only")
require_text("${ai_cpp}" "if (!lastUnreachableLog || now - lastUnreachableLog >= ai::unreachable::LogSeconds)" "log budget")
require_text("${ai_cpp}" "snapshot += uint64(bot->GetSkillValue(skill))" "skill-ups are progress")
