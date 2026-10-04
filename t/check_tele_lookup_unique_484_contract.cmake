if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#484 (hotfix 8.19): `.tele the barrens` searched only "the" and took the first of ~33
# substring matches in hash order (e.g. the Turtle development island, map 451). A plain name now
# takes the whole rest of the command; a substring match counts only when it is unique.
file(READ "${TW_CORE_ROOT}/src/game/ObjectMgr.cpp" om)
string(FIND "${om}" "GameTele const* ObjectMgr::GetGameTele(std::string const& name, uint32* matches) const" begin)
if (begin EQUAL -1)
  message(FATAL_ERROR "GetGameTele(name, matches) not found")
endif()
string(SUBSTRING "${om}" ${begin} 2200 body)
foreach (required
    "if (itr.second.wnameLow == wname)"
    "return found == 1 ? alt : nullptr;")
  string(FIND "${body}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "GetGameTele must keep the exact match and accept only a unique substring: ${required}")
  endif()
endforeach()
string(FIND "${body}" "alt == nullptr &&" first_match)
if (NOT first_match EQUAL -1)
  message(FATAL_ERROR "GetGameTele must not take the first substring match again")
endif()

file(READ "${TW_CORE_ROOT}/src/game/Chat/Chat.cpp" chat)
string(FIND "${chat}" "GameTele const* ChatHandler::ExtractGameTeleFromLink(char** text)" begin)
if (begin EQUAL -1)
  message(FATAL_ERROR "ExtractGameTeleFromLink not found")
endif()
string(SUBSTRING "${chat}" ${begin} 2000 body)
foreach (required
    "if (*p != ' ' && *p != '\"')"
    "sObjectMgr.GetGameTele(name, &matches)"
    "is ambiguous")
  string(FIND "${body}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "ExtractGameTeleFromLink must join the whole name and report ambiguity: ${required}")
  endif()
endforeach()

message(STATUS "TELE_LOOKUP_UNIQUE_484_CONTRACT=PASS")
