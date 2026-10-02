if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#482 (test 2026-10-02): the 1.12 client keeps loot slots in a fixed array of 16.
# A SMSG_LOOT_RESPONSE with more entries or a slot index >= 16 damaged the client heap
# (two crashes). Every slot write in the loot writer must pass the 16-slot guard.
file(READ "${TW_CORE_ROOT}/src/game/LootMgr.h" loot_h)
string(FIND "${loot_h}" "#define MAX_NR_LOOT_CLIENT_SLOTS 16" at)
if (at EQUAL -1)
  message(FATAL_ERROR "MAX_NR_LOOT_CLIENT_SLOTS must stay 16 (client array size)")
endif()

file(READ "${TW_CORE_ROOT}/src/game/LootMgr.cpp" loot_cpp)
string(FIND "${loot_cpp}" "ByteBuffer& operator<<(ByteBuffer& b, LootView const& lv)" begin)
string(FIND "${loot_cpp}" "b.put<uint8>(count_pos, itemsShown);" end)
if (begin EQUAL -1 OR end EQUAL -1 OR NOT begin LESS end)
  message(FATAL_ERROR "Loot writer not found")
endif()
math(EXPR len "${end} - ${begin}")
string(SUBSTRING "${loot_cpp}" ${begin} ${len} writer)

string(FIND "${writer}" "if (slot < MAX_NR_LOOT_CLIENT_SLOTS && itemsShown < MAX_NR_LOOT_CLIENT_SLOTS)" guard_at)
if (guard_at EQUAL -1)
  message(FATAL_ERROR "clientSlotOk must reject index >= 16 and a 17th entry")
endif()

# CMake lists split at ';', so the writer text uses '#' for ';' before matching.
string(REPLACE ";" "#" writer "${writer}")
set(slot_write "b << uint8[(](i|l[.]items[.]size[(][)] [+] [(]qi - q_list->begin[(][)][)]|fi[.]index|ci[.]index)[)]")
string(REGEX MATCHALL "${slot_write}" writes "${writer}")
string(REGEX MATCHALL "if [(]!clientSlotOk[(][^\n]*[)][)]\n[ \t]*continue#\n[ \t]*${slot_write}" guarded "${writer}")
list(LENGTH writes write_count)
list(LENGTH guarded guarded_count)
if (NOT write_count EQUAL 6 OR NOT guarded_count EQUAL write_count)
  message(FATAL_ERROR "Every loot slot write needs the client slot guard: ${guarded_count} of ${write_count} (expected 6)")
endif()

# Quest items sit behind `items` and must stay inside the client slots as well.
string(REGEX MATCHALL "MAX_NR_LOOT_CLIENT_SLOTS" quest_caps "${loot_cpp}")
list(LENGTH quest_caps cap_count)
if (cap_count LESS 4)
  message(FATAL_ERROR "FillQuestLoot/MoveExcessToOverflow must cap at MAX_NR_LOOT_CLIENT_SLOTS")
endif()

message(STATUS "LOOT_CLIENT_SLOTS_482_CONTRACT=PASS")
