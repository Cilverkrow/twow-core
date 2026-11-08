# twow-repo#333 M2 Crafting.Cooking (owner list 07.11.2026, item 2; M2-ENTWURF §7; inventory A3-AKTIONS-VERDRAHTUNG §4: cooking dead
# in operation - learned only at a random local trainer, the craft loop skips fire recipes, no campfire 818).
# AiPlayerbot.Crafting.Cooking (code default 0; 1 all, 2 cohort B, 3 cohort A), for a roster bot on its own: the profession trips
# also teach cooking; under RealReagents a fire recipe (focus 4) is a craft candidate when a fire is in reach or the bot can light
# its campfire; the craft action lights it first and cooks on the next run. Proof lines "[Crafting] kind=cooking event=campfire|cook".
# This contract pins the switch, the helper, the trip skill rule, the trigger's focus exception, the campfire step and the lines.

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
    message(FATAL_ERROR "#333 M2 Crafting.Cooking: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 M2 Crafting.Cooking: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Cooking: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Cooking: region end not found after '${first}': '${second}'")
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
    message(FATAL_ERROR "#333 M2 Crafting.Cooking: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/ProfessionTravelPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.h" pu_h)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.cpp" pu_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/ProfessionUseActions.cpp" act)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry, documented lines.
require_text("${dist}" "\nAiPlayerbot.Crafting.Cooking = 0\n" "dist key, default 0")
require_text("${dist}" "#   [Crafting] kind=cooking event=campfire result=cast_started|failed check= recipe= bot= level= cohort=" "dist: campfire line")
require_text("${dist}" "#   [Crafting] kind=cooking event=cook spell= item= bot= level= cohort=" "dist: cook line")
require_text("${config_h}" "uint32 craftingCooking = 0;" "config member, default 0")
require_text("${config_cpp}" "craftingCooking = config.GetIntDefault(\"AiPlayerbot.Crafting.Cooking\", 0);\n    if (craftingCooking > 3)\n    {\n        sLog.outError(\"[Crafting] config key=Cooking value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", craftingCooking);\n        craftingCooking = 0;\n    }" "config read, > 3 = off")
require_text("${config_cpp}" "{\"Crafting.Cooking\", Value(craftingCooking)}," "[ConfigEcho] entry")
require_order("${config_cpp}" "{\"CheatItem.Trace\", Value(cheatItemTrace)}," "{\"Crafting.Cooking\", Value(craftingCooking)}," "[ConfigEcho] sorted after CheatItem.Trace")
require_order("${config_cpp}" "{\"Crafting.Consumables\", Value(craftingConsumables)}," "{\"Crafting.Cooking\", Value(craftingCooking)}," "[ConfigEcho] sorted after Crafting.Consumables")
require_order("${config_cpp}" "{\"Crafting.Cooking\", Value(craftingCooking)}," "{\"CustomRaceStartZones\"" "[ConfigEcho] sorted before CustomRaceStartZones")

# 1. One helper: config first, roster bot on its own, cohort. The fire: focus 4 within 10 yd as Spell::CheckCast; lighting needs
#    the spell, no (category) cooldown, the flint and the wood.
require_text("${pu_h}" "bool CraftingCookingFor(PlayerbotAI* ai);" "(1) helper declared")
require_text("${pu_h}" "constexpr uint32 kCookingFireFocus = 4;" "(1) cooking fire focus")
require_text("${pu_h}" "constexpr uint32 kCampfireSpell = 818;" "(1) Basic Campfire")
between("${pu_cpp}" "bool ai::CraftingCookingFor(PlayerbotAI* ai)" "\n}\n" helper)
require_text("${helper}" "    if (!sPlayerbotAIConfig.craftingCooking)\n        return false;" "(1) config check first")
require_text("${helper}" "return bot && IsRosterBotOnItsOwn(ai) && ai::cohort::Applies(sPlayerbotAIConfig.craftingCooking, bot->GetGUIDLow());" "(1) roster bot on its own, cohort")
between("${pu_cpp}" "bool ai::CookingFireNear(Player* bot)" "\n}\n" near)
require_text("${near}" "MaNGOS::GameObjectFocusCheck check(bot, kCookingFireFocus);" "(1) the core's focus check")
require_text("${near}" "Cell::VisitGridObjects(bot, searcher, 10.0f);" "(1) Spell::CheckCast's 10 yd")
between("${pu_cpp}" "bool ai::CanLightCampfire(Player* bot)" "\n}\n" light)
require_text("${light}" "bot->HasSpell(kCampfireSpell) && !bot->HasSpellCooldown(kCampfireSpell) &&" "(1) spell known, no cooldown")
require_text("${light}" "!(fire->Category && bot->HasSpellCategoryCooldown(fire->Category)) && HasCraftTools(fire, bot) && CraftableFromBags(fire, bot) > 0;" "(1) no category cooldown, flint, wood")

# 2. The trips: cooking only with the switch (pure rule), the one trainer list reads it once.
require_text("${policy}" "constexpr std::uint32_t kCooking = 185;" "(2) cooking skill")
require_text("${policy}" "inline bool IsTripSkill(std::uint32_t skill, bool cooking, bool survival = false, bool fishing = false)\n{\n    return IsPhaseOneSkill(skill) || (cooking && skill == kCooking) || (survival && skill == kSurvival) ||\n        (fishing && skill == kFishing);\n}" "(2) trip skill rule (M2 Crafting.Survival / .Fishing add their flags)")
require_text("${policy}" "inline bool NearTripSkill(std::uint32_t skill, bool hasSkill, bool cooking, bool survival = false, bool fishing = false)\n{\n    return IsTripSkill(skill, cooking, survival, fishing) && !hasSkill;\n}" "(2) near trip once")
require_text("${policy}" "inline bool IsPhaseOneSkill(std::uint32_t skill)\n{\n    return skill == kHerbalism || skill == kMining || skill == kSkinning || skill == kFirstAid;\n}" "(2) phase one unchanged")
between("${travel}" "std::vector<int32> ai::PhaseOneProfessionTrainers(PlayerbotAI* ai, bool onlyUnlearned)" "\n}\n" trainers)
require_text("${trainers}" "    bool const cooking = CraftingCookingFor(ai);\n" "(2) switch read once")
require_text("${trainers}" "if (!ai::profession_travel::IsTripSkill(skill, cooking, survival, fishing))" "(2) trip skill filter")
require_text("${trainers}" "NearTripSkill(skill, ai->HasSkill(SkillType(skill)), cooking, survival, fishing)" "(2) near trip filter")
count_text("${trainers}" "IsPhaseOneSkill(" 0 "(2) no direct phase-one filter left")

# 3. The trigger: under RealReagents, a fire recipe needs no focus when a fire is near or can be lit; every other focus stays.
set(fire "    bool const cookingFire = realReagents && CraftingCookingFor(ai) && (CookingFireNear(bot) || CanLightCampfire(bot));")
require_text("${pu_cpp}" "${fire}" "(3) fire ready")
require_text("${pu_cpp}" "recipe.needsFocus = spell->RequiresSpellFocus != 0 && !(cookingFire && spell->RequiresSpellFocus == kCookingFireFocus);" "(3) only the cooking fire focus")
require_order("${pu_cpp}" "${fire}" "for (uint32 spellId : AI_VALUE(std::vector<uint32>, \"craft spells\"))" "(3) once per scan, before the loop")

# 4. The action: the campfire before the recipe's cast check, the pick kept and the trigger due again; lines.
between("${act}" "bool ProfessionCraftAction::Execute(Event& event)" "bool ProfessionCraftAction::isUseful()" exec)
set(step "        bool const cooking = spell->RequiresSpellFocus == kCookingFireFocus && CraftingCookingFor(ai);\n        if (cooking && !CookingFireNear(bot) && CanLightCampfire(bot))")
require_text("${exec}" "${step}" "(4) campfire step")
require_order("${exec}" "ai->RemoveShapeshift();" "${step}" "(4) after the form drop")
require_order("${exec}" "${step}" "if (!ai->CanCastSpell(spellId, bot, 0, true, nullptr, false, false, false, &check))" "(4) before the recipe's cast check")
require_text("${exec}" "ai->CastSpell(kCampfireSpell, bot, nullptr, true, &fireDuration);" "(4) direct cast, no whisper")
require_text("${exec}" "sLog.outBasic(\"[Crafting] kind=cooking event=campfire result=%s check=%u recipe=%u bot=%u level=%u cohort=%c\"" "(4) campfire line")
require_text("${exec}" "SET_AI_VALUE2(int, \"manual int\", \"profession craft spell\", int32(spellId));" "(4) pick kept")
require_text("${exec}" "SET_AI_VALUE2(time_t, \"manual time\", \"profession craft\", now - time_t(sPlayerbotAIConfig.professionUseCraftIntervalSeconds));" "(4) trigger due again")
require_text("${exec}" "            if (cooking)\n                sLog.outBasic(\"[Crafting] kind=cooking event=cook spell=%u item=%u bot=%u level=%u cohort=%c\"" "(4) cook line after a started cast")
