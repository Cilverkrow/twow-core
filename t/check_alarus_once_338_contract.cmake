if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

# twow-repo#338 (owner 2026-09-30, bosses never return within an instance id): Alarus' defeat is
# instance data that is saved to the DB, and the summoning trigger checks it before anything else.
file(READ "${TW_CORE_ROOT}/src/scripts/dungeons/karazhan_crypt/instance_karazhan_crypt.cpp" script)

require_text("${script}" "TYPE_ALARUS = 1" "instance data slot")
require_text("${script}" "SetData(TYPE_ALARUS, DONE);" "defeat recorded on death")
require_text("${script}" "SaveToDB();" "state saved to the DB")
require_text("${script}" "const char* Save() override" "save hook")
require_text("${script}" "void Load(const char* chrIn) override" "load hook")

string(FIND "${script}" "struct trigger_summon_alarusAI" trigger_at)
string(SUBSTRING "${script}" ${trigger_at} -1 trigger)
string(FIND "${trigger}" "GetData(TYPE_ALARUS) == DONE" guard_at)
string(FIND "${trigger}" "SummonCreature(91928" summon_at)
if (guard_at EQUAL -1 OR summon_at EQUAL -1 OR NOT guard_at LESS summon_at)
  message(FATAL_ERROR "The Alarus trigger must check the saved defeat before it summons him")
endif()

message(STATUS "ALARUS_ONCE_338_CONTRACT=PASS")
