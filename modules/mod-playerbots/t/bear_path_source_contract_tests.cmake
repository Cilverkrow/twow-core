function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/AiFactory.cpp" factory)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)
file(READ "${PB_MODULE_DIR}/tools/build_premade_specs.py" generator)

# #308: bear (druid 11.3) is decided by the premade path, Primal Fury only as fallback.
require_text("${factory}" "bool IsBearSpec(Player const* player)" "bear spec helper")
require_text("${factory}" "return path.name == \"bear\";" "bear decided by premade name")
string(REGEX MATCHALL "IsBearSpec\(player\) \|\| player->HasSpell\(16961\)" uses "${factory}")
list(LENGTH uses use_count)
if(NOT use_count EQUAL 6)
  message(FATAL_ERROR "Expected 6 bear/cat decisions using IsBearSpec, found ${use_count}")
endif()

# Generated bear path, 50/50 with cat.
require_text("${config_template}" "AiPlayerbot.PremadeSpecName.11.3 = bear" "bear premade path")
require_text("${config_template}" "AiPlayerbot.PremadeSpecProb.11.3 = 50" "bear weight")
require_text("${config_template}" "AiPlayerbot.PremadeSpecProb.11.1 = 50" "cat weight")
require_text("${config_template}" "AiPlayerbot.PremadeSpecLink.11.3.60 = " "bear level-60 link")
require_text("${generator}" "('bear',          '0140003201-553030213232021521-55')" "bear build in the generator")
require_text("${generator}" "(11, 'bear'): 50," "bear probability in the generator")
