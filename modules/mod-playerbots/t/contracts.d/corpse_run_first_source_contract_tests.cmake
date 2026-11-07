cmake_policy(VERSION 3.16)
# twow-repo#575 A3 step 4 S4-1 (OB-00 07.11.2026; design ws-10/cli-pathfinding/a3-step4-route/DESIGN-STEP4-RESCUE-DANGER-20261107.md
# d54dfd0e; CLI-PATH): AiPlayerbot.Revive.CorpseRunFirst (cohort modes, default 0). "revive from corpse" was USELESS in the dead
# engine ("stay" + MovementAction::isUseful = !stay, A3 U4); with the key the reclaim runs, the spirit healer is taken only with
# a reason, and a ghost hit by "move long stuck" goes to the spirit healer instead of a repop. Pins: the hook places and their
# order, the defaults, the log fields, the pure policy, and that every legacy path stays for bots the key does not apply to.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "corpse run first: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "corpse run first: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "corpse run first: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "corpse run first: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from `first` to the next `second` after it.
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "corpse run first: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "corpse run first: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("CorpseRunFirstPolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("AiFactory.cpp" factory)
read_source("strategy/generic/DeadStrategy.cpp" dead_strategy)
read_source("strategy/values/DeadValues.cpp" dead_values)
read_source("strategy/actions/ReviveFromCorpseAction.h" revive_h)
read_source("strategy/actions/ReviveFromCorpseAction.cpp" revive_cpp)
read_source("strategy/actions/ReleaseSpiritAction.h" release_h)

# 1. Pure policy: no game types, no config, no log; the rule values and the order of the reasons.
forbid_text("${policy}" "#include \"playerbot/" "the policy includes a game header")
forbid_text("${policy}" "sPlayerbotAIConfig" "the policy reads config")
forbid_text("${policy}" "sLog" "the policy logs")
forbid_text("${policy}" "Player*" "the policy takes a game type")
require_text("${policy}" "namespace ai::corpse_first" "own namespace (ai::corpse_run belongs to the chat command)")
require_text("${policy}" "constexpr std::uint32_t DefaultMaxSeconds = 600;" "default max seconds")
require_text("${policy}" "constexpr std::uint32_t LoopDeaths = 2;" "loop deaths")
require_text("${policy}" "constexpr std::uint32_t LoopWindowSeconds = 600;" "loop window")
require_text("${policy}" "constexpr float LoopRadius = 60.0f;" "loop radius")
require_text("${policy}" "constexpr std::uint8_t MinDurability = 10;" "durability floor")
between("${policy}" "inline Reason Decide(Input const& in)" "\n}\n" decide)
require_order("${decide}" "return Reason::NoCorpse;" "return Reason::Sickness;" "no corpse before sickness")
require_order("${decide}" "return Reason::Sickness;" "return Reason::Durability;" "sickness before durability")
require_order("${decide}" "return Reason::Durability;" "return Reason::Loop;" "durability before loop")
require_order("${decide}" "return Reason::Loop;" "return Reason::Timeout;" "loop before timeout")
require_order("${decide}" "return Reason::Timeout;" "return Reason::Corpse;" "corpse run last")
forbid_text("${decide}" "death count" "the many-deaths reason is dropped (OB-00 07.11. decision 3)")

# 2. Config: default 0, cohort clamp, MaxSeconds clamp, echo, dist.
require_text("${config_h}" "uint32 reviveCorpseRunFirst = 0;" "default off")
require_text("${config_h}" "uint32 reviveCorpseRunFirstMaxSeconds = 600;" "default max seconds member")
require_text("${config_cpp}" "#include \"playerbot/CorpseRunFirstPolicy.h\"" "loader include")
require_text("${config_cpp}" "reviveCorpseRunFirst = ai::corpse_first::ClampMode(config.GetIntDefault(\"AiPlayerbot.Revive.CorpseRunFirst\", 0));" "mode loader")
require_text("${config_cpp}" "reviveCorpseRunFirstMaxSeconds = ai::corpse_first::ClampMaxSeconds(config.GetIntDefault(\"AiPlayerbot.Revive.CorpseRunFirst.MaxSeconds\", int(ai::corpse_first::DefaultMaxSeconds)));" "max seconds loader")
require_text("${config_cpp}" "{\"Revive.CorpseRunFirst\", Value(reviveCorpseRunFirst)}," "echo mode")
require_text("${config_cpp}" "{\"Revive.CorpseRunFirst.MaxSeconds\", Value(reviveCorpseRunFirstMaxSeconds)}," "echo max seconds")
require_text("${conf_dist}" "\nAiPlayerbot.Revive.CorpseRunFirst = 0\n" "dist default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Revive.CorpseRunFirst.MaxSeconds = 600\n" "dist max seconds")

# 3. Why the override is needed: the dead engine carries "stay", and the legacy triggers stay registered (off = unchanged).
require_text("${factory}" "deadEngine->addStrategies(\"dead\", \"stay\", \"default\", \"follow\", \"group\", NULL);" "dead engine carries stay (U4)")
require_text("${dead_strategy}" "\"move long stuck\",\n        NextAction::array(0, new NextAction(\"repop\", relevance+1), NULL)" "ghost move long stuck trigger unchanged")
require_text("${dead_strategy}" "\"val::should spirit healer\"" "spirit healer trigger unchanged")

# 4. The reclaim: isUseful override on ReviveFromCorpseAction only, gated, legacy answer otherwise.
between("${revive_h}" "class ReviveFromCorpseAction" "class FindCorpseAction" revive_class)
require_text("${revive_class}" "virtual bool isUseful() override;" "revive from corpse overrides isUseful")
between("${revive_cpp}" "bool ReviveFromCorpseAction::isUseful()" "\n}\n" revive_useful)
require_text("${revive_useful}" "ai::cohort::Applies(sPlayerbotAIConfig.reviveCorpseRunFirst, bot->GetGUIDLow()) && !ai->HasActivePlayerMaster()" "gate: switch and no active player master")
require_text("${revive_useful}" "return !bot->InBattleGround() && bot->GetCorpse();" "useful for a ghost with a corpse outside BGs")
require_text("${revive_useful}" "return MovementAction::isUseful();" "legacy answer for everyone else")
require_order("${revive_cpp}" "bot->GetSession()->HandleReclaimCorpseOpcode(packet);" "ai::corpse_first::SharedCounters().reclaimed.fetch_add" "reclaim counted after the reclaim")

# 5. The spirit healer decision: gated before every legacy rule, after the active-master rule.
between("${dead_values}" "bool ShouldSpiritHealerValue::Calculate()" "//Stick to corpse." should_healer)
require_order("${should_healer}" "if (ai->HasActivePlayerMaster())" "if (ai::cohort::Applies(sPlayerbotAIConfig.reviveCorpseRunFirst, bot->GetGUIDLow()))" "active master first")
require_order("${should_healer}" "return CorpseRunFirstHealer(ai, bot, durability);" "//Died too many times" "the switch path before the legacy rules")
require_text("${should_healer}" "if (deathCount > DEATH_COUNT_BEFORE_REVIVING_AT_SPIRIT_HEALER)" "legacy many-deaths rule kept for the other bots")
between("${dead_values}" "static bool CorpseRunFirstHealer(" "\n}\n" healer)
require_text("${healer}" "cf::Reason const reason = cf::Decide(in);" "decision from the policy")
require_text("${healer}" "in.maxSeconds = sPlayerbotAIConfig.reviveCorpseRunFirstMaxSeconds;" "max seconds from config")
require_text("${healer}" "cf::DeathsNear(ai->GetCorpseFirstDeaths(), now, corpse->GetMapId(), corpse->GetPositionX(), corpse->GetPositionY())" "own deaths near the corpse")
require_text("${healer}" "[CorpseRun] bot=%u cohort=%c level=%u map=%u zone=%u decision=%s reason=%s dead_s=%u corpse_d=%.0f deaths_10m=%u first=%u" "decision line")
require_text("${healer}" "[CorpseRunMin] deaths=%u corpse=%u no_corpse=%u sickness=%u durability=%u loop=%u timeout=%u reclaimed=%u ghost_stuck=%u" "minute line")
require_text("${healer}" "if (uint32 const kind = ai->MarkCorpseFirstDecision(deathAt, reason))" "one line per death and decision")
require_text("${healer}" "return reason != cf::Reason::Corpse;" "spirit healer only with a reason")

# 6. Ghost "move long stuck": spirit healer instead of repop, before the repop body, only for a ghost with a corpse.
between("${release_h}" "class RepopAction : public SpiritHealerAction" "virtual bool isUseful() override" repop_execute)
require_order("${repop_execute}" "event.getSource().find(\"move long stuck\") != std::string::npos && bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) && bot->GetCorpse()" "ai->ClearDeathLoop();" "ghost gate before the repop body")
require_text("${repop_execute}" "!ai->HasActivePlayerMaster() && ai::cohort::Applies(sPlayerbotAIConfig.reviveCorpseRunFirst, bot->GetGUIDLow())" "ghost gate: switch and no active player master")
require_text("${repop_execute}" "return ai->DoSpecificAction(\"spirit healer\", event, true);" "spirit healer instead of repop")
require_text("${repop_execute}" "decision=healer reason=ghost_stuck" "ghost stuck line")
require_text("${release_h}" "#include \"playerbot/CorpseRunFirstPolicy.h\"" "release spirit include")

# 7. Own deaths: recorded on death for the switch's bots, next to the death loop record; bot thread only.
require_order("${ai_cpp}" "RecordDeathForLoop();\n" "RecordCorpseFirstDeath(mark);" "own death recorded with the loop record")
between("${ai_cpp}" "RecordDeathForLoop();\n" "RecordDeathForSeries();" own_death)
require_text("${own_death}" "if (ai::cohort::Applies(sPlayerbotAIConfig.reviveCorpseRunFirst, bot->GetGUIDLow()))" "recorded only for the switch's bots")
require_text("${ai_h}" "ai::corpse_first::Ring corpseFirstDeaths;" "per-bot ring")
require_text("${ai_h}" "#include \"playerbot/CorpseRunFirstPolicy.h\"" "ai header include")

message(STATUS "corpse run first source contract: ok")
