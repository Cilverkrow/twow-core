# twow-repo#654 step 1 (OB-00 / owner 29.10.2026, "fix the poison ping-pong bug first"): AiPlayerbot.Rotation.TankRoguePoison, code
# default 1 (0 = rollback, a bug fix). The rogue tank's multiplier keeps every main-hand poison but the threat poison (Agitating) off; it
# matched the suffix " poison main hand" on the action's name, but the actions are named "apply instant poison" etc., so it never matched,
# and it ran in the combat engine only. This contract pins the dist key, the config member and read (unknown = the default), the parsed
# value in [ConfigEcho] (owner rule #615, sorted), the pure policy, the hand / threat-poison accessors on the poison actions, the
# multiplier's rule by the action (before the old suffix check, which stays as the rollback path) and its non-combat registration.
# The rules are tested in t/tank_rogue_poison_policy_tests.cpp.

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
    message(FATAL_ERROR "#654 TankRoguePoison: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#654 TankRoguePoison: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("TankRoguePoisonPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("strategy/rogue/RogueActions.h" actions_h)
read_source("strategy/rogue/TankRogueStrategy.h" strategy_h)
read_source("strategy/rogue/TankRogueStrategy.cpp" strategy_cpp)

# 1. The switch: default 1 in the dist, the member and the read; unknown = the default; parsed value in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.TankRoguePoison = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 rotationTankRoguePoison = 1;" "config member, default 1")
require_text("${config_cpp}" "rotationTankRoguePoison = config.GetIntDefault(\"AiPlayerbot.Rotation.TankRoguePoison\", 1);   // twow-repo#654 poison ping-pong\n    if (rotationTankRoguePoison > 2)\n        rotationTankRoguePoison = 1;" "config read, unknown = the default")
require_text("${config_cpp}" "            {\"Rotation.TankKitFromLevel\", Value(rotationTankKitFromLevel)},\n            {\"Rotation.TankRoguePoison\", Value(rotationTankRoguePoison)},\n" "parsed value in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std only + CohortPolicy.h) and its rule.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#654 TankRoguePoison: TankRoguePoisonPolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the switch through CohortPolicy (0 off, 1 all, 2 cohort B)")
require_text("${policy}" "return isPoisonAction && mainHand && !isThreatPoison;" "only a foreign main-hand poison is blocked")

# 3. The poison actions carry the hand and the threat-poison flag (not their names).
require_text("${actions_h}" "        bool IsMainHand() const { return mainHand; }\n        virtual bool IsThreatPoison() const { return false; }" "ApplyPoisonAction: hand + threat-poison accessors")
string(FIND "${actions_h}" "class ApplyAgitatingPoisonAction : public ApplyPoisonAction" ag)
if(ag EQUAL -1)
  message(FATAL_ERROR "#654 TankRoguePoison: ApplyAgitatingPoisonAction not found")
endif()
string(SUBSTRING "${actions_h}" ${ag} 400 agitating)
require_text("${agitating}" "bool IsThreatPoison() const override { return true; }" "Agitating Poison is the threat poison")

# 4. The multiplier: the rule by the action comes first (switch on); the old suffix check stays as the rollback path.
string(FIND "${strategy_cpp}" "float TankRogueThreatMultiplier::GetValue(Action* action)" gv)
if(gv EQUAL -1)
  message(FATAL_ERROR "#654 TankRoguePoison: TankRogueThreatMultiplier::GetValue not found")
endif()
string(SUBSTRING "${strategy_cpp}" ${gv} 2000 getvalue)
require_text("${getvalue}" "    if (ai::tank_rogue_poison::Applies(sPlayerbotAIConfig.rotationTankRoguePoison, bot->GetGUIDLow()))\n    {\n        ApplyPoisonAction const* poison = dynamic_cast<ApplyPoisonAction const*>(action);\n        if (poison && ai::tank_rogue_poison::BlocksMainHand(true, poison->IsMainHand(), poison->IsThreatPoison()))\n            return 0.0f;\n\n        return 1.0f;\n    }" "the rule by the action, switch on")
require_order("${getvalue}" "if (name == \"feint\" || name == \"vanish\")" "ai::tank_rogue_poison::Applies(" "Feint / Vanish stay blocked first")
require_order("${getvalue}" "ai::tank_rogue_poison::Applies(" "std::string const mainHand = \" poison main hand\";" "the new rule before the old suffix check (rollback path)")
require_text("${strategy_cpp}" "#include \"playerbot/TankRoguePoisonPolicy.h\"" "the policy include")

# 5. The multiplier runs out of combat too (the ping-pong was out of combat).
require_text("${strategy_h}" "void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;" "non-combat multipliers declared")
require_text("${strategy_cpp}" "void TankRogueStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)\n{\n    multipliers.push_back(new TankRogueThreatMultiplier(ai));\n}" "non-combat registration")

message(STATUS "#654 TankRoguePoison source contract passed")
