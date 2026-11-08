cmake_policy(VERSION 3.16)
# twow-repo#575 A3 step 4 S4-2 (OB-00 07.11.2026; design ws-10/cli-pathfinding/a3-step4-route/DESIGN-STEP4-RESCUE-DANGER-20261107.md
# d54dfd0e + addendum 1 3f6726b8; CLI-PATH): AiPlayerbot.Rescue.RepopDeadOnly (cohort modes, default 0). RepopAction had no death
# gate (A3 W11, design F2): "unstuck" repopped ALIVE bots (race start / homebind teleport, group leave) whenever the hearthstone
# was not useful. With the key the repop is not useful for an alive bot on its own; "unstuck" asks for QuestRescue, else a reset.
# Pins: the pure policy, the gate in isUseful and in unstuck (the same rule), the fallback order, the log fields, and that
# every legacy path stays for bots the key does not apply to.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "repop dead only: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "repop dead only: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "repop dead only: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "repop dead only: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from `first` to the next `second` after it.
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "repop dead only: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "repop dead only: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("RepopDeadOnlyPolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/ReleaseSpiritAction.h" release_h)
read_source("strategy/actions/UnstuckAction.cpp" unstuck)

# 1. Pure policy: no game types, no config, no log; the gate and the alive step.
forbid_text("${policy}" "#include \"playerbot/" "the policy includes a game header")
forbid_text("${policy}" "sPlayerbotAIConfig" "the policy reads config")
forbid_text("${policy}" "sLog" "the policy logs")
forbid_text("${policy}" "Player*" "the policy takes a game type")
require_text("${policy}" "namespace ai::repop_dead_only" "own namespace")
require_text("${policy}" "inline bool BlocksRepop(bool alive, bool realPlayerMaster)\n{\n    return alive && !realPlayerMaster;\n}" "the gate: alive and no real player")
require_text("${policy}" "inline Step AliveStep(bool questRescueQueued)\n{\n    return questRescueQueued ? Step::QuestRescue : Step::Reset;\n}" "QuestRescue, else reset (OB-00 decision 2)")

# 2. Config: default 0, cohort clamp, echo sorted, dist.
require_text("${config_h}" "uint32 rescueRepopDeadOnly = 0;" "default off")
require_text("${config_cpp}" "#include \"playerbot/RepopDeadOnlyPolicy.h\"" "loader include")
require_text("${config_cpp}" "rescueRepopDeadOnly = ai::repop_dead_only::ClampMode(config.GetIntDefault(\"AiPlayerbot.Rescue.RepopDeadOnly\", 0));" "mode loader")
require_order("${config_cpp}" "{\"QuestRescue.KeepRequestWhenFull\", Value(questRescueKeepRequestWhenFull)}," "{\"Rescue.RepopDeadOnly\", Value(rescueRepopDeadOnly)}," "echo sorted after QuestRescue.*")
require_order("${config_cpp}" "{\"Rescue.RepopDeadOnly\", Value(rescueRepopDeadOnly)}," "{\"Revive.CorpseRunFirst\", Value(reviveCorpseRunFirst)}," "echo sorted before Revive.*")
require_text("${conf_dist}" "\nAiPlayerbot.Rescue.RepopDeadOnly = 0\n" "dist default 0")

# 3. The repop gate: RepopAction::isUseful, after the BG and active-master rules, before the legacy yes.
between("${release_h}" "class RepopAction : public SpiritHealerAction" "class SelfResurrectAction" repop_class)
between("${repop_class}" "virtual bool isUseful() override" "\n    };" repop_useful)
require_order("${repop_useful}" "if (bot->InBattleGround())" "if (ai->HasActivePlayerMaster())" "legacy rules first")
require_order("${repop_useful}" "if (ai->HasActivePlayerMaster())" "ai::cohort::Applies(sPlayerbotAIConfig.rescueRepopDeadOnly, bot->GetGUIDLow())" "gate after the active master rule")
require_text("${repop_useful}" "ai::repop_dead_only::BlocksRepop(bot->IsAlive(), ai->HasRealPlayerMaster()))\n                return false;\n\n            return true;" "gate before the legacy yes")
require_text("${release_h}" "#include \"playerbot/RepopDeadOnlyPolicy.h\"" "release spirit include")

# 4. unstuck: the same gate, then QuestRescue or reset; the hearthstone first and the legacy repop otherwise.
between("${unstuck}" "auto const repopBlocked = [&]()" "};" blocked)
require_text("${blocked}" "ai::cohort::Applies(sPlayerbotAIConfig.rescueRepopDeadOnly, bot->GetGUIDLow()) &&\n            ai::repop_dead_only::BlocksRepop(bot->IsAlive(), ai->HasRealPlayerMaster())" "the same gate as isUseful")
between("${unstuck}" "auto const aliveFallback = [&](ai::repop_dead_only::Source src)" "\n    };" fallback)
require_order("${fallback}" "ai->RequestQuestRescue(now);" "rd::Step const step = rd::AliveStep(ai->HasQuestRescueRequest());" "ask first, then read the pending request")
require_text("${fallback}" "[Rescue] bot=%u cohort=%c level=%u map=%u zone=%u src=%s stage=%s alive=1" "rescue line")
require_text("${fallback}" "[RepopDeadOnlyMin] move_long_quest_rescue=%u move_long_reset=%u combat_long_quest_rescue=%u combat_long_reset=%u" "minute line")
require_text("${fallback}" "return ai->DoSpecificAction(\"reset\", event, true);" "reset when no rescue is queued")
forbid_text("${fallback}" "\"repop\"" "no repop in the alive fallback")
foreach(src MoveLong CombatLong)
  require_text("${unstuck}" "            if (repopBlocked())\n                return aliveFallback(ai::repop_dead_only::Source::${src});\n            return ai->DoSpecificAction(\"repop\", event, true);" "${src}: gate before the legacy repop")
endforeach()
between("${unstuck}" "if (source.find(\"move long stuck\") != std::string::npos)" "// Handle combat stuck scenarios" move_long)
require_order("${move_long}" "if (AI_VALUE2(bool, \"action useful\", \"hearthstone\") && bot->IsAlive())" "if (repopBlocked())" "move long: hearthstone first")
between("${unstuck}" "if (source.find(\"combat long stuck\") != std::string::npos)" "// Fallback to reset" combat_long)
require_order("${combat_long}" "if (AI_VALUE2(bool, \"action useful\", \"hearthstone\") && bot->IsAlive())" "if (repopBlocked())" "combat long: hearthstone first")

# 5. QuestRescue keeps its own per-bot limit (the reset fallback depends on it).
require_text("${ai_h}" "if (!lastQuestRescue.load() || now - lastQuestRescue.load() >= ai::quest_search::RescuePerBotSeconds)\n            questRescueRequested = true;" "QuestRescue per-bot limit")

message(STATUS "repop dead only source contract: ok")
