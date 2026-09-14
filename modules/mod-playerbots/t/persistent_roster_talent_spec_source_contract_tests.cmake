if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

file(READ "${PB_SOURCE_DIR}/PlayerbotFactory.cpp" factory)
file(READ "${PB_SOURCE_DIR}/PersistentRosterTalentSpecPolicy.h" policy)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

require_text("${factory}" "#include \"playerbot/PersistentRosterTalentSpecPolicy.h\""
  "standalone stored-spec policy")
require_text("${factory}" "uint32 storedSpecNo = sRandomPlayerbotMgr.GetValue(bot->GetGUIDLow(), \"specNo\");"
  "stored spec lookup")
require_text("${factory}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())"
  "existing persistent-roster gate")
require_text("${factory}" "ai::roster::talents::KeepStoredSpecNo("
  "stored-spec validation")
require_text("${factory}" "return true;"
  "valid roster spec preservation")
require_text("${policy}" "static_cast<std::uint32_t>(path.id) + 1 == storedSpecNo"
  "specNo equals premade id plus one")

string(FIND "${factory}" "ai::roster::talents::KeepStoredSpecNo(" retain_offset)
string(FIND "${factory}" "uint32 totalProbability = 0;" weighted_offset)
string(FIND "${factory}" "urand(0, totalProbability - 1)" roll_offset)
if(retain_offset EQUAL -1 OR weighted_offset EQUAL -1 OR roll_offset EQUAL -1 OR
   retain_offset GREATER weighted_offset OR weighted_offset GREATER roll_offset)
  message(FATAL_ERROR "Roster retention must precede the unchanged weighted fallback")
endif()

message(STATUS "PERSISTENT_ROSTER_TALENT_SPEC_SOURCE_CONTRACT=PASS")
