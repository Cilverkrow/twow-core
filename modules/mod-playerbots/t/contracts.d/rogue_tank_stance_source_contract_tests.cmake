# twow-repo#654 (WS10-ROGUE-TANK-STANCE-01, owner 29.10.2026): AiPlayerbot.Rotation.RogueTankStance, code default 0 (BEHAVIOUR CHANGE,
# A/B with 2). The core makes Shadow Dance a rogue tank stance (Funserver.Rogue.ShadowDanceStance, stance 61230); with this switch a bot
# on the rogue tank path learns it from 20 while the core switch covers bots, keeps it up while it runs "tank rogue", cancels it
# otherwise, and does not try Stealth / Vanish in it. This contract pins the dist key, the config member and read (unknown = 0), the
# parsed value in [ConfigEcho] (owner rule #615, sorted), the pure policy and its stance id (= the core's), the teach / unteach in the
# kit sync (switch off: untouched), the trigger by id (switch off: the old buff trigger), the cancel trigger / action in the rogue base
# strategy, and the Stealth / Vanish guards. The rules are tested in t/rogue_tank_stance_policy_tests.cpp.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#654 RogueTankStance: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#654 RogueTankStance: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("RogueTankStancePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("strategy/actions/AutoLearnSpellAction.cpp" learn_cpp)
read_source("strategy/rogue/RogueTriggers.h" triggers_h)
read_source("strategy/rogue/RogueTriggers.cpp" triggers_cpp)
read_source("strategy/rogue/RogueActions.h" actions_h)
read_source("strategy/rogue/RogueAiObjectContext.cpp" context_cpp)
read_source("strategy/rogue/RogueStrategy.cpp" strategy_cpp)
file(READ "${CORE_SOURCE_DIR}/src/game/FunserverShadowDanceStance.h" core_policy)

# 1. The switch: default 0 in the dist, the member and the read; unknown = 0; parsed value in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.RogueTankStance = 0\n" "dist key, default 0")
require_text("${config_h}" "uint32 rotationRogueTankStance = 0;" "config member, default 0")
require_text("${config_cpp}" "rotationRogueTankStance = config.GetIntDefault(\"AiPlayerbot.Rotation.RogueTankStance\", 0);   // twow-repo#654 Shadow Dance stance\n    if (rotationRogueTankStance > 2)\n        rotationRogueTankStance = 0;" "config read, unknown = off")
require_text("${config_cpp}" "            {\"Rotation.RestGate\", Value(rotationRestGate)},\n            {\"Rotation.RogueTankStance\", Value(rotationRogueTankStance)},\n            {\"Rotation.SpecFromPath\", Value(rotationSpecFromPath)},\n" "parsed value in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std only + CohortPolicy.h), the stance id equals the core's.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#654 RogueTankStance: RogueTankStancePolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "constexpr std::uint32_t StanceSpell = 61230;" "the stance id")
require_text("${core_policy}" "SHADOW_DANCE_STANCE                    = 61230," "the core's stance id (FunserverShadowDanceStance.h)")
require_text("${policy}" "return applies && coreCovers && tankPath && level >= MinLevel;" "taught only on the tank path from 20 with both switches")
require_text("${policy}" "return applies && coreCovers && tankStrategy;" "up only while tanking")
require_text("${policy}" "return coreMode == 1 || coreMode == 2;" "the core switch covers bots with 1 and 2")

# 3. The kit sync teaches / unteaches it, only with the switch.
require_text("${learn_cpp}" "#include \"playerbot/RogueTankStancePolicy.h\"" "the policy include in AutoLearnSpellAction.cpp")
require_text("${learn_cpp}" "    if (bot->getClass() == CLASS_ROGUE && ai::rogue_tank_stance::Applies(sPlayerbotAIConfig.rotationRogueTankStance, bot->GetGUIDLow()))\n    {\n        bool const coreCovers = ai::rogue_tank_stance::CoreCoversBots(sWorld.getConfig(CONFIG_UINT32_FUNSERVER_ROGUE_SHADOW_DANCE_STANCE));\n        known.push_back(ai::rogue_tank_stance::StanceSpell);\n        if (ai::rogue_tank_stance::Teach(true, coreCovers, (path & ai::spec_aura::RogueTank) != 0, bot->GetLevel()))\n            wanted.push_back(ai::rogue_tank_stance::StanceSpell);\n        else\n            bot->RemoveAurasDueToSpell(ai::rogue_tank_stance::StanceSpell);\n    }" "the stance in the kit sync, switch off untouched")
require_order("${learn_cpp}" "known.push_back(ai::rogue_tank_stance::StanceSpell);" "bot->removeSpell(spellId, false, false);" "managed before the removal loop")

# 4. The trigger by id (switch off: the old buff trigger) and the cancel trigger / action.
require_text("${triggers_h}" "    class ShadowDanceTrigger : public BuffTrigger" "the Shadow Dance trigger keeps the buff trigger as its base")
require_text("${triggers_cpp}" "    if (!ai::rogue_tank_stance::Applies(sPlayerbotAIConfig.rotationRogueTankStance, bot->GetGUIDLow()))\n        return BuffTrigger::IsActive();   // switch off: as before" "switch off: as before")
require_text("${triggers_cpp}" "    return RogueTankStanceCovers(bot) && bot->HasSpell(ai::rogue_tank_stance::StanceSpell) &&\n        !bot->HasAura(ai::rogue_tank_stance::StanceSpell);" "active while known, covered and not up (by id)")
require_text("${triggers_cpp}" "ai::rogue_tank_stance::CoreCoversBots(sWorld.getConfig(CONFIG_UINT32_FUNSERVER_ROGUE_SHADOW_DANCE_STANCE))" "the core switch read")
require_text("${triggers_cpp}" "    bool const tanking = ai->HasStrategy(\"tank rogue\", BotState::BOT_STATE_COMBAT) || ai->HasStrategy(\"tank rogue\", BotState::BOT_STATE_NON_COMBAT);" "tanking = the tank rogue strategy")
require_text("${actions_h}" "            if (!ai::rogue_tank_stance::Applies(sPlayerbotAIConfig.rotationRogueTankStance, bot->GetGUIDLow()))\n                return CastBuffSpellAction::isUseful();\n\n            return !bot->HasAura(ai::rogue_tank_stance::StanceSpell);" "the stance action useful by id (switch off: as before)")
require_text("${actions_h}" "        CancelShadowDanceStanceAction(PlayerbotAI* ai) : Action(ai, \"cancel shadow dance stance\") {}" "the cancel action")
require_text("${actions_h}" "            bot->RemoveAurasDueToSpell(ai::rogue_tank_stance::StanceSpell);" "the cancel removes the stance aura")
require_text("${context_cpp}" "creators[\"shadow dance stance off\"] = [](PlayerbotAI* ai) { return new ShadowDanceStanceOffTrigger(ai); };" "the cancel trigger registered")
require_text("${context_cpp}" "creators[\"cancel shadow dance stance\"] = [](PlayerbotAI* ai) { return new CancelShadowDanceStanceAction(ai); };" "the cancel action registered")
string(FIND "${strategy_cpp}" "#ifdef MANGOSBOT_ZERO" zero)
string(FIND "${strategy_cpp}" "#ifdef MANGOSBOT_ONE" one)
math(EXPR zero_len "${one} - ${zero}")
string(SUBSTRING "${strategy_cpp}" ${zero} ${zero_len} vanilla)
string(REGEX MATCHALL "new NextAction\\(\"cancel shadow dance stance\", ACTION_HIGH \\+ 6\\)" cancels "${vanilla}")
list(LENGTH cancels cancel_count)
if(NOT cancel_count EQUAL 2)
  message(FATAL_ERROR "#654 RogueTankStance: the vanilla rogue base strategy needs the cancel node in combat and non-combat (found ${cancel_count})")
endif()

# 5. Owner "Nur Stealth und Vanish": neither is tried in the stance (no blocked-cast loop).
string(FIND "${actions_h}" "class CastStealthAction : public CastBuffSpellAction" st)
string(SUBSTRING "${actions_h}" ${st} 900 stealth)
require_text("${stealth}" "if (!ai::rogue_tank_stance::StealthUseful(bot->HasAura(ai::rogue_tank_stance::StanceSpell)))\n                return false;" "Stealth not useful in the stance")
string(FIND "${actions_h}" "class CastVanishAction : public CastBuffSpellAction" va)
string(SUBSTRING "${actions_h}" ${va} 600 vanish)
require_text("${vanish}" "if (!ai::rogue_tank_stance::StealthUseful(bot->HasAura(ai::rogue_tank_stance::StanceSpell)))\n                return false;" "Vanish not useful in the stance")

message(STATUS "#654 RogueTankStance source contract passed")
