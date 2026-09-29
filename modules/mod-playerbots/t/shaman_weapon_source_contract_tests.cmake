function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotFactory.cpp" factory)

# twow-repo#357 S2-7: Enhancement shaman bots may take swords with the sword skills.
require_text("${factory}" "ai::shaman_weapons::SwordAllowed(proto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD2," "sword filter by skill")
require_text("${factory}" "bot->HasSkill(SKILL_SWORDS), bot->HasSkill(SKILL_2H_SWORDS))" "skills 43/55 checked")
