function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/triggers/RpgTriggers.cpp" rpg_triggers)
file(READ "${PB_SOURCE_DIR}/strategy/actions/UseItemAction.cpp" use_item)

# #307: roster bots neither bind nor hearth to a zone clearly above their level.
require_text("${rpg_triggers}" "homebind::IsZoneClearlyAboveLevel(area ? area->area_level : 0, bot->GetLevel())" "level-appropriate inn bind")
require_text("${use_item}" "homebind::IsZoneClearlyAboveLevel(area ? area->area_level : 0, bot->GetLevel())" "level-appropriate hearthstone")
require_text("${use_item}" "AI_VALUE(WorldPosition, \"home bind\")" "hearthstone checks the actual bind")
