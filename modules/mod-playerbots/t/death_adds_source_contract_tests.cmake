cmake_policy(VERSION 3.16)
# twow-repo#602 ROT-01 F6 (log fix, no switch; OB-10: before the X3 A/B): the adds column of deaths.csv was 0 in every row because the
# death clears the attacker set and every victim before OnDeath runs. Pins:
#  - the pure policy DeathAddsPolicy.h (std only, no project include, no lock, no config),
#  - the snapshot on the bot's own AI tick before DoNextAction, kept while the bot is dead, cleared out of combat,
#  - the deaths.csv writer merging live victims with the fresh snapshot, the killer never an add, the row layout unchanged.
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
    message(FATAL_ERROR "#602 F6 DeathAdds: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#602 F6 DeathAdds: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#602 F6 DeathAdds: order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text begin_marker end_marker out_var)
  string(FIND "${text}" "${begin_marker}" begin_at)
  if(begin_at EQUAL -1)
    message(FATAL_ERROR "#602 F6 DeathAdds: region start not found: ${begin_marker}")
  endif()
  string(SUBSTRING "${text}" ${begin_at} -1 tail)
  string(FIND "${tail}" "${end_marker}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#602 F6 DeathAdds: region end not found: ${end_marker}")
  endif()
  string(SUBSTRING "${tail}" 0 ${end_at} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("DeathAddsPolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)

# 1. Pure policy.
foreach(forbidden "#include \"" "mutex" "atomic" "static " "sPlayerbotAIConfig" "sLog")
  forbid_text("${policy}" "${forbidden}" "in the pure policy")
endforeach()
require_text("${policy}" "constexpr std::uint32_t RefreshMs = 1000;" "at most one snapshot a second")
require_text("${policy}" "constexpr std::uint32_t MaxAgeMs = 5000;" "snapshot freshness at OnDeath")
require_text("${policy}" "constexpr std::size_t MaxUnits = 16;" "bounded snapshot")
require_text("${ai_h}" "#include \"playerbot/DeathAddsPolicy.h\"" "policy included by PlayerbotAI.h")
require_text("${ai_h}" "    ai::death_adds::Snapshot deathAdds;" "one snapshot per bot")

# 2. Snapshot on the bot's own tick, before the engine (the DeathTrigger runs OnDeath from DoNextAction).
region("${ai_cpp}" "void PlayerbotAI::UpdateAIInternal(uint32 elapsed, bool minimal)\n{" "\n}\n" internal)
require_order("${internal}" "    UpdateDeathAddsSnapshot();\n" "\tDoNextAction(minimal);" "snapshot before DoNextAction")
region("${ai_cpp}" "void PlayerbotAI::UpdateDeathAddsSnapshot()\n{" "\n}\n" snap)
require_order("${snap}" "    if (!bot->IsAlive())\n        return;" "    if (!bot->IsInCombat())\n    {\n        deathAdds.Clear();" "kept while dead, cleared out of combat")
require_text("${snap}" "ai::death_adds::Due(deathAdds, nowMs, uint32(attackers.size()))" "throttled refresh")
require_text("${snap}" "unit->IsCreature() && !deathAdds.Add(" "creatures only, bounded")
forbid_text("${snap}" "AI_VALUE" "no AI value per refresh")

# 3. The writer.
region("${ai_cpp}" "            if (sPlayerbotAIConfig.hasLog(\"deaths.csv\"))\n" "                sPlayerbotAIConfig.log(\"deaths.csv\", out.str().c_str());" writer)
require_text("${writer}" "if (unit && unit->GetVictim() == bot)\n                        live.push_back(target.GetRawValue());" "live victims kept")
require_text("${writer}" "ai::death_adds::MergeAdds(live, deathAdds,\n                    ai::death_adds::Fresh(deathAdds, WorldTimer::getMSTime()), ctarget ? ctarget->GetObjectGuid().GetRawValue() : 0);" "fresh snapshot merged, killer excluded")
require_text("${writer}" "if (!unit || !unit->IsAlive())\n                        continue;" "only living adds")
require_order("${writer}" "out << \"\\\"\";" "out << \"\\\",\" << std::to_string(adds);" "row layout: quoted names, then the count")
