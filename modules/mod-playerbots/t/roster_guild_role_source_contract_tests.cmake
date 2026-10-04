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

# Review 04.10, defects 1 and 2: the deal is guild_plan.py's for every role (class count first, also
# with the switches off), and the spread rules only fall back to the role slot once every target
# guild exists. Returns the first problem, "" when the policy deals as the planner.
function(check_deal_rules text problem)
  set(${problem} "" PARENT_SCOPE)
  foreach(needle
      "return std::make_tuple(c.Get(roleClass), switches.rareComboSpread ? c.GetCombo(combo) : 0u,"
      "bool const allGuilds = guilds.size() >= targetGuilds;"
      "std::vector<std::size_t> const& candidates = (fitting.empty() && allGuilds) ? open : fitting;")
    string(FIND "${text}" "${needle}" at)
    if(at EQUAL -1)
      set(${problem} "missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(forbidden "UsesClassKey" "classKey ?" "fitting.empty() ? open : fitting")
    string(FIND "${text}" "${forbidden}" at)
    if(NOT at EQUAL -1)
      set(${problem} "forbidden: ${forbidden}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# Review 04.10 (config reload): the plan lines are published as a whole and read through a snapshot;
# nobody clears or reads the member in place. Returns the first problem, "" when clean.
function(check_plan_publish config_source config_header create problem)
  set(${problem} "" PARENT_SCOPE)
  string(FIND "${config_source}" "std::atomic_store(&rosterGuildPlanLines, std::shared_ptr<const std::vector<std::string>>(planLines));" store_at)
  string(FIND "${config_header}" "return std::atomic_load(&rosterGuildPlanLines);" load_at)
  string(FIND "${config_source}" "rosterGuildPlanLines.clear();" clear_at)
  string(FIND "${config_source}" "rosterGuildPlanLines.push_back(" push_at)
  string(FIND "${create}" "sPlayerbotAIConfig.rosterGuildPlanLines" member_at)
  if(store_at EQUAL -1 OR load_at EQUAL -1)
    set(${problem} "plan lines not swapped atomically" PARENT_SCOPE)
  elseif(NOT clear_at EQUAL -1 OR NOT push_at EQUAL -1)
    set(${problem} "plan lines changed in place on reload" PARENT_SCOPE)
  elseif(NOT member_at EQUAL -1)
    set(${problem} "guild code reads the plan member without a snapshot" PARENT_SCOPE)
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
check_deal_rules("${policy}" deal_problem)
if(NOT deal_problem STREQUAL "")
  message(FATAL_ERROR "Role deal differs from guild_plan.py: ${deal_problem}")
endif()
# Negative probes: the old switch-gated class key and the early fallback must be caught.
string(REPLACE "std::make_tuple(c.Get(roleClass)," "std::make_tuple(classKey ? c.Get(roleClass) : 0u," class_key_mutant "${policy}")
check_deal_rules("${class_key_mutant}" class_key_probe)
if(class_key_probe STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: class key only with a switch - the check is broken")
endif()
string(REPLACE "(fitting.empty() && allGuilds) ? open : fitting" "fitting.empty() ? open : fitting" fallback_mutant "${policy}")
check_deal_rules("${fallback_mutant}" fallback_probe)
if(fallback_probe STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: fallback before every target guild exists - the check is broken")
endif()
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
    "std::shared_ptr<const std::vector<std::string>> const planLines = sPlayerbotAIConfig.RosterGuildPlanLines();"
    "state.plan = roster_guild_role::ParsePlan(*planLines, rejected);"
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
check_plan_publish("${config_source}" "${config_header}" "${create}" plan_problem)
if(NOT plan_problem STREQUAL "")
  message(FATAL_ERROR "Plan file reload: ${plan_problem}")
endif()
# Negative probe: the old in-place reset must be caught.
string(REPLACE "std::shared_ptr<std::vector<std::string>> planLines;" "rosterGuildPlanLines.clear();" reload_mutant "${config_source}")
check_plan_publish("${reload_mutant}" "${config_header}" "${create}" reload_probe)
if(reload_probe STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: plan lines cleared under a reader - the check is broken")
endif()
require_text("${create}" "if (planLines != state.planSource)" "plan parsed again only after a reload")
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

# 9. Config: the owner rules of 04.10. (assignment v3) are the defaults - 7/10/28, a healer of every
#    healer class, tank mix 2-3 warriors + 1 bear/rogue/paladin/shaman, rare pairs about 2.5 % - and
#    stay inactive while BotsPerGuild = 0 (core#281 master switch). Keys next to the other RosterGuild
#    keys, members read, documented.
foreach(pair
    "\"AiPlayerbot.RosterGuild.Tanks\", 7)"
    "\"AiPlayerbot.RosterGuild.Healers\", 10)"
    "\"AiPlayerbot.RosterGuild.Dps\", 28)"
    "\"AiPlayerbot.RosterGuild.HealerClassMin\", true)"
    "\"AiPlayerbot.RosterGuild.TankClassSpread\", false)"
    "\"AiPlayerbot.RosterGuild.RareComboSpread\", false)"
    "\"AiPlayerbot.RosterGuild.TankClassMix\", \"1:2-3,11:1,4:1,2:1,7:1\")"
    "\"AiPlayerbot.RosterGuild.RarePairs\", \"3:7,3:9,5:2\")"
    "\"AiPlayerbot.RosterGuild.RarePairMaxShare\", 0.025f)"
    "\"AiPlayerbot.RosterGuild.PlanFile\", \"\")")
  require_text("${config_source}" "${pair}" "owner default (assignment v3)")
endforeach()
# Negative probes: the old neutral quota and the old switch default are no longer the defaults.
reject_text("${config_source}" "\"AiPlayerbot.RosterGuild.Tanks\", 0)" "the old 0 quota as the code default")
reject_text("${config_source}" "\"AiPlayerbot.RosterGuild.HealerClassMin\", false)" "the healer minimum off by default")
require_text("${config_source}" "\"AiPlayerbot.RosterGuild.BotsPerGuild\", 0)" "the master switch stays off by default")
require_order("${config_source}" "\"AiPlayerbot.RosterGuild.NoteRefreshSeconds\"" "\"AiPlayerbot.RosterGuild.Tanks\"" "keys next to the other RosterGuild keys")
foreach(member "uint32 rosterGuildTanks;" "uint32 rosterGuildHealers;" "uint32 rosterGuildDps;" "bool rosterGuildHealerClassMin;"
    "bool rosterGuildTankClassSpread;" "bool rosterGuildRareComboSpread;" "std::string rosterGuildTankClassMix;"
    "std::string rosterGuildRarePairs;" "float rosterGuildRarePairMaxShare;" "std::string rosterGuildPlanFile;"
    "std::shared_ptr<const std::vector<std::string>> rosterGuildPlanLines;")
  require_text("${config_header}" "${member}" "member declared without in-class default")
endforeach()
foreach(member "sPlayerbotAIConfig.rosterGuildTanks" "sPlayerbotAIConfig.rosterGuildHealers" "sPlayerbotAIConfig.rosterGuildDps"
    "sPlayerbotAIConfig.rosterGuildHealerClassMin" "sPlayerbotAIConfig.rosterGuildTankClassSpread" "sPlayerbotAIConfig.rosterGuildRareComboSpread"
    "sPlayerbotAIConfig.rosterGuildTankClassMix" "sPlayerbotAIConfig.rosterGuildRarePairs" "sPlayerbotAIConfig.rosterGuildRarePairMaxShare")
  require_text("${create}" "${member}" "config member read by the plan")
endforeach()
foreach(line
    "\nAiPlayerbot.RosterGuild.Tanks = 7\n"
    "\nAiPlayerbot.RosterGuild.Healers = 10\n"
    "\nAiPlayerbot.RosterGuild.Dps = 28\n"
    "\nAiPlayerbot.RosterGuild.HealerClassMin = 1\n"
    "\nAiPlayerbot.RosterGuild.TankClassMix = \"1:2-3,11:1,4:1,2:1,7:1\"\n"
    "\nAiPlayerbot.RosterGuild.RarePairs = \"3:7,3:9,5:2\"\n"
    "\nAiPlayerbot.RosterGuild.RarePairMaxShare = 0.025\n"
    "\nAiPlayerbot.RosterGuild.TankClassSpread = 0\n"
    "\nAiPlayerbot.RosterGuild.RareComboSpread = 0\n"
    "AiPlayerbot.RosterGuild.PlanFile = \"\"")
  require_text("${config_template}" "${line}" "documented default")
endforeach()
reject_text("${config_template}" "\nAiPlayerbot.RosterGuild.Tanks = 0\n" "the old 0 quota as the documented default")
reject_text("${config_template}" "\nAiPlayerbot.RosterGuild.Tanks = 5\n" "the 5/10/30 candidate as the default")

# 10. Owner rules in the policy: tank class mix (maximum, slots kept for classes below the minimum as far
#     as the faction has unplaced tanks of the class) and the rare pair cap; tanks are counted as unplaced
#     like healers; the plan builds both from the config strings.
foreach(needle
    "if (own != switches.tankMix.end() && own->second.second && counts.Get(roleClass) >= own->second.second)"
    "if (inRole + 1 + MissingTankMix(counts, stats, switches.tankMix, member.cls) > quota.tanks)"
    "missing += std::min(item.second.first - have, unplaced->second);"
    "if (switches.rarePairCap && switches.rarePairs.count(combo) && counts.GetCombo(combo) >= switches.rarePairCap)"
    "++stats.unplacedTanks[member.cls];"
    "--stats.unplacedTanks[member.cls];"
    "case Fit::TankClassMix: return \"tank_class_mix\";"
    "case Fit::RarePairCap: return \"rare_pair_cap\";")
  require_text("${policy}" "${needle}" "owner rule in the policy")
endforeach()
require_order("${policy}" "inline bool ParseUnsigned(" "inline TankMix ParseTankMix(" "the mix parser after ParseUnsigned")
foreach(needle
    "switches.tankMix = roster_guild_role::ParseTankMix(sPlayerbotAIConfig.rosterGuildTankClassMix);"
    "switches.rarePairs = roster_guild_role::ParseRarePairs(sPlayerbotAIConfig.rosterGuildRarePairs);"
    "roster_guild_role::RarePairCapFor(sPlayerbotAIConfig.rosterGuildRarePairMaxShare, quota.tanks + quota.healers + quota.dps)")
  require_text("${create}" "${needle}" "owner rules built from the config")
endforeach()

message(STATUS "ROSTER_GUILD_ROLE_SOURCE_CONTRACT=PASS")
