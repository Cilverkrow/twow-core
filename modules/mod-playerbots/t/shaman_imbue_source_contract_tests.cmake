# twow-repo#541: shaman weapon imbue per spec (owner 09.10.2026, table in ShamanImbuePolicy.h).
# Switch AiPlayerbot.Shaman.ImbueBySpec (default 0): every spec strategy binds the "shaman weapon" trigger to
# the per-spec action; off, each strategy keeps its old imbue. The trigger loop checks each learned imbue.
# Earthliving Weapon (not on Turtle) is gone.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 shaman imbue: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 shaman imbue: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR at "${at} + 1")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("ShamanImbuePolicy.h" policy)
read_source("strategy/shaman/ShamanActions.cpp" actions)
read_source("strategy/shaman/ShamanTriggers.cpp" triggers)
read_source("strategy/shaman/ShamanAiObjectContext.cpp" context)

require_text("${config_h}" "bool shamanImbueBySpec = false;" "switch default off")
require_text("${config_cpp}" "shamanImbueBySpec = config.GetBoolDefault(\"AiPlayerbot.Shaman.ImbueBySpec\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Shaman.ImbueBySpec = 0" "documented")

# The table: Windfury, Frostbrand only solo, Flametongue for everyone but tanks, Rockbiter last.
string(FIND "${policy}" "std::string Choose(Spec spec, bool inGroup, Known known)" choose_at)
string(SUBSTRING "${policy}" ${choose_at} -1 choose)
require_order("${choose}" "if (known(Windfury))" "if (!inGroup && known(Frostbrand))" "Windfury before Frostbrand")
require_order("${choose}" "if (!inGroup && known(Frostbrand))" "if (spec != Spec::Tank && known(Flametongue))" "Frostbrand (solo) before Flametongue")
require_order("${choose}" "if (spec != Spec::Tank && known(Flametongue))" "if (known(Rockbiter))" "Rockbiter last")
require_order("${choose}" "if (spec == Spec::Enhancement)" "if (known(Windfury))" "Windfury and Frostbrand only for enhancement")

# Tank strategy: Rockbiter at ACTION_HIGH + 1 in both modes (the table's tank row; tank_path contract).
read_source("strategy/shaman/TankShamanStrategy.cpp" tank)
require_text("${tank}" "new NextAction(\"rockbiter weapon\", ACTION_HIGH + 1)" "tank Rockbiter")

# Every other spec strategy binds the trigger through the switch.
foreach(strategy Elemental Enhancement Restoration)
  read_source("strategy/shaman/${strategy}ShamanStrategy.cpp" text)
  count_text("${text}" "\"shaman weapon\"," triggers_count)
  count_text("${text}" "\"shaman weapon\",\n        NextAction::array(0, new NextAction(shaman_imbue::TriggerAction(sPlayerbotAIConfig.shamanImbueBySpec, \"" switched)
  if(triggers_count EQUAL 0 OR NOT triggers_count EQUAL switched)
    message(FATAL_ERROR "#541 shaman imbue: ${strategy}: ${switched} of ${triggers_count} \"shaman weapon\" bindings go through the switch")
  endif()
endforeach()

# The action chooses before the base checks and is registered.
require_order("${actions}" "bool CastShamanWeaponImbueAction::isUseful()\n{\n    return ChooseImbue() &&" "CastEnchantItemAction::isUseful();" "choose before isUseful")
require_order("${actions}" "bool CastShamanWeaponImbueAction::isPossible()\n{\n    return ChooseImbue() &&" "CastEnchantItemAction::isPossible();" "choose before isPossible")
require_text("${actions}" "ai->HasStrategy(\"tank shaman\", BotState::BOT_STATE_COMBAT), AiFactory::GetPlayerSpecTab(bot)" "spec from tank strategy and talent tab")
require_text("${actions}" "bot->GetGroup() != nullptr" "group decides Frostbrand")
require_text("${context}" "creators[\"shaman weapon imbue\"] = [](PlayerbotAI* ai) { return new CastShamanWeaponImbueAction(ai); };" "action registered")

# Trigger: with the switch each learned imbue is checked (the loop variable, not the trigger's own spell).
require_order("${triggers}" "if (sPlayerbotAIConfig.shamanImbueBySpec)" "uint32 const imbueId = AI_VALUE2(uint32, \"spell id\", imbue);" "fixed loop behind the switch")
require_order("${triggers}" "uint32 const imbueId = AI_VALUE2(uint32, \"spell id\", imbue);" "uint32 spellId = AI_VALUE2(uint32, \"spell id\", spell);" "old check only when off")

# No Earthliving left in code (comments may mention it).
file(GLOB_RECURSE module_sources "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
foreach(source ${module_sources})
  file(STRINGS "${source}" lines REGEX "[Ee]arthliving")
  foreach(line ${lines})
    string(REGEX REPLACE "//.*$" "" code "${line}")
    if(code MATCHES "[Ee]arthliving")
      message(FATAL_ERROR "#541 shaman imbue: Earthliving still referenced in ${source}: ${line}")
    endif()
  endforeach()
endforeach()

message(STATUS "shaman_imbue source contract passed")
