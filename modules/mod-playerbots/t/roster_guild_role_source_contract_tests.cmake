if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#485 / #518: fill roster guilds by role. The split is configuration (Tanks/Healers/Dps,
# default 0/0/0 = core#281 unchanged), the spread switches default off, OB-40's guilds.tsv is read
# once in PlayerbotAIConfig::Initialize. The deal runs in memory at the plan's recount (no SQL, no
# file IO, no other bot's Player*); invites, accepts and signatures of roster bots follow it.

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(text_between text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "Missing section start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(FIND "${rest}" "${end_marker}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "Missing section end after ${start_marker}: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end_at} section)
  set(${out} "${section}" PARENT_SCOPE)
endfunction()

# Tick-path hazards in the role fill: SQL, file IO, a writer lock of GuildMgr, a live Player*/Guild*
# or another bot's AI. Returns the first hit, "" when clean.
function(find_role_fill_hazard text found)
  set(${found} "" PARENT_SCOPE)
  foreach(forbidden "CharacterDatabase" "PQuery" "shared_mutex" "std::ifstream" "fopen(" "sObjectMgr.GetPlayer(" "GetGuildById(" "GetBotAI(" "GetMemberSlot(")
    string(FIND "${text}" "${forbidden}" at)
    if(NOT at EQUAL -1)
      set(${found} "${forbidden}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# The accept gate: the deal decides before the core's accept handler runs.
function(check_accept_gate text problem)
  set(${problem} "" PARENT_SCOPE)
  string(FIND "${text}" "RosterGuildPlan::AssignedGuild(bot->GetGUIDLow(), bot->GetTeam())" assigned_at)
  string(FIND "${text}" "if (assigned != guildId)" compare_at)
  string(FIND "${text}" "bot->GetSession()->HandleGuildAcceptOpcode(packet);" accept_at)
  if(assigned_at EQUAL -1 OR compare_at EQUAL -1 OR accept_at EQUAL -1)
    set(${problem} "accept gate missing" PARENT_SCOPE)
  elseif(NOT assigned_at LESS accept_at OR NOT compare_at LESS accept_at)
    set(${problem} "accept gate after the accept handler" PARENT_SCOPE)
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/RosterGuildRolePolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.cpp" create)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.h" create_h)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildAcceptAction.cpp" accept)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildManagementActions.cpp" manage)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildManagementActions.h" manage_h)
file(READ "${PB_SOURCE_DIR}/strategy/actions/PetitionSignAction.cpp" sign)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/GuildTriggers.cpp" triggers)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/GuildTriggers.h" triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/TriggerContext.h" trigger_context)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ActionContext.h" action_context)
file(READ "${PB_SOURCE_DIR}/strategy/generic/GuildStrategy.cpp" guild_strategy)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# 1. Pure policy (std only), included only by GuildCreateActions.cpp.
require_text("${policy}" "namespace ai::roster_guild_role" "policy namespace")
reject_text("${policy}" "#include \"" "game or playerbot include in the pure policy")
foreach(needle
    "return quota.tanks || quota.healers || quota.dps;"
    "return rosterPath && (QuotaActive(quota) || planLoaded);"
    "return std::make_tuple(perClass[a.cls], a.cls, Band(a.level), a.race, a.ordinal, a.guid) <"
    "c.Get(role), c.GetBand(role, band), i);"
    "inRole + 1 + MissingHealerClasses(counts, stats, member.cls) > quota.healers"
    "counts.Get(roleClass) >= SpreadCap(stats.Get(roleClass), stats.guilds)"
    "counts.GetCombo(combo) >= SpreadCap(stats.GetCombo(combo), stats.guilds)")
  require_text("${policy}" "${needle}" "role fill policy (guild_plan.py rules)")
endforeach()
file(GLOB_RECURSE module_sources "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(LENGTH module_sources module_source_count)
if(module_source_count LESS 100)
  message(FATAL_ERROR "module source scan found only ${module_source_count} files under ${PB_SOURCE_DIR}")
endif()
set(policy_users "")
foreach(source_file ${module_sources})
  file(READ "${source_file}" source_text)
  string(FIND "${source_text}" "RosterGuildRolePolicy.h\"" include_at)
  if(NOT include_at EQUAL -1)
    get_filename_component(source_name "${source_file}" NAME)
    list(APPEND policy_users "${source_name}")
  endif()
endforeach()
if(NOT policy_users STREQUAL "GuildCreateActions.cpp")
  message(FATAL_ERROR "RosterGuildRolePolicy.h must be included only from GuildCreateActions.cpp, found: ${policy_users}")
endif()

# 2. The deal at the recount: only with a quota or a plan, under the plan's mutex, from the player
#    cache and GuildMgr copies; per-call hazards rejected (negative probe below).
text_between("${create}" "// --- twow-repo#485 / #518: role fill of roster guilds (RosterGuildRolePolicy.h)" "void RecountRosterGuilds(RosterGuildState& state, time_t now)" role_helpers)
text_between("${create}" "// twow-repo#485 / #518: role fill of roster guilds.\nbool RosterGuildPlan::UsesRoleFill(" "// --- end of the roster guild plan" role_api)
foreach(needle
    "roster_guild_role::ParsePlan(sPlayerbotAIConfig.rosterGuildPlanLines, rejected)"
    "member.guild = sGuildMgr.GetPlayerGuildId(member.guid);"
    "roster_guild_role::Deal(quotaMembers, factionGuilds[f], quota, switches, target)"
    "state.assigned.emplace(item.first, item.second);"
    "[RosterGuild] event=deal faction=%s mode=%s guilds=%u dealt_without_guild=%u unplaced=%u unknown_role=%u plan_rows=%u"
    "[RosterGuild] event=roles faction=%s guild=%u tanks=%u healers=%u dps=%u unknown=%u healer_classes=%u tank_classes=%u quota=%u/%u/%u")
  require_text("${role_helpers}" "${needle}" "role deal at the recount")
endforeach()
require_text("${create}" "bool const roleFill = RoleFillConfigured();" "role fill only when configured")
require_order("${create}" "if (roleFill)\n            DealRosterGuildRoles(state, roleMembers, guilds, petitions, rosterFaction);" "state.builtAt = now;" "deal inside the recount")
# The traces only on change (no line per call).
require_text("${role_helpers}" "if (deal != state.tracedDeal[f])" "deal trace on change")
require_text("${role_helpers}" "if (traced != state.tracedRoles.end() && traced->second == line)" "roles trace on change")
foreach(section_name role_helpers role_api)
  find_role_fill_hazard("${${section_name}}" hazard)
  if(NOT hazard STREQUAL "")
    message(FATAL_ERROR "Forbidden in the role fill (${section_name}): ${hazard}")
  endif()
endforeach()
# Negative probe: the hazard scan must catch a live Player* lookup.
find_role_fill_hazard("        Player* other = sObjectMgr.GetPlayer(member.guid);" hazard_probe)
if(hazard_probe STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: live Player* in the role fill - the scan is broken")
endif()

# 3. Role source: the plan's role first, else the bot's own report (own thread, own Player*).
require_order("${role_helpers}" "if (planIt != state.plan.end())\n            return planIt->second.role;" "state.selfRoles.find(guid)" "plan role before the self report")
text_between("${role_api}" "uint8 RosterGuildPlan::ReportOwnRole(Player* bot)" "uint32 RosterGuildPlan::AssignedGuild(" report)
require_order("${report}" "AiFactory::GetPlayerRoles(bot)" "std::lock_guard<std::mutex> guard(state.lock);" "role computed before the lock")
reject_text("${report}" "state.dirty = true;" "a recount per role report")

# 4. Signing: lock order (GuildMgr copy before the plan's mutex), plan guild and role slot.
text_between("${role_api}" "bool RosterGuildPlan::MaySignForRole(" "void RosterGuildPlan::NoteJoined(" may_sign)
require_order("${may_sign}" "sGuildMgr.GetPetitionSignerGuids(offered.id, signers);" "std::lock_guard<std::mutex> guard(state.lock);" "signer copy before the plan's lock")
require_order("${may_sign}" "ReportOwnRole(bot);" "std::lock_guard<std::mutex> guard(state.lock);" "own role reported before the lock (std::mutex is not recursive)")
require_text("${may_sign}" "roster_guild_role::SamePlanGuild(ownLabel, PlanLabel(state, offered.ownerGuid.GetCounter()))" "plan guild of the charter")
require_text("${may_sign}" "roster_guild_role::CheckFit(charter, candidate, quota, RosterGuildSwitches()," "role slot and spread rules on the charter")
require_order("${may_sign}" "if (!RoleFillConfigured())\n        return true;" "ReportOwnRole(bot);" "no work without quota and plan")
require_order("${sign}" "roster_guild::DecideSign(" "RosterGuildPlan::MaySignForRole(bot, offered, reason)" "role gate after the founding rules")
require_order("${sign}" "else if (!IsRealPlayer(_inviter))" "RosterGuildPlan::MaySignForRole(bot, offered, reason)" "real players' charters untouched")

# 5. Accept: only the dealt guild, before the core's accept handler; real players' invites untouched.
require_text("${accept}" "bool const roleFill = accept && !IsRealPlayer(inviter) && RosterGuildPlan::UsesRoleFill(ai);" "accept gate scope")
check_accept_gate("${accept}" accept_problem)
if(NOT accept_problem STREQUAL "")
  message(FATAL_ERROR "Guild accept: ${accept_problem}")
endif()
# Negative probe: without the gate the check must fail.
string(REPLACE "RosterGuildPlan::AssignedGuild(bot->GetGUIDLow(), bot->GetTeam())" "0" accept_mutant "${accept}")
check_accept_gate("${accept_mutant}" accept_probe)
if(accept_probe STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: accept without the deal - the check is broken")
endif()
require_order("${accept}" "bot->GetSession()->HandleGuildAcceptOpcode(packet);" "RosterGuildPlan::NoteJoined(bot, guildId);" "joined trace after the accept")

# 6. Invite: inside the roster block of GuildManageNearbyAction, only into the dealt guild, no /say,
#    no other bot's AI, same faction, invite right and distance.
text_between("${manage}" "the one\n            // exception." "if (guild->GetMemberSize() >= sPlayerbotAIConfig.guildMaxBotLimit)\n            return false;" invite)
foreach(needle
    "if (!sRandomPlayerbotMgr.IsPersistentRosterMember(player->GetGUIDLow()) || !RosterGuildPlan::UsesRoleFill(ai))"
    "player->GetTeam() != bot->GetTeam()"
    "player->GetGuildIdInvited()"
    "!guild->HasRankRight(botMember->RankId, GR_RIGHT_INVITE)"
    "sServerFacade.GetDistance2d(bot, player) > sPlayerbotAIConfig.spellDistance"
    "[RosterGuild] event=invite bot=%u guild=%u member=%u")
  require_text("${invite}" "${needle}" "role fill invite")
endforeach()
require_order("${invite}" "RosterGuildPlan::AssignedGuild(player->GetGUIDLow(), player->GetTeam()) != bot->GetGuildId()" "ai->DoSpecificAction(\"guild invite\"" "dealt guild before the invite")
require_text("${invite}" "RosterGuildPlan::IsDue(ai, \"roster guild invite trace\", 600)" "invite trace throttled")
foreach(forbidden "->Say(" "GetBotAI(" "GetMaxPreferedGuildSize(")
  reject_text("${invite}" "${forbidden}" "chat or another bot's AI in the role fill invite")
endforeach()

# 7. Role report: trigger throttled by SnapshotSeconds, registered, in the guild strategy.
require_text("${triggers_h}" "Trigger(ai, \"roster guild role\", 60)" "role trigger looked at once a minute")
text_between("${triggers}" "bool RosterGuildRoleTrigger::IsActive()" "return true;" role_trigger)
require_order("${role_trigger}" "RosterGuildPlan::UsesRoleFill(ai)" "roster_guild::IsDue(now, lastRoleReport, roster_guild::SnapshotInterval(sPlayerbotAIConfig.rosterGuildSnapshotSeconds))" "switch before the throttle")
require_text("${role_trigger}" "lastRoleReport = now;" "role throttle advanced")
require_text("${trigger_context}" "creators[\"roster guild role\"] = [](PlayerbotAI* ai) { return new RosterGuildRoleTrigger(ai); };" "role trigger registered")
require_text("${action_context}" "creators[\"roster guild role\"] = [](PlayerbotAI* ai) { return new RosterGuildRoleAction(ai); };" "role action registered")
require_order("${guild_strategy}" "\"roster guild role\"," "new NextAction(\"roster guild role\", 4.0f)" "role trigger node in the guild strategy")
require_text("${manage_h}" "class RosterGuildRoleAction : public Action" "role action declared")
require_text("${create_h}" "static uint32 AssignedGuild(uint32 guidLow, Team team);" "deal lookup declared")

# 8. Plan file (PlanFile): read once in PlayerbotAIConfig::Initialize, never by the guild code.
require_text("${config_source}" "std::ifstream planFile(rosterGuildPlanFile);" "plan file read in Initialize")
require_text("${config_source}" "rosterGuildPlanLines.clear();" "plan lines reset on reload")
foreach(source_name create accept manage sign triggers)
  foreach(io "std::ifstream" "fopen(" "rosterGuildPlanFile")
    reject_text("${${source_name}}" "${io}" "plan file IO outside Initialize (${source_name})")
  endforeach()
endforeach()
# One plan guild = one guild: purchase and founding gates.
foreach(needle
    "if (!PlanLabelOpen(state, bot->GetGUIDLow(), true))\n    {\n        reason = \"plan_guild_taken\";"
    "    // twow-repo#485 / #518 (PlanFile): no second guild of one plan guild.\n    if (!PlanLabelOpen(state, bot->GetGUIDLow(), false))")
  require_text("${create}" "${needle}" "plan guild gate")
endforeach()

# 9. Config: neutral defaults next to the other RosterGuild keys, members read, documented.
foreach(pair
    "\"AiPlayerbot.RosterGuild.Tanks\", 0)"
    "\"AiPlayerbot.RosterGuild.Healers\", 0)"
    "\"AiPlayerbot.RosterGuild.Dps\", 0)"
    "\"AiPlayerbot.RosterGuild.HealerClassMin\", false)"
    "\"AiPlayerbot.RosterGuild.TankClassSpread\", false)"
    "\"AiPlayerbot.RosterGuild.RareComboSpread\", false)"
    "\"AiPlayerbot.RosterGuild.PlanFile\", \"\")")
  require_text("${config_source}" "${pair}" "neutral config default")
endforeach()
require_order("${config_source}" "\"AiPlayerbot.RosterGuild.NoteRefreshSeconds\"" "\"AiPlayerbot.RosterGuild.Tanks\"" "keys next to the other RosterGuild keys")
foreach(member "uint32 rosterGuildTanks;" "uint32 rosterGuildHealers;" "uint32 rosterGuildDps;" "bool rosterGuildHealerClassMin;"
    "bool rosterGuildTankClassSpread;" "bool rosterGuildRareComboSpread;" "std::string rosterGuildPlanFile;" "std::vector<std::string> rosterGuildPlanLines;")
  require_text("${config_header}" "${member}" "member declared without in-class default")
endforeach()
foreach(member "sPlayerbotAIConfig.rosterGuildTanks" "sPlayerbotAIConfig.rosterGuildHealers" "sPlayerbotAIConfig.rosterGuildDps"
    "sPlayerbotAIConfig.rosterGuildHealerClassMin" "sPlayerbotAIConfig.rosterGuildTankClassSpread" "sPlayerbotAIConfig.rosterGuildRareComboSpread")
  require_text("${create}" "${member}" "config member read by the plan")
endforeach()
foreach(line
    "AiPlayerbot.RosterGuild.Tanks = 0"
    "AiPlayerbot.RosterGuild.Healers = 0"
    "AiPlayerbot.RosterGuild.Dps = 0"
    "AiPlayerbot.RosterGuild.HealerClassMin = 0"
    "AiPlayerbot.RosterGuild.TankClassSpread = 0"
    "AiPlayerbot.RosterGuild.RareComboSpread = 0"
    "AiPlayerbot.RosterGuild.PlanFile = \"\""
    "Proposal (owner decides): 5/10/30 or 7/10/28.")
  require_text("${config_template}" "${line}" "documented default")
endforeach()
reject_text("${config_template}" "\nAiPlayerbot.RosterGuild.Tanks = 5" "a proposal as the default")
reject_text("${config_template}" "\nAiPlayerbot.RosterGuild.Tanks = 7" "a proposal as the default")

message(STATUS "ROSTER_GUILD_ROLE_SOURCE_CONTRACT=PASS")
