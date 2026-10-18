cmake_policy(VERSION 3.16)
# twow-repo#602 ROT-01 R06a (AiPlayerbot.Rotation.SpecFromPath, cohort mode 0/1/2, default 0; owner decision 18.10.2026:
# the spec identity of a bot comes from its planned premade path, never from the talent point majority). Pins:
#  - the pure policy SpecPathPolicy.h (no project includes, no locks, no statics, no config reads),
#  - the neutral default (member 0, plain load line, conf key = 0) and the tab key loader with its load check,
#  - exactly one changed spec function: AiFactory::GetPlayerSpecTab reads the path tab only behind the switch, for bots,
#    and otherwise returns the unchanged point majority (GetPlayerMajorityTab),
#  - the role filter of ChangeTalentsAction (off branch textually unchanged; by path: tab key + tank paths),
#  - the dungeon finder respec: a persistent roster member is not respecced with the switch on,
#  - [RotRole] carries maj= next to tab=,
#  - the shipped premade block has one PremadeSpecTab key per path.
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
    message(FATAL_ERROR "#602 R06a SpecFromPath: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#602 R06a SpecFromPath: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#602 R06a SpecFromPath: order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

# The part of `text` from `start` up to the first `end` after it (both required).
function(region text start end out)
  string(FIND "${text}" "${start}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "#602 R06a SpecFromPath: missing region start: ${start}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(LENGTH "${start}" start_length)
  string(SUBSTRING "${rest}" ${start_length} -1 tail)
  string(FIND "${tail}" "${end}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#602 R06a SpecFromPath: missing region end: ${end}")
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
    message(FATAL_ERROR "#602 R06a SpecFromPath: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()


read_source("SpecPathPolicy.h" policy)
read_source("AiFactory.cpp" factory)
read_source("AiFactory.h" factory_h)
read_source("Talentspec.h" talentspec_h)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotScripts.cpp" scripts)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("RotCastPolicy.h" rot_policy)
read_source("strategy/actions/ChangeTalentsAction.cpp" talents_cpp)
read_source("strategy/actions/ChangeTalentsAction.h" talents_h)

# 1. Pure policy.
require_text("${policy}" "namespace ai::spec_path\n" "policy namespace")
foreach(token "#include \"" "mutex" "atomic" "static " "sPlayerbotAIConfig" "sLog" "thread_local")
  forbid_text("${policy}" "${token}" "token in the pure policy")
endforeach()
require_text("${policy}" "constexpr std::uint32_t MinLevel = 10;" "path tab from level 10 (OB-00 18.10.2026)")
require_text("${policy}" "    return applies && level >= MinLevel ? ValidTab(pathTab) : -1;" "switch, level and valid tab")

# 2. Neutral default and loader.
require_text("${config_h}" "    uint32 rotationSpecFromPath = 0;  // twow-repo#602 ROT-01 R06a:" "member default 0")
require_text("${config_cpp}" "\n    rotationSpecFromPath = config.GetIntDefault(\"AiPlayerbot.Rotation.SpecFromPath\", 0);\n" "plain load line")
require_text("${conf_dist}" "\nAiPlayerbot.Rotation.SpecFromPath = 0\n" "conf key = 0")
foreach(value 1 2)
  forbid_text("${conf_dist}" "AiPlayerbot.Rotation.SpecFromPath = ${value}" "conf key on")
endforeach()
require_order("${conf_dist}" "AiPlayerbot.Rotation.CastTrace = 0" "AiPlayerbot.Rotation.SpecFromPath = 0" "in the Rotation block, not the Loot block")
require_text("${talentspec_h}" "        int tab = -1;  // twow-repo#602 R06a:" "TalentPath::tab, -1 = not given")
require_text("${config_cpp}" "tabKey << \"AiPlayerbot.PremadeSpecTab.\" << cls << \".\" << spec;" "tab key name")
require_text("${config_cpp}" "talentPath.tab = ai::spec_path::ValidTab(config.GetIntDefault(tabKey.str().c_str(), -1));" "tab key read, default -1")
require_order("${config_cpp}" "int const firstLinkTree = talentPath.talentSpec.front().highestTree();" "ai::spec_path::TabAgreesWithFirstLink(talentPath.tab, firstLinkTree)" "load check against the level 10 link")
require_text("${config_cpp}" "ai::spec_path::FormatLoadedLine(specPathCount, specPathWithTab, specPathMismatches)" "[SpecPath] loaded line")

# 3. Exactly one changed spec function.
region("${factory}" "int AiFactory::GetPlayerSpecTab(const Player* bot)\n{\n" "\nint AiFactory::GetPlayerMajorityTab(" spec_tab)
require_text("${spec_tab}" "    if (sPlayerbotAIConfig.rotationSpecFromPath && bot && GetBotAI(bot) &&\n        ai::cohort::Applies(sPlayerbotAIConfig.rotationSpecFromPath, bot->GetGUIDLow()) &&\n        sRandomPlayerbotMgr.IsRandomBot(const_cast<Player*>(bot)))\n" "switch first; bots, cohort, random/roster bots only (not a player's own bots)")
require_order("${spec_tab}" "ai::spec_path::PathTabFor(true, bot->GetLevel(), PremadePathTab(bot));" "    return GetPlayerMajorityTab(bot);\n" "path tab before the majority")
count_text("${spec_tab}" "return " 2 "returns of GetPlayerSpecTab (path tab or majority)")
region("${factory}" "int AiFactory::GetPlayerMajorityTab(const Player* bot)\n{\n" "\nstd::map<uint32, int32> AiFactory::GetPlayerSpecTabs(" majority)
require_text("${majority}" "            if (tab == -1 || max < tabs[i])\n" "majority rule unchanged (ties: the lower tab)")
forbid_text("${majority}" "rotationSpecFromPath" "switch in the majority rule")
count_text("${factory}" "sPlayerbotAIConfig.rotationSpecFromPath" 2 "switch reads in AiFactory.cpp (gate + cohort)")
require_text("${factory_h}" "    static int GetPlayerMajorityTab(const Player* player);\n" "majority accessor")
require_text("${factory_h}" "    static int GetPremadePathTab(const Player* player);\n" "path tab helper (OB-10 18.10.2026)")

# 4. Role filter and re-roll.
region("${talents_cpp}" "std::vector<TalentPath*> ChangeTalentsAction::getPremadePaths(uint8 cls, std::string findName, BotRoles role, bool byPath)\n{\n" "\nbool ChangeTalentsAction::SpecFromPath(" filter)
require_text("${filter}" "        if (!byPath)\n        {\n            if (role != BotRoles::BOT_ROLE_NONE && AiFactory::GetPlayerRoles(cls, path.talentSpec.back().highestTree()) != role)\n                continue;\n        }\n" "off branch unchanged")
require_order("${filter}" "ai::tank_path::IsTankPath(cls, path.name) ? BotRoles::BOT_ROLE_TANK" "ai::spec_path::PathRoleTab(path.tab, path.talentSpec.back().highestTree())" "by path: tank paths, then the tab key")
require_text("${talents_h}" "BotRoles role = BotRoles::BOT_ROLE_NONE, bool byPath = false);" "byPath defaults to the old filter")
require_text("${talents_cpp}" "        bool const byPath = SpecFromPath(bot);\n" "re-roll follows the switch")
require_text("${talents_cpp}" "            paths = getPremadePaths(bot->getClass(), \"\", role, byPath);\n" "role pick by path")
require_text("${talents_cpp}" "return byPath ? ai::spec_path::PathRoleTab(p->tab, lastLinkTree) : lastLinkTree;" "same-tree filter by path")

require_text("${talents_cpp}" "ai::cohort::Applies(sPlayerbotAIConfig.rotationSpecFromPath, bot->GetGUIDLow()) &&
        sRandomPlayerbotMgr.IsRandomBot(const_cast<Player*>(bot));" "role filter follows the same bot rule")

# 5. Dungeon finder: a roster member whose planned spec cannot fill the role is refused before any change.
region("${scripts}" "        void SetForcedRole(Player* bot, uint8 role) override\n        {\n" "\n            ai->ResetStrategies();\n" forced_role)
require_text("${forced_role}" "            bool const specFromPath = ChangeTalentsAction::SpecFromPath(bot);\n" "same bot rule as GetPlayerSpecTab")
require_text("${forced_role}" "            if (role != BotRoles::BOT_ROLE_NONE && specFromPath &&\n                sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&\n                !(AiFactory::GetPlayerRoles(bot) & role))\n                return;\n" "roster member refused with the switch")
require_order("${forced_role}" "!(AiFactory::GetPlayerRoles(bot) & role))\n                return;" "            ai->SetForcedRole(role);" "refusal before the forced role is set (no stuck role, no churn)")
require_text("${forced_role}" "ChangeTalentsAction::getPremadePaths(bot->getClass(), \"\", (BotRoles)role, specFromPath).empty()" "reachability by path with the switch")
forbid_text("${forced_role}" "!(specFromPath && sRandomPlayerbotMgr.IsPersistentRosterMember" "exemption after the role is set (left the probed role in the world)")

# 6. [RotRole] measures the flip.
require_text("${rot_policy}" "cohort=%c tab=%d maj=%d path=%s" "maj= next to tab=")
require_text("${ai_cpp}" "^ (uint64(uint8(majority)) << 56);" "a majority flip changes the [RotRole] signature")
require_text("${ai_cpp}" "ai::cohort::Letter(guid), tab, majority, AiFactory::GetPremadePathName(bot)," "[RotRole] passes the majority")

# 7. Shipped premade block: one tab key per path.
count_text("${conf_dist}" "\nAiPlayerbot.PremadeSpecTab." 30 "PremadeSpecTab keys in the shipped premade block")
count_text("${conf_dist}" "\nAiPlayerbot.PremadeSpecName." 30 "premade paths in the shipped block")

message(STATUS "SPEC_PATH_SOURCE_CONTRACT=PASS")
