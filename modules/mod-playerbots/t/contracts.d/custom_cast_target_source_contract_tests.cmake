cmake_policy(VERSION 3.16)
# twow-repo#741 (AUDIT-L03-5, R2 bug fix, no switch): CastCustomSpellAction targets the bot only for item-target spells and for
# spells whose targets all start at the caster (SpellEntry::IsSpellWithCasterSourceTargetsOnly, the core's reading). The dead
# `Targets & TARGET_FLAG_SELF` test (TARGET_FLAG_SELF is 0) must not come back, and `Targets == 0` must not mean "self"
# (Fireball has Targets 0). Pins the policy, the call and the include.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "custom cast target: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "custom cast target: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "custom cast target: forbidden ${description}: ${needle}")
  endif()
endfunction()

read_source("CustomCastTargetPolicy.h" policy)
require_text("${policy}" "inline bool UseBotAsTarget(bool itemTargetFlag, bool casterSourceTargetsOnly)" "the policy function")
require_text("${policy}" "return itemTargetFlag || casterSourceTargetsOnly;" "the policy rule")

read_source("strategy/actions/CastCustomSpellAction.cpp" cast)
require_text("${cast}" "#include \"playerbot/CustomCastTargetPolicy.h\"" "the policy include")
require_text("${cast}"
  "if (ai::custom_cast_target::UseBotAsTarget((pSpellInfo->Targets & TARGET_FLAG_ITEM) != 0, pSpellInfo->IsSpellWithCasterSourceTargetsOnly()))"
  "the caster-source call")
forbid_text("${cast}" "& TARGET_FLAG_SELF" "the dead self flag test (TARGET_FLAG_SELF is 0)")
forbid_text("${cast}" "Targets == 0" "Targets 0 read as self")

# The target choice must come before the cast check that uses it.
string(FIND "${cast}" "ai::custom_cast_target::UseBotAsTarget(" choice)
string(FIND "${cast}" "const bool canCast = " cancast)
if(choice EQUAL -1 OR cancast EQUAL -1 OR NOT choice LESS cancast)
  message(FATAL_ERROR "custom cast target: the target choice must come before the cast check")
endif()

message(STATUS "CUSTOM_CAST_TARGET_CONTRACT=PASS")
