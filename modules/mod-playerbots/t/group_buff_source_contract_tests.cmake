function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/values/PartyMemberWithoutAuraValue.cpp" aura_value)
file(READ "${PB_SOURCE_DIR}/strategy/values/MaintenanceValues.h" maintenance)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)

# #420: group bots buff their group only, drink late while following, [GroupBuff] log.
require_text("${aura_value}" "!ai::group_buff::MayBuffOutOfGroup(ai->IsInGroupWithRealPlayer())" "group-only buffs")
require_text("${aura_value}" "FindPartyMember(predicate, ignoreOutOfGroup, ignoreTank)" "out-of-group flag passed")
require_text("${maintenance}" "ai::group_buff::DrinkWhileMasterMoves(AI_VALUE2(uint8, \"mana\", \"self target\"), sPlayerbotAIConfig.mediumMana)" "drink threshold while the master walks")
require_text("${ai_cpp}" "RecordGroupBuff(pSpellInfo, target);" "buff casts counted")
require_text("${ai_cpp}" "ReportGroupBuff(uint32(time(nullptr)));" "window reported from UpdateAI")
