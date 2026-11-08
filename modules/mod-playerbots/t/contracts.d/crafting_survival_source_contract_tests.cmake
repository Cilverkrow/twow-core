# twow-repo#333 M2 Crafting.Survival (owner list 07.11.2026, item 3; M2-ENTWURF §7; inventory: Turtle Survival 142 only an allowed
# secondary skill - no trip, no tool, never bought).
# AiPlayerbot.Crafting.Survival (code default 0; 1 all, 2 cohort B, 3 cohort A), for a roster bot on its own: the profession trips
# also teach Survival; a survivalist's tool trip also buys the Whittle 42004 (after pick and knife); the item usage makes Whittle and
# Simple Wood "buy at a vendor" items for it; the craft loop crafts its recipes. Proof line "[Crafting] kind=survival event=craft".
# This contract pins the switch, the helper, the trip skill rule, the tool rule, the item usage and the line.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Survival: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 M2 Crafting.Survival: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Survival: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Survival: region end not found after '${first}': '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#333 M2 Crafting.Survival: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/ProfessionTravelPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel)
read_source("${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.h" pu_h)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.cpp" pu_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/ProfessionUseActions.cpp" act)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry, documented line.
require_text("${dist}" "\nAiPlayerbot.Crafting.Survival = 0\n" "dist key, default 0")
require_text("${dist}" "#   [Crafting] kind=survival event=craft spell= item= skill= bot= level= cohort=" "dist: craft line")
require_text("${config_h}" "uint32 craftingSurvival = 0;" "config member, default 0")
require_text("${config_cpp}" "craftingSurvival = config.GetIntDefault(\"AiPlayerbot.Crafting.Survival\", 0);\n    if (craftingSurvival > 3)\n    {\n        sLog.outError(\"[Crafting] config key=Survival value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", craftingSurvival);\n        craftingSurvival = 0;\n    }" "config read, > 3 = off")
require_text("${config_cpp}" "{\"Crafting.Survival\", Value(craftingSurvival)}," "[ConfigEcho] entry")
require_order("${config_cpp}" "{\"CheatItem.Trace\", Value(cheatItemTrace)}," "{\"Crafting.Survival\", Value(craftingSurvival)}," "[ConfigEcho] sorted after CheatItem.Trace")
require_order("${config_cpp}" "{\"Crafting.Survival\", Value(craftingSurvival)}," "{\"CustomRaceStartZones\"" "[ConfigEcho] sorted before CustomRaceStartZones")

# 1. One helper: config first, roster bot on its own, cohort.
require_text("${pu_h}" "bool CraftingSurvivalFor(PlayerbotAI* ai);" "(1) helper declared")
between("${pu_cpp}" "bool ai::CraftingSurvivalFor(PlayerbotAI* ai)" "\n}\n" helper)
require_text("${helper}" "    if (!sPlayerbotAIConfig.craftingSurvival)\n        return false;" "(1) config check first")
require_text("${helper}" "return bot && IsRosterBotOnItsOwn(ai) && ai::cohort::Applies(sPlayerbotAIConfig.craftingSurvival, bot->GetGUIDLow());" "(1) roster bot on its own, cohort")

# 2. The trips: survival only with the switch (pure rule), the one trainer list reads it once.
require_text("${policy}" "constexpr std::uint32_t kSurvival = 142;" "(2) survival skill")
require_text("${policy}" "inline bool IsTripSkill(std::uint32_t skill, bool cooking, bool survival = false, bool fishing = false)\n{\n    return IsPhaseOneSkill(skill) || (cooking && skill == kCooking) || (survival && skill == kSurvival) ||\n        (fishing && skill == kFishing);\n}" "(2) trip skill rule (M2 Crafting.Fishing adds its flag)")
require_text("${policy}" "inline bool NearTripSkill(std::uint32_t skill, bool hasSkill, bool cooking, bool survival = false, bool fishing = false)\n{\n    return IsTripSkill(skill, cooking, survival, fishing) && !hasSkill;\n}" "(2) near trip once")
between("${travel}" "std::vector<int32> ai::PhaseOneProfessionTrainers(" "\n}\n" trainers)
count_text("${trainers}" "CraftingSurvivalFor(ai)" 1 "(2) the switch read once per list")
require_text("${trainers}" "if (!ai::profession_travel::IsTripSkill(skill, cooking, survival, fishing))" "(2) trip skill filter")
require_text("${trainers}" "NearTripSkill(skill, ai->HasSkill(SkillType(skill)), cooking, survival, fishing)" "(2) near trip filter")

# 3. The tool: the Whittle after pick and knife, only for a survivalist with the switch on.
require_text("${policy}" "constexpr std::uint32_t kWhittle = 42004;" "(3) Whittle")
require_text("${policy}" "inline std::uint32_t MissingTool(bool mining, bool hasPick, bool skinning, bool hasKnife, bool survival, bool hasWhittle)\n{\n    if (std::uint32_t const gatherTool = MissingTool(mining, hasPick, skinning, hasKnife))\n        return gatherTool;\n    return survival && !hasWhittle ? kWhittle : 0;\n}" "(3) tool rule")
between("${travel}" "uint32 ai::MissingGatherTool(PlayerbotAI* ai)" "\n}\n" tool)
require_text("${tool}" "bool const survival = CraftingSurvivalFor(ai) && ai->HasSkill(SkillType(ai::profession_travel::kSurvival));" "(3) survivalist with the switch")
require_text("${tool}" "survival, bot->HasItemCount(ai::profession_travel::kWhittle, 1)" "(3) Whittle in the bags")

# 4. The item usage: Whittle and Simple Wood are skill items of a survivalist with the switch (buy below one stack, then keep).
require_text("${usage}" "return ai->HasSkill(SKILL_COOKING) || (ai->HasSkill(SKILL_SURVIVAL2) && CraftingSurvivalFor(ai));" "(4) Simple Wood")
require_text("${usage}" "case 42004: //Whittle - twow-repo#333 M2 Crafting.Survival: the tool of most survival recipes\n        return ai->HasSkill(SKILL_SURVIVAL2) && CraftingSurvivalFor(ai);" "(4) Whittle")

# 5. The line: after a started cast, for the skill line the trigger picked the recipe for.
between("${act}" "bool const ok = ai->CastSpell(spellId, bot, nullptr, true, &castDuration);" "return ok;" started)
require_text("${started}" "if (AI_VALUE2(int, \"manual int\", \"profession craft last skill\") == int(SKILL_SURVIVAL2) && CraftingSurvivalFor(ai))" "(5) survival recipes of the switch's bots")
require_text("${started}" "sLog.outBasic(\"[Crafting] kind=survival event=craft spell=%u item=%u skill=%u bot=%u level=%u cohort=%c\"" "(5) craft line")
require_order("${act}" "if (ok)\n        {" "[Crafting] kind=survival event=craft" "(5) only after a started cast")
