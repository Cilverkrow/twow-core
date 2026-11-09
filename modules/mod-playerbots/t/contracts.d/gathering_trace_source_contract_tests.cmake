# twow-repo#333 T28-M (plan A3-PLAN-ZIEL T28, OB-00 09.11.2026; CLI-BERUFE): AiPlayerbot.Gathering.Trace, 0 off (default), 1 on,
# unknown -> 0 with an error line, ConfigEcho. A pure measurement: one "[Gathering] event=share" line per roster bot on its own
# and online hour with the time of its gathering purposes and profession trips, its gathering / craft casts and its gathering
# skills. Pins: the switch (dist, member, read, clamp, echo in sorted position), the pure policy (no game types, no config, no
# logging; field order), the counter sites (LootAction, ProfessionCraftAction), the sampler (a never-firing trigger of the
# "default" strategy, added only with the key on), the unit test registration and the neutrality: the new code writes no AI
# value, travel target, target or strategy, and PlayerbotAI::UpdateAI gets nothing.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

set(TAG "#333 T28-M Gathering.Trace")

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(read_module path out_var)
  file(READ "${PB_MODULE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "${TAG}: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from the first `begin` to the next `end` after it (both included); fatal when either is missing.
function(between text begin end out_var)
  string(FIND "${text}" "${begin}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing region start: ${begin}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing region end after '${begin}': ${end}")
  endif()
  string(LENGTH "${end}" end_length)
  math(EXPR b "${b} + ${end_length}")
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

# Occurrences of needle in text (string(FIND) loop, no regex).
function(count_text text needle out_var)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_length)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${needle_length}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" n)
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "${TAG}: ${description}: '${needle}' found ${n} times, expected ${expected}")
  endif()
endfunction()

read_source("aiplayerbot.conf.dist.in" dist)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("GatheringTracePolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/triggers/ProfessionUseTriggers.h" trig_h)
read_source("strategy/triggers/ProfessionUseTriggers.cpp" trig_cpp)
read_source("strategy/triggers/TriggerContext.h" trig_ctx)
read_source("strategy/generic/WorldPacketHandlerStrategy.cpp" default_strategy)
read_source("strategy/actions/LootAction.cpp" loot_cpp)
read_source("strategy/actions/ProfessionUseActions.cpp" craft_cpp)
read_module("tests.cmake" tests_cmake)
read_module("t/gathering_trace_policy_tests.cpp" unit_test)

# 1. The switch: documented next to the profession keys, default 0, line format; member 0; read with the > 1 clamp and its error
#    line; ConfigEcho entry between its sorted neighbours.
require_text("${dist}" "# twow-repo#333 T28-M (CLI-BERUFE 09.11.2026): " "dist comment header")
require_text("${dist}" "\nAiPlayerbot.Gathering.Trace = 0\n" "dist key, default 0")
forbid_text("${dist}" "\nAiPlayerbot.Gathering.Trace = 1" "dist default 1")
require_text("${dist}" "#   [Gathering] event=share bot= level= play_s= purpose_s= trip_s= cast_s= opens_herb= opens_ore= opens_skin= crafts=\n#     s182=<value>/<max> s186=<value>/<max> s393=<value>/<max>\n" "dist line format")
require_order("${dist}" "\nAiPlayerbot.ProfessionUse.TraceCooldownSeconds = 300\n# twow-repo#333 T28-M (CLI-BERUFE 09.11.2026): " "\nAiPlayerbot.Gathering.Trace = 0\n" "dist block right after the ProfessionUse.Trace keys")
require_text("${config_h}" "    uint32 gatheringTrace = 0;\n" "config member, default 0")
require_text("${config_cpp}" "    gatheringTrace = config.GetIntDefault(\"AiPlayerbot.Gathering.Trace\", 0);\n    if (gatheringTrace > 1)\n    {\n        sLog.outError(\"[Gathering] config key=Trace value=%u result=off (0 off, 1 on)\", gatheringTrace);\n        gatheringTrace = 0;\n    }\n" "config read, > 1 is off with the error line")
require_order("${config_cpp}" "professionUseTraceCooldownSeconds = std::max<uint32>(1, config.GetIntDefault(" "gatheringTrace = config.GetIntDefault(" "read next to the ProfessionUse.Trace loaders")
require_count("${config_cpp}" "{\"Gathering.Trace\", Value(gatheringTrace)}," 1 "ConfigEcho entry once")
require_order("${config_cpp}" "{\"Gather.QuestHerbFilter\", Value(gatherQuestHerbFilter)},\n" "{\"Gathering.Trace\", Value(gatheringTrace)},\n" "ConfigEcho after Gather.QuestHerbFilter")
require_order("${config_cpp}" "{\"Gathering.Trace\", Value(gatheringTrace)},\n" "{\"Grind.GreyByLevel\", Value(grindGreyByLevel)},\n" "ConfigEcho before Grind.GreyByLevel")

# 2. The pure policy: no game headers, no config, no logging; the overlap rule (trip first), the hour, the line's field order.
forbid_text("${policy}" "#include \"playerbot/" "game headers in the pure policy")
require_count("${policy}" "#include \"" 0 "only standard headers (<...>) in the pure policy")
forbid_text("${policy}" "sPlayerbotAIConfig" "config read in the pure policy")
forbid_text("${policy}" "sLog" "logging in the pure policy")
forbid_text("${policy}" "Player*" "game types in the pure policy")
require_text("${policy}" "namespace ai::gathering_trace" "policy namespace")
require_text("${policy}" "constexpr uint32_t WindowSeconds = 3600;" "one line per hour")
require_text("${policy}" "constexpr uint32_t SampleSeconds = 5;" "sample interval")
require_text("${policy}" "    if (tripActive)\n        return Activity::Trip;\n    return purposeActive ? Activity::Purpose : Activity::None;" "overlap rule: a trip outranks a purpose")
require_text("${policy}" "        if (lastActivity == Activity::Trip)\n            tripSeconds += elapsed;\n        else if (lastActivity == Activity::Purpose)\n            purposeSeconds += elapsed;" "left-point accounting, one bucket per interval")
require_text("${policy}" "        return now - start >= WindowSeconds;" "hour rollover")
require_text("${policy}" "\"[Gathering] event=share bot=%u level=%u play_s=%u purpose_s=%u trip_s=%u cast_s=%u opens_herb=%u opens_ore=%u \"\n        \"opens_skin=%u crafts=%u s182=%u/%u s186=%u/%u s393=%u/%u\"" "line format, field order")
foreach(spell "kHerbGatheringSpell = 2366;" "kMiningSpell = 2575;" "kSkinningSpell = 8613;" "kCreatureHerbSpell = 32605;" "kCreatureOreSpell = 32606;")
  require_text("${policy}" "constexpr uint32_t ${spell}" "gathering spell ${spell}")
endforeach()

# 3. The window lives on the bot (own thread), next to the [ItemUse] counters.
require_text("${ai_h}" "#include \"playerbot/GatheringTracePolicy.h\"" "policy include in PlayerbotAI.h")
require_text("${ai_h}" "    ai::gathering_trace::Window& GetGatheringTrace() { return gatheringTrace; }\n" "window accessor")
require_text("${ai_h}" "    ai::gathering_trace::Window gatheringTrace;" "window member")

# 4. Counter sites: one key test each, the call after the cast started.
#    LootAction.cpp OpenLootAction::Execute: a cast DoLoot started (castDuration set only by a started CastSpell); the kind from
#    the running generic spell (HERB_GATHERING / MINING / SKINNING on game objects and corpses, 32605 / 32606 on creatures).
between("${loot_cpp}" "bool OpenLootAction::Execute(Event& event)" "\n}\n" open_body)
require_text("${open_body}" "    bool result = DoLoot(lootObject);\n    // twow-repo#333 T28-M (AiPlayerbot.Gathering.Trace): a cast DoLoot started; a herb / ore / skinning cast (also on a creature) counts.\n    if (sPlayerbotAIConfig.gatheringTrace && result && castDuration)\n        GatheringTraceOpen(ai, castDuration);\n    if (result)\n    {\n" "gathering open site after DoLoot")
require_count("${loot_cpp}" "sPlayerbotAIConfig.gatheringTrace" 1 "one key test in LootAction.cpp")
#    ProfessionUseActions.cpp ProfessionCraftAction::Execute: the started RealReagents craft cast.
between("${craft_cpp}" "bool const ok = ai->CastSpell(spellId, bot, nullptr, true, &castDuration);" "return ok;" started)
require_text("${started}" "            sPlayerbotAIConfig.logEvent(ai, \"CraftCastStarted\", spell->SpellName[0], std::to_string(spellId));\n            // twow-repo#333 T28-M (AiPlayerbot.Gathering.Trace): a started craft cast and its cast time.\n            if (sPlayerbotAIConfig.gatheringTrace)\n                GatheringTraceCraft(ai, castDuration);\n" "craft site after a started cast")
require_order("${started}" "if (ok)\n        {" "GatheringTraceCraft(ai, castDuration);" "craft counted only after a started cast")
require_count("${craft_cpp}" "sPlayerbotAIConfig.gatheringTrace" 1 "one key test in ProfessionUseActions.cpp")

# 5. The sampler: a trigger of the "default" strategy (every engine: its combat and dead triggers are the non-combat ones),
#    added only with the key on, without a handler; IsActive tests the key first, keeps the roster-on-its-own scope, samples,
#    writes the line when the hour is due and always returns false.
require_text("${trig_h}" "GatheringTraceTrigger(PlayerbotAI* ai) : Trigger(ai, \"gathering trace\", int(ai::gathering_trace::SampleSeconds)) {}" "trigger name and interval")
require_text("${trig_ctx}" "creators[\"gathering trace\"] = [](PlayerbotAI* ai) { return new GatheringTraceTrigger(ai); };" "trigger registered")
set(node "    if (sPlayerbotAIConfig.gatheringTrace)\n        triggers.push_back(new TriggerNode(\n            \"gathering trace\",\n            NextAction::array(0, NULL)));")
require_text("${default_strategy}" "${node}" "node only with the key on, no handler")
require_order("${default_strategy}" "void WorldPacketHandlerStrategy::InitNonCombatTriggers" "${node}" "node in the non-combat triggers")
require_order("${default_strategy}" "${node}" "void WorldPacketHandlerStrategy::InitCombatTriggers" "node before the combat triggers")
require_text("${default_strategy}" "void WorldPacketHandlerStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)\n{\n    InitNonCombatTriggers(triggers);\n}" "combat engine reuses the node")
require_text("${default_strategy}" "void WorldPacketHandlerStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)\n{\n    InitNonCombatTriggers(triggers);\n}" "dead engine reuses the node")
require_count("${default_strategy}" "sPlayerbotAIConfig.gatheringTrace" 1 "one key test in the default strategy")

between("${trig_cpp}" "// twow-repo#333 T28-M begin" "// twow-repo#333 T28-M end" t28)
between("${t28}" "bool GatheringTraceTrigger::IsActive()" "\n}\n" sampler)
require_text("${sampler}" "bool GatheringTraceTrigger::IsActive()\n{\n    if (!sPlayerbotAIConfig.gatheringTrace)\n        return false;\n" "key tested first")
require_text("${sampler}" "if (!bot || !IsRosterBotOnItsOwn(ai))\n    {\n        window.Reset();\n        return false;\n    }" "scope: a roster bot on its own")
require_text("${sampler}" "ProfessionTripKindOf(travelTarget) != ai::profession_travel::kTripKindNone" "trip = profession trip travel target")
require_text("${sampler}" "if (!window.Sample(uint32(time(nullptr)), ai->GetGatherPurpose().Active(), trip))\n        return false;" "sample: purpose and trip")
require_text("${sampler}" "sLog.outBasic(\"%s\", ai::gathering_trace::ShareLine(bot->GetGUIDLow(), bot->GetLevel(), window, herbalism, mining, skinning).c_str());\n    window.Roll();\n    return false;\n}" "the hour line, then the next window")
forbid_text("${sampler}" "return true" "a firing sampler")
require_count("${trig_cpp}" "sPlayerbotAIConfig.gatheringTrace" 1 "one key test in the trigger file")
require_text("${t28}" "Spell* const spell = bot ? bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;" "open kind from the running generic spell")
require_text("${t28}" "NoteOpen(ai::gathering_trace::OpenKindOfSpell(spell->m_spellInfo->Id), castDurationMs);" "open counted by its spell")
require_text("${t28}" "ai->GetGatheringTrace().NoteCraft(castDurationMs);" "craft counted")

# 6. Neutrality: the new code reads values and writes only its window and the line - no AI value, travel target, target,
#    strategy, movement or cast; nothing in PlayerbotAI.cpp (UpdateAI's per-pass hooks stay as they are).
foreach(write "SET_AI_VALUE" "RESET_AI_VALUE" "->Set(" "->Reset(" "SetStatus(" "SetTarget(" "SetExpire" "ClearValues" "ChangeStrategy"
    "ResetStrategies" "CastSpell(" "DoSpecificAction" "MoveTo" "TellMaster" "SetSelection" "->Get() =")
  forbid_text("${t28}" "${write}" "write in the T28-M code")
endforeach()
# Writes through a reference and direct bot side effects (review 09.11.); the one Reset is the bot's own window.
foreach(write ".Set(" "Interrupt" "Stop" "bot->Set" "->Remove(" "->Clear" "Clear(")
  forbid_text("${t28}" "${write}" "side effect in the T28-M code")
endforeach()
require_count("${t28}" ".Reset(" 1 "only the window is reset")
require_count("${t28}" "window.Reset();" 1 "only the window is reset")
forbid_text("${ai_cpp}" "gatheringTrace" "T28-M code in PlayerbotAI.cpp")
forbid_text("${ai_cpp}" "GatheringTrace" "T28-M code in PlayerbotAI.cpp")
forbid_text("${ai_cpp}" "gathering_trace::" "T28-M code in PlayerbotAI.cpp")

# 7. The unit test, registered like profession_travel_policy.
require_text("${tests_cmake}" "add_executable(gathering_trace_policy_tests\n  \"\${PB_MODULE_DIR}/t/gathering_trace_policy_tests.cpp\")\n\ntarget_include_directories(gathering_trace_policy_tests PRIVATE\n  \"\${PB_MODULE_DIR}/src/playerbot\")\n" "unit test target")
require_text("${tests_cmake}" "add_test(NAME gathering_trace_policy\n  COMMAND gathering_trace_policy_tests\n  WORKING_DIRECTORY \"\${CMAKE_BINARY_DIR}\")" "unit test registered")
require_text("${unit_test}" "#include \"GatheringTracePolicy.h\"" "unit test includes the policy")
require_text("${unit_test}" "both: the trip counts, the purpose not" "unit test: overlap rule")
require_text("${unit_test}" "line format and field order" "unit test: line format")

message(STATUS "${TAG} source contract passed")
