cmake_policy(VERSION 3.16)
# twow-repo#602 ROT-01 D0 (AiPlayerbot.Rotation.CastTrace, default 0): cast diagnostics, measurement only. Pins:
#  - the pure policies RotCastPolicy.h / CohortPolicy.h (no project includes, no locks, no statics, no config reads)
#    and that neither they nor RotCastTrace.h enter the PlayerbotAI.h / PlayerbotAIConfig.h (botpch) chain,
#  - the neutral default (member false, plain load line, conf key = 0),
#  - every hook behind the switch with the switch-off path unchanged: CastSpellAction / CastPetSpellAction isPossible
#    (RefreshSpellIdOnLearn() stays the first statement, the off path calls CanCastSpell exactly as before), Execute,
#    the flush after HandleQueuedBotPackets, SMSG_CAST_RESULT before the handler queue, [RotRole] after the engine
#    init loop, [RotPull] after the unchanged AttackAnythingAction event, ReportRotCast after ReportQuestIdle,
#  - bounded output: every [Rot*] line has a budget or is once per minute/hour; no shared_mutex, no logEvent.
# Reads only named files below modules/mod-playerbots/src/playerbot (never a glob, never src/modules/Eluna).

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#602 CastTrace: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#602 CastTrace: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#602 CastTrace: order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

# The part of `text` from `start` up to the first `end` after it (both required).
function(region text start end out)
  string(FIND "${text}" "${start}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "#602 CastTrace: missing region start: ${start}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(LENGTH "${start}" start_length)
  string(SUBSTRING "${rest}" ${start_length} -1 tail)
  string(FIND "${tail}" "${end}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#602 CastTrace: missing region end: ${end}")
  endif()
  math(EXPR length "${start_length} + ${end_at}")
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR next "${at} + ${len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#602 CastTrace: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("RotCastPolicy.h" policy)
read_source("CohortPolicy.h" cohort)
read_source("RotCastTrace.h" rt_h)
read_source("RotCastTrace.cpp" rt_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" rpm_cpp)
read_source("strategy/actions/GenericSpellActions.h" spell_h)
read_source("strategy/actions/GenericSpellActions.cpp" spell_cpp)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_target)

# 1. Pure policies.
require_text("${policy}" "namespace ai::rot_cast\n" "policy namespace")
require_text("${cohort}" "namespace ai::cohort\n" "cohort namespace")
foreach(header policy cohort)
  foreach(token "#include \"" "mutex" "atomic" "static " "sPlayerbotAIConfig" "sLog" "thread_local")
    forbid_text("${${header}}" "${token}" "token in a pure policy header (${header})")
  endforeach()
endforeach()
require_text("${cohort}" "inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)\n{\n    return mode == ModeAll || (mode == ModeOnlyB && InB(guidLow)) || (mode == ModeOnlyA && !InB(guidLow));\n}" "cohort rule 0 off / 1 all / 2 only B / 3 only A (#601 crossover)")
foreach(header "RotCastPolicy.h" "CohortPolicy.h" "RotCastTrace.h")
  forbid_text("${ai_h}" "${header}" "include in PlayerbotAI.h (botpch chain)")
  forbid_text("${config_h}" "${header}" "include in PlayerbotAIConfig.h (botpch chain)")
endforeach()

# 2. Neutral default.
require_text("${config_h}" "    bool rotationCastTrace = false;  // twow-repo#602 ROT-01 D0:" "member default off")
require_text("${config_cpp}" "\n    rotationCastTrace = config.GetBoolDefault(\"AiPlayerbot.Rotation.CastTrace\", false);\n" "plain load line")
require_text("${conf_dist}" "\nAiPlayerbot.Rotation.CastTrace = 0\n" "conf key = 0")
forbid_text("${conf_dist}" "AiPlayerbot.Rotation.CastTrace = 1" "conf key on")
require_order("${conf_dist}" "AiPlayerbot.CastSpell.RefreshOnLearn = 0" "AiPlayerbot.Rotation.CastTrace = 0" "key next to the cast switches")
foreach(tag "[RotCast] window_s=" "[RotCastTotal] window_min=60" "[RotCastUnknown] bot=" "[RotRole] guid=" "[RotPull] bot=")
  require_text("${conf_dist}" "${tag}" "line documented in the conf")
endforeach()

# 3. isPossible: refresh first, off path unchanged, every early return counted.
region("${spell_cpp}" "bool CastSpellAction::isPossible()\n{\n" "\nbool CastSpellAction::RotEval(" base_possible)
require_text("${base_possible}" "bool CastSpellAction::isPossible()\n{\n    RefreshSpellIdOnLearn();\n" "refresh stays first (base)")
require_text("${base_possible}" "    if (!sPlayerbotAIConfig.rotationCastTrace)\n\t    return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true);\n" "base off path unchanged")
require_order("${base_possible}" "return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true);" "&result);" "off path before the traced call")
# The id-0 refusal is pinned verbatim by the A27 contract; it is counted one line earlier and is the only plain false.
require_text("${base_possible}" "            if (!spellId && sPlayerbotAIConfig.rotationCastTrace)\n                RotEval(false, ai::rot_cast::EvalNoSpellId);" "id-0 refusal counted")
require_order("${base_possible}" "RotEval(false, ai::rot_cast::EvalNoSpellId);" "            if (!spellId)\n                return false;" "counted before the unchanged refusal")
count_text("${base_possible}" "return false;" 1 "plain false in the base isPossible (only the A27 id-0 refusal)")
forbid_text("${base_possible}" "return true;" "uncounted true in the base isPossible")
# 7 = the six of D0 + the never-possible early-out (twow-repo#602 Rotation.NeverPossibleSkip, EvalNoSpellId; pinned by never_possible_skip)
count_text("${base_possible}" "return RotEval(" 7 "counted returns in the base isPossible")
region("${spell_cpp}" "bool CastPetSpellAction::isPossible()\n{\n" "\nbool CastAuraSpellAction::isUseful()" pet_possible)
require_text("${pet_possible}" "bool CastPetSpellAction::isPossible()\n{\n    RefreshSpellIdOnLearn();\n" "refresh stays first (pet)")
require_text("${pet_possible}" "                    if (!sPlayerbotAIConfig.rotationCastTrace)\n                        return ai->CanCastSpell(spellId, spellTarget, 0, true);\n" "pet off path unchanged")
require_text("${pet_possible}" "\n    return RotEval(false, why);\n}" "pet false counted with its reason")
forbid_text("${pet_possible}" "return false;" "uncounted false in the pet isPossible")
count_text("${spell_cpp}" "    RefreshSpellIdOnLearn();\n" 2 "refresh call sites (A27 contract)")

# 4. RotEval / RotStart: switch first, unknown once per bot + action under a budget; no lock in the spell actions.
region("${spell_cpp}" "bool CastSpellAction::RotEval(bool possible, uint16 code)\n{\n" "\nvoid CastSpellAction::RotStart(" rot_eval)
require_text("${rot_eval}" "{\n    if (!sPlayerbotAIConfig.rotationCastTrace)\n        return possible;\n" "RotEval off = return possible")
require_order("${rot_eval}" "rotCastUnknownLogged = true;" "TakeUnknownBudget(" "unknown flag before the budget (once per bot + action)")
region("${spell_cpp}" "void CastSpellAction::RotStart(uint16 code)\n{\n" "\n}\n" rot_start)
require_text("${rot_start}" "{\n    if (!sPlayerbotAIConfig.rotationCastTrace)\n        return;\n" "RotStart off = no-op")
region("${spell_cpp}" "bool CastSpellAction::Execute(Event& event)\n{\n" "\nbool CastSpellAction::isPossible()" execute)
count_text("${execute}" "RotStart(" 2 "Execute outcomes counted")
require_order("${execute}" "RotStart(executed ? ai::rot_cast::StartOk : ai::rot_cast::StartFail);" "    if (executed)\n" "start counted before the duration")
forbid_text("${spell_cpp}" "std::mutex" "lock in spell actions")
require_text("${spell_h}" "        bool RotEval(bool possible, uint16 code);\n" "RotEval declaration")
require_text("${spell_h}" "        bool rotCastUnknownLogged = false;" "unknown flag member")

# 5. PlayerbotAI hooks.
region("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)\n{\n" "    if (parked)\n" update_ai)
require_order("${update_ai}" "HandleQueuedBotPackets();" "ai::rot_cast_rt::FlushIfDue();" "flush after the inbox")
require_text("${update_ai}" "    if (sPlayerbotAIConfig.rotationCastTrace)\n        ai::rot_cast_rt::FlushIfDue();\n" "flush behind the switch")
region("${ai_cpp}" "void PlayerbotAI::HandleBotOutgoingPacket(const WorldPacket& packet)\n{\n" "\nvoid PlayerbotAI::SpellInterrupted(" outgoing)
region("${outgoing}" "\n\tdefault:\n" "\n}\n" outgoing_default)
require_text("${outgoing_default}" "if (sPlayerbotAIConfig.rotationCastTrace && packet.GetOpcode() == SMSG_CAST_RESULT && bot)" "cast result behind the switch, in the default branch")
require_order("${outgoing_default}" "ai::rot_cast_rt::RecordServer(" "botOutgoingPacketHandlers.AddPacket(packet);" "counted before the handler queue")
count_text("${outgoing_default}" "botOutgoingPacketHandlers.AddPacket(packet);" 1 "the packet still reaches its handler once")
count_text("${outgoing}" "ai::rot_cast_rt::RecordServer(" 1 "one server count site")
forbid_text("${outgoing}" "case SMSG_CAST_RESULT" "a new case would bypass the handler queue")
region("${ai_cpp}" "void PlayerbotAI::ResetStrategies(bool autoLoad)\n{\n" "\nbool PlayerbotAI::IsRanged(" reset)
require_order("${reset}" "engines[i]->Init();" "if (sPlayerbotAIConfig.rotationCastTrace)" "[RotRole] after the engine init loop")
require_order("${reset}" "ai::rot_cast_rt::RoleLineDue(" "ai::rot_cast::FormatRoleLine(" "role line only when due")
count_text("${ai_cpp}" "sPlayerbotAIConfig.rotationCastTrace" 3 "switch reads in PlayerbotAI.cpp")

# 6. [RotPull]: after the unchanged event, before the pull branch.
require_order("${choose_target}" "sPlayerbotAIConfig.logEvent(ai, \"AttackAnythingAction\", grindName + \" (lvl \" + std::to_string(grindTarget->GetLevel()) + \")\", std::to_string(grindTarget->GetEntry()));" "ai::rot_cast::FormatPullLine(" "pull line after the bot_events entry")
require_order("${choose_target}" "ai::rot_cast::FormatPullLine(" "if (ai->HasStrategy(\"pull\", BotState::BOT_STATE_COMBAT))" "pull line before the pull branch")
require_text("${choose_target}" "if (sPlayerbotAIConfig.rotationCastTrace && ai::rot_cast_rt::TakePullBudget(uint32(time(nullptr))))" "pull line behind the switch and its budget")

# 7. World-thread report.
require_order("${rpm_cpp}" "    ReportQuestIdle();" "    ReportRotCast();" "report after ReportQuestIdle")
region("${rpm_cpp}" "void ReportRotCast()\n{\n" "\n}\n" report)
require_text("${report}" "    if (!sPlayerbotAIConfig.rotationCastTrace)\n    {\n        tracing = false;\n        return;" "report off = nothing")

# 8. Runtime: bounded, no shared_mutex, no logEvent, no config reads.
forbid_text("${rt_cpp}" "shared_mutex" "shared_mutex (twow-core#171)")
forbid_text("${rt_cpp}" "logEvent" "bot_events.csv writes")
forbid_text("${rt_cpp}" "sPlayerbotAIConfig" "config reads in the runtime")
foreach(cap "constexpr std::size_t BookCap = 8192;" "constexpr std::size_t HourCap = 16384;" "constexpr std::size_t RoleBookCap = 8192;"
    "constexpr std::uint32_t UnknownLinesPerMinute = 120;" "constexpr std::uint32_t RoleLinesPerMinute = 120;"
    "constexpr std::uint32_t PullLinesPerMinute = 300;" "constexpr std::size_t RowsPerMinute = 25;" "constexpr std::size_t RowsPerHour = 40;")
  require_text("${rt_cpp}" "${cap}" "bound")
endforeach()
require_text("${rt_cpp}" "thread_local Local tl;" "per-thread counters")

message(STATUS "ROT_CAST_TRACE_SOURCE_CONTRACT=PASS")
