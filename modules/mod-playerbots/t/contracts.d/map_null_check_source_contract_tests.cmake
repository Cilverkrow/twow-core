cmake_policy(VERSION 3.16)
# twow-repo#722 (core crash in RandomPlayerbotMgr::QuestRescueTargets that took down the n4 realm, 18.11.2026): WorldObject::GetMap()
# runs MANGOS_ASSERT(m_currMap) before it returns (Object.cpp), so `x->GetMap() && ...` or `!x->GetMap()` is not a null check - a bot
# without a map (far teleport, login, logout) aborts the world thread. FindMap() returns m_currMap without the assert.
# Pins (source text, no proof): the sites named in #722 use FindMap() for the null check, the quest rescue scan skips a bot that is
# not in the world or is being teleported far BEFORE it takes the request, and no `GetMap() &&` / `!...GetMap()` shape is left in the
# files that were fixed. Haystacks get \r\n normalised to \n.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "map null check: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r\n" "\n" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "map null check: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_regex text regex description)
  if("${text}" MATCHES "${regex}")
    message(FATAL_ERROR "map null check: forbidden ${description} (/${regex}/): ${CMAKE_MATCH_0}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "map null check: wrong order or missing (${description}): '${first}' before '${second}'")
  endif()
endfunction()

# the shapes that look like a null check but assert first
set(NULLCHECK_BAD [=[[A-Za-z_]+->GetMap[(][)] *&& *[A-Za-z_]+->GetMap[(][)]|![A-Za-z_]+->GetMap[(][)] *([|][|]|[)])|[A-Za-z_]+->GetMap[(][)] *[?]]=])

foreach(file RandomPlayerbotMgr.cpp PlayerbotAI.cpp PlayerbotMgr.cpp PlayerbotSecurity.cpp QuestBand.cpp SupervisorGroup.cpp strategy/actions/LootRollAction.cpp)
  read_source("${file}" src)
  forbid_regex("${src}" "${NULLCHECK_BAD}" "an asserting GetMap() null check in ${file}")
endforeach()

read_source("RandomPlayerbotMgr.cpp" mgr)
require_text("${mgr}" "own && bot->FindMap() && bot->FindMap()->IsContinent();" "the FindMap null check in QuestRescueTargets")
require_text("${mgr}" "if (bot && (!bot->IsInWorld() || bot->IsBeingTeleportedFar()))\n            continue;" "the scan guard in ProcessQuestRescues")
string(FIND "${mgr}" "void RandomPlayerbotMgr::ProcessQuestRescues()" scan_at)
if(scan_at EQUAL -1)
  message(FATAL_ERROR "map null check: ProcessQuestRescues not found")
endif()
string(SUBSTRING "${mgr}" ${scan_at} -1 scan_body)
require_order("${scan_body}" "!bot->IsInWorld() || bot->IsBeingTeleportedFar()" "ai->TakeQuestRescueRequest()" "guard before the request is taken")
require_order("${scan_body}" "!bot->IsInWorld() || bot->IsBeingTeleportedFar()" "ai->HasQuestRescueRequest()" "guard before the keep-request break")

read_source("PlayerbotAI.cpp" pai)
require_text("${pai}" "step == ai::stuck_combat::Step::Rescue && bot->FindMap() && bot->FindMap()->IsContinent() &&" "FindMap in the stuck rescue gate")
read_source("QuestBand.cpp" qb)
require_text("${qb}" "|| !bot->FindMap() || !bot->FindMap()->IsContinent() ||" "FindMap in the idle rescue gate")

message(STATUS "map null check: all checks passed")
