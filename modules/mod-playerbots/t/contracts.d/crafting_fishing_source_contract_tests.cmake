# twow-repo#333 M2 Crafting.Fishing (owner list 07.11.2026, item 4; M2-ENTWURF §7; map SURVIVAL-FISHING-MAP: fishing learned only at a
# random local trainer, no pole trip, the fishing purpose 6.5 outranks the quest request 6.3 for up to 30 min).
# AiPlayerbot.Crafting.Fishing (code default 0; 1 all, 2 cohort B, 3 cohort A), for a roster bot on its own: the profession trips also
# teach fishing; a fisher's tool trip also buys the Fishing Pole when the bags hold none; its fishing purpose starts only in the
# quest-first gap and ends with it. Proof line "[Crafting] kind=fishing event=start".
# This contract pins the switch, the helper, the trip skill rule, the tool rule, the gap guard and the line.

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
    message(FATAL_ERROR "#333 M2 Crafting.Fishing: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 M2 Crafting.Fishing: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Fishing: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Fishing: region end not found after '${first}': '${second}'")
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
    message(FATAL_ERROR "#333 M2 Crafting.Fishing: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/ProfessionTravelPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.h" pu_h)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.cpp" pu_cpp)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry, documented line.
require_text("${dist}" "\nAiPlayerbot.Crafting.Fishing = 0\n" "dist key, default 0")
require_text("${dist}" "#   [Crafting] kind=fishing event=start skill= target= backoff_s= bot= level= cohort=" "dist: start line")
require_text("${config_h}" "uint32 craftingFishing = 0;" "config member, default 0")
require_text("${config_cpp}" "craftingFishing = config.GetIntDefault(\"AiPlayerbot.Crafting.Fishing\", 0);\n    if (craftingFishing > 3)\n    {\n        sLog.outError(\"[Crafting] config key=Fishing value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", craftingFishing);\n        craftingFishing = 0;\n    }" "config read, > 3 = off")
require_text("${config_cpp}" "{\"Crafting.Fishing\", Value(craftingFishing)}," "[ConfigEcho] entry")
require_order("${config_cpp}" "{\"CheatItem.Trace\", Value(cheatItemTrace)}," "{\"Crafting.Fishing\", Value(craftingFishing)}," "[ConfigEcho] sorted after CheatItem.Trace")
require_order("${config_cpp}" "{\"Crafting.Fishing\", Value(craftingFishing)}," "{\"CustomRaceStartZones\"" "[ConfigEcho] sorted before CustomRaceStartZones")

# 1. One helper: config first, roster bot on its own, cohort.
require_text("${pu_h}" "bool CraftingFishingFor(PlayerbotAI* ai);" "(1) helper declared")
between("${pu_cpp}" "bool ai::CraftingFishingFor(PlayerbotAI* ai)" "\n}\n" helper)
require_text("${helper}" "    if (!sPlayerbotAIConfig.craftingFishing)\n        return false;" "(1) config check first")
require_text("${helper}" "return bot && IsRosterBotOnItsOwn(ai) && ai::cohort::Applies(sPlayerbotAIConfig.craftingFishing, bot->GetGUIDLow());" "(1) roster bot on its own, cohort")

# 2. The trips: fishing only with the switch (pure rule), the one trainer list reads it once.
require_text("${policy}" "constexpr std::uint32_t kFishing = 356;" "(2) fishing skill")
require_text("${policy}" "(fishing && skill == kFishing);\n}" "(2) trip skill rule")
require_text("${policy}" "return IsTripSkill(skill, cooking, survival, fishing) && !hasSkill;" "(2) near trip once")
between("${travel}" "std::vector<int32> ai::PhaseOneProfessionTrainers(" "\n}\n" trainers)
count_text("${trainers}" "CraftingFishingFor(ai)" 1 "(2) the switch read once per list")
require_text("${trainers}" "if (!ai::profession_travel::IsTripSkill(skill, cooking, survival, fishing))" "(2) trip skill filter")
require_text("${trainers}" "NearTripSkill(skill, ai->HasSkill(SkillType(skill)), cooking, survival, fishing)" "(2) near trip filter")

# 3. The tool: the Fishing Pole when the bags hold no pole, after pick, knife and Whittle.
require_text("${policy}" "constexpr std::uint32_t kFishingPole = 6256;" "(3) Fishing Pole")
require_text("${policy}" "    if (std::uint32_t const tool = MissingTool(mining, hasPick, skinning, hasKnife, survival, hasWhittle))\n        return tool;\n    return fishing && !hasPole ? kFishingPole : 0;" "(3) tool rule")
between("${travel}" "uint32 ai::MissingGatherTool(PlayerbotAI* ai)" "\n}\n" tool)
require_text("${tool}" "bool const fishing = CraftingFishingFor(ai) && ai->HasSkill(SKILL_FISHING);" "(3) fisher with the switch")
require_text("${tool}" "!ai->GetAiObjectContext()->GetValue<std::list<Item*>>(\"inventory items\", \"fishing pole\")->Get().empty();" "(3) any pole, the list of can fish")
require_text("${tool}" "survival, bot->HasItemCount(ai::profession_travel::kWhittle, 1), fishing, hasPole);" "(3) passed to the rule")

# 4. The gap guard: start only in the quest-first gap; a running purpose until the backoff end + hold, never past a turn-in or band walk.
between("${policy}" "inline bool FishingInGap(" "\n}\n" gap)
require_text("${gap}" "    if (!running)\n        return InQuestGap(now, backoffUntil, turnInWaiting, bandTripEnd);" "(4) start in the gap")
require_text("${gap}" "return backoffUntil != 0 && now < backoffUntil + kHoldAfterBackoffSeconds && !turnInWaiting && bandTripEnd == 0;" "(4) running: hold, ends with the gap")
between("${travel}" "    case TravelDestinationPurpose::GatherFishing:\n" "    case TravelDestinationPurpose::GatherSkinning:" fishcase)
require_text("${fishcase}" "        if (CraftingFishingFor(ai))\n        {\n            if (!AI_VALUE(bool, \"can fish\"))\n                return false;" "(4) switch bots: no trip without a pole")
require_text("${fishcase}" "FishingInGap(running.Active() && running.skill == SKILL_FISHING, uint32(time(nullptr)),\n                    uint32(std::max(0, AI_VALUE2(int, \"manual int\", \"quest route backoff until\"))),\n                    ProfessionTripTurnInWaiting(bot, travelTarget), travelTarget->GetBandTripEnd()))\n                return false;" "(4) the gap guard")
count_text("${fishcase}" "SET_AI_VALUE" 0 "(4) the gap values are only read")
require_order("${fishcase}" "\"has strategy\", \"tfish\"" "CraftingFishingFor(ai)" "(4) after the tfish check")

# 5. The line: one per fishing purpose started by the switch's bots.
between("${travel}" "case ai::gather_purpose::Decision::Start:" "case ai::gather_purpose::Decision::Yes:" started)
require_text("${started}" "if (purpose == TravelDestinationPurpose::GatherFishing && CraftingFishingFor(ai))" "(5) fishing starts of the switch's bots")
require_text("${started}" "sLog.outBasic(\"[Crafting] kind=fishing event=start skill=%u target=%u backoff_s=%u bot=%u level=%u cohort=%c\"" "(5) start line")
require_order("${started}" "declared.Start(skill, value, target, now);" "[Crafting] kind=fishing event=start" "(5) after the purpose started")
