if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#694: the Mysterious Glittering Object (3000284) must also summon Kheyna Spinpistol (81041)
# after 80395 was turned in while 80396 is not yet accepted; otherwise 80396 is lost for good.
file(READ "${TW_CORE_ROOT}/src/scripts/miscellaneous/random_scripts_1.cpp" s)
string(FIND "${s}" "// A Glittering Opportunity" at)
if (at EQUAL -1)
  message(FATAL_ERROR "#694: Kheyna summon condition not found")
endif()
string(SUBSTRING "${s}" 0 ${at} head)
string(FIND "${head}" "if (" cond_at REVERSE)
math(EXPR cond_len "${at} - ${cond_at}")
string(SUBSTRING "${s}" ${cond_at} ${cond_len} cond)
foreach (required
    "pPlayer->GetQuestStatus(80395) == QUEST_STATUS_INCOMPLETE"
    "pPlayer->GetQuestStatus(80395) == QUEST_STATUS_COMPLETE"
    "pPlayer->GetQuestStatus(80396) == QUEST_STATUS_COMPLETE"
    "(pPlayer->GetQuestRewardStatus(80395) && pPlayer->GetQuestStatus(80396) == QUEST_STATUS_NONE)")
  string(FIND "${cond}" "${required}" found)
  if (found EQUAL -1)
    message(FATAL_ERROR "#694: Kheyna summon condition misses: ${required}")
  endif()
endforeach()
message(STATUS "KHEYNA_694_CONTRACT=PASS")
