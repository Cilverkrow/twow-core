if(NOT DEFINED PB_SOURCE_DIR OR NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "PB_SOURCE_DIR and TW_CORE_ROOT are required")
endif()

# twow-repo#301: leave reads the live group and disbands a self-led singleton
# through the normal Core path; Core still disbands a group below two members.
file(READ "${PB_SOURCE_DIR}/strategy/actions/LeaveGroupAction.cpp" leave)
file(READ "${TW_CORE_ROOT}/src/game/Group/Group.cpp" group)
file(READ "${TW_CORE_ROOT}/src/game/Handlers/GroupHandler.cpp" handler)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}")
  endif()
endfunction()

require_text("${leave}" "group->GetMembersCount(), group->IsLeader(bot->GetObjectGuid()), group->isBGGroup()" "singleton detection from the live group")
require_text("${leave}" "singleton_group::ShouldStayInGroup(" "stay rule from the tested policy")
forbid_text("${leave}" "bool shouldStay = freeBot && bot->GetGroup() && player == bot;" "old stay rule that stranded singletons")
require_text("${leave}" "HandleGroupDisbandOpcode(p)" "leave through the normal Core handler")
foreach(forbidden "->Disband(" "CharacterDatabase" "DELETE FROM")
  forbid_text("${leave}" "${forbidden}" "direct group or database mutation ${forbidden}")
endforeach()

# Premise in Core: leaving a group below the minimum disbands it.
require_text("${handler}" "GetPlayer()->RemoveFromGroup();" "leave removes the player from the group")
require_text("${group}" "if (GetMembersCount() > GetMembersMinCount())" "member-count branch")
require_text("${group}" "Disband(true, guid);" "disband below the minimum")

message(STATUS "SINGLETON_GROUP_SOURCE_CONTRACT=PASS")
