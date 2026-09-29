if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#408 (train 8): the quest relation migration only adds (id, quest) pairs,
# replay-safe, and never adds a starter for a quest without a spawned turn-in.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260929190000_world.sql" migration)

foreach (forbidden "DELETE " "UPDATE " "REPLACE " "TRUNCATE " "DROP ")
  string(FIND "${migration}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Relation migration must be insert-only, found: ${forbidden}")
  endif()
endforeach()

string(REGEX MATCHALL "INSERT INTO" plain_inserts "${migration}")
list(LENGTH plain_inserts plain_count)
if (NOT plain_count EQUAL 0)
  message(FATAL_ERROR "Every insert must be INSERT IGNORE (replay safety)")
endif()

string(REGEX MATCHALL "\n[(][0-9]+, +[0-9]+[)]" pairs "${migration}")
list(LENGTH pairs pair_count)
if (NOT pair_count EQUAL 64)
  message(FATAL_ERROR "Expected 29 starters + 35 turn-ins = 64 pairs, found ${pair_count}")
endif()

# Quests whose turn-in NPC is not spawned yet stay without a starter (train 9).
foreach (held "40821)" "41921)")
  string(FIND "${migration}" "${held}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Quest ${held} must not get a relation before its turn-in is spawned")
  endif()
endforeach()

message(STATUS "QUEST_RELATIONS_408_CONTRACT=PASS")
