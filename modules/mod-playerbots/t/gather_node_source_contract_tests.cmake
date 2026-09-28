function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)

# #414: gather destinations are real herb/ore nodes only.
require_text("${travel_mgr}" "#include \"playerbot/GatherNodePolicy.h\"" "gather node policy")
require_text("${travel_mgr}" "ai::gather_node::IsGatherNode(goInfo->type == GAMEOBJECT_TYPE_CHEST, lockHasNonProfessionSkill)" "chest type and profession-only lock required")
require_text("${travel_mgr}" "SkillByLockType(LockType(lockInfo->Index[i])) == 0" "non-profession lock slot detected")

file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
require_text("${choose}" "ai::gather_node::StaysOnMap((uint32(destination->GetPurpose()) & gatherPurposes) != 0," "no gathering trips to another map")
