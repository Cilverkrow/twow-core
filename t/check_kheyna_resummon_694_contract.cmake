if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#694: quest 80396 "A Bloody Good Deed" has a single giver, Kheyna Spinpistol 81041, who is summoned by the
# Mysterious Glittering Object for 60 s. After 80395 is turned in, 80396 is NONE until it is accepted; the object must
# summon her in that state too, or a declined offer locks 80396 for good.
file(READ "${TW_CORE_ROOT}/src/scripts/miscellaneous/random_scripts_1.cpp" script)

string(FIND "${script}" "// A Glittering Opportunity" cond_at)
if (cond_at EQUAL -1)
  message(FATAL_ERROR "Missing the Kheyna summon condition (A Glittering Opportunity)")
endif()
# The condition and its helper flag sit in the few lines before the marker comment.
math(EXPR from "${cond_at} - 700")
if (from LESS 0)
  set(from 0)
endif()
string(SUBSTRING "${script}" ${from} 900 block)

foreach(needle
    "GetQuestRewardStatus(80395)"
    "GetQuestStatus(80396) == QUEST_STATUS_NONE"
    "|| offer80396Again)"
    "SummonCreature(81041")
  string(FIND "${block}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Kheyna summon condition misses: ${needle}")
  endif()
endforeach()

message(STATUS "KHEYNA_RESUMMON_694_CONTRACT=PASS")
