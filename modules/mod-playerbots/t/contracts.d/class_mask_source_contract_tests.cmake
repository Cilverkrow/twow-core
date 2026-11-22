cmake_policy(VERSION 3.16)
# twow-repo#742 (AUDIT-L03-6, R2 bug fix, no switch): item_template.AllowableClass is a class mask. ItemUsageValue (class
# consumables) and RandomItemMgr (stat-less class trinkets) read it through ClassMaskPolicy.h: restricted masks only, -1 and
# all-class masks are not class items, 0 is nobody. Pins the policy, both call sites and forbids the class-ID compares.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "class mask: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "class mask: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "class mask: forbidden ${description}: ${needle}")
  endif()
endfunction()

read_source("ClassMaskPolicy.h" policy)
require_text("${policy}" "inline bool IsRestrictedTo(std::uint32_t allowableClass, std::uint32_t classMask, std::uint32_t allPlayableMask)" "the policy function")
require_text("${policy}" "if ((allowableClass & allPlayableMask) == allPlayableMask)
        return false;" "the all-class guard (-1 is not a class item)")
require_text("${policy}" "return (allowableClass & classMask) != 0;" "the mask test")

read_source("strategy/values/ItemUsageValue.cpp" usage)
require_text("${usage}" "#include \"playerbot/ClassMaskPolicy.h\"" "the ItemUsageValue include")
require_text("${usage}" "if (ai::class_mask::IsRestrictedTo(proto->AllowableClass, bot->getClassMask(), CLASSMASK_ALL_PLAYABLE) && proto->RequiredLevel + 6 >= bot->GetLevel())" "the class consumable call")
forbid_text("${usage}" "proto->AllowableClass == bot->getClass()" "the class-ID compare")

read_source("RandomItemMgr.cpp" rim)
require_text("${rim}" "#include \"playerbot/ClassMaskPolicy.h\"" "the RandomItemMgr include")
require_text("${rim}" "if (statW == 0 && ai::class_mask::IsRestrictedTo(proto->AllowableClass, 1u << (clazz - 1), CLASSMASK_ALL_PLAYABLE) && proto->Spells[0].SpellId)" "the class trinket call")
forbid_text("${rim}" "proto->AllowableClass == uint32(clazz)" "the class-ID compare")

message(STATUS "CLASS_MASK_CONTRACT=PASS")
