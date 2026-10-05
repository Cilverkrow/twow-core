# Hotfix 8.16a: ItemUsageValue must not copy "craft spells" or walk every recipe per item query.
file(READ "${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
file(READ "${PB_SOURCE_DIR}/strategy/values/CraftValues.h" craft_h)
file(READ "${PB_SOURCE_DIR}/strategy/values/ValueContext.h" values)
string(FIND "${usage}" "AI_VALUE(std::vector<uint32>, \"craft spells\")" copy)
if (NOT copy EQUAL -1)
  message(FATAL_ERROR "craft reagent index: ItemUsageValue still copies craft spells")
endif()
foreach (needle
    "AI_VALUE(std::shared_ptr<const CraftReagentIndex>, \"craft reagent index\")"
    "index->find(proto->ItemId)")
  string(FIND "${usage}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "craft reagent index: missing ${needle}")
  endif()
endforeach()
string(FIND "${craft_h}" "CraftReagentIndexValue(PlayerbotAI* ai, std::string name = \"craft reagent index\", int checkInterval = 60)" value)
if (value EQUAL -1)
  message(FATAL_ERROR "craft reagent index: value must recalculate at most every 60 s")
endif()
string(FIND "${values}" "creators[\"craft reagent index\"]" reg)
if (reg EQUAL -1)
  message(FATAL_ERROR "craft reagent index: value not registered")
endif()
# Hotfix 8.16a: the rpg craft path ("craft random item") checks the bags under RealReagents.
file(READ "${PB_SOURCE_DIR}/strategy/actions/CastCustomSpellAction.cpp" cast)
foreach (needle
    "if (sPlayerbotAIConfig.professionUseRealReagents && IsRosterBotOnItsOwn(ai))"
    "uint32 const fromBags = HasCraftTools(pSpellInfo, bot) ? CraftableFromBags(pSpellInfo, bot) : 0;"
    "castCount = std::min(castCount, fromBags);")
  string(FIND "${cast}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "craft random item: missing ${needle}")
  endif()
endforeach()
string(FIND "${cast}" "if (sPlayerbotAIConfig.professionUseRealReagents && IsRosterBotOnItsOwn(ai))" guard)
string(FIND "${cast}" "cmd << \"castnc \";" queue)
if (guard GREATER queue)
  message(FATAL_ERROR "craft random item: the bag check must come before the castnc command")
endif()
# Hotfix 8.16b: the loop of CraftRandomItemAction must not null the shared target (v32 crash).
string(FIND "${cast}" "bool CraftRandomItemAction::Execute" craft_at)
string(SUBSTRING "${cast}" ${craft_at} 4000 craft_body)
foreach (needle
    "WorldObject* spellTarget = wot;"
    "if (!spellTarget || !GuidPosition(spellTarget).IsGameObject())"
    "spellTarget = nullptr;")
  string(FIND "${craft_body}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "craft random item: missing ${needle}")
  endif()
endforeach()
string(REGEX MATCH "\n[ ]+wot = nullptr;" reassign "${craft_body}")
string(FIND "${craft_body}" "GuidPosition(wot)" shared_focus)
if (reassign OR NOT shared_focus EQUAL -1)
  message(FATAL_ERROR "craft random item: the loop must not null or focus-check the shared target wot (v32 crash)")
endif()
# Hotfix 8.20: profession priority, enchanting with real reagents (traced), disenchant filter.
file(READ "${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.cpp" triggers)
file(READ "${PB_SOURCE_DIR}/strategy/actions/CastCustomSpellAction.h" cast_h)
foreach (pair
    "triggers|int const pick = profession_use::Pick(recipes, lastSkill);"
    "triggers|CraftSkillUpChance(spellId, bot, &recipe.skillId, &recipe.skillValue);"
    "triggers|SET_AI_VALUE2(int, \"manual int\", \"profession craft last skill\""
    "cast_h|castCount = HasCraftTools(pSpellInfo, bot) ? CraftableFromBags(pSpellInfo, bot) : 0;"
    "cast|TraceProfessionUse(ai, \"enchant\", cast ? \"cast_started\" : \"failed\""
    "cast|ai::profession_use::CanBeDisenchanted(proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR,")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "hotfix 8.20: missing ${needle}")
  endif()
endforeach()
message(STATUS "CRAFT_REAGENT_INDEX_CONTRACT=PASS")
