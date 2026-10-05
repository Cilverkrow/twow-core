if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#485 (owner decision 5, poaching): a real player recruits a roster bot from a bot guild
# by a guild invitation or his charter. The core asks a module before "already in a guild"
# (PlayerScript::CanSwitchGuild, default false = the stock answer); the switch itself - leave the
# old guild, join the new one or sign the charter - is only queued on the bot's map thread and
# applied by GuildMgr::Update on the world thread, after the checks of CMSG_GUILD_LEAVE,
# CMSG_GUILD_ACCEPT and CMSG_PETITION_SIGN. The accept and sign handlers stay closed to guild members.
file(READ "${TW_CORE_ROOT}/src/game/ScriptObjects.h" hooks)
file(READ "${TW_CORE_ROOT}/src/game/ScriptMgr.h" script_header)
file(READ "${TW_CORE_ROOT}/src/game/ScriptMgr.cpp" script_source)
file(READ "${TW_CORE_ROOT}/src/game/Handlers/GuildHandler.cpp" guild_handler)
file(READ "${TW_CORE_ROOT}/src/game/Handlers/PetitionsHandler.cpp" petition_handler)
file(READ "${TW_CORE_ROOT}/src/game/Guild/GuildMgr.h" header)
file(READ "${TW_CORE_ROOT}/src/game/Guild/GuildMgr.cpp" source)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Guild switch: missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Guild switch: forbidden ${description}: ${needle}")
  endif()
endfunction()

# needle_a must appear in text, and before needle_b.
function(require_before text needle_a needle_b description)
  string(FIND "${text}" "${needle_a}" a)
  string(FIND "${text}" "${needle_b}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "Guild switch: ${description} (${needle_a} before ${needle_b})")
  endif()
endfunction()

# Text from signature to the next line that starts with "}" (end of a function at column 0).
function(definition_body text signature out)
  string(FIND "${text}" "${signature}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "Guild switch: missing ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}" stop)
  if (stop EQUAL -1)
    message(FATAL_ERROR "Guild switch: no end of ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${stop} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# 1. The hook: default false, so without a module (or with AllowPoaching = 0) nothing changes.
require_before("${hooks}" "PLAYERHOOK_CAN_SWITCH_GUILD," "PLAYERHOOK_END" "hook registered before the end marker")
require_text("${hooks}" "virtual bool CanSwitchGuild(Player* /*inviter*/, ObjectGuid const& /*invitee*/, uint32 /*guildId*/) { return false; }" "hook defaulting to the core's answer")
require_text("${script_header}" "bool Script_CanSwitchGuild(Player* inviter, ObjectGuid const& invitee, uint32 guildId);" "hook wrapper")
definition_body("${script_source}" "bool Script_CanSwitchGuild(" wrapper)
require_text("${wrapper}" "ForEachEnabledHookWithReturn(PLAYERHOOK_CAN_SWITCH_GUILD" "wrapper asking the enabled hooks")

# 2. Invitation and charter offer ask before "already in a guild"; the charter offer passes the
#    invitee only as a guid (it runs on the inviter's map thread).
definition_body("${guild_handler}" "void WorldSession::HandleGuildInviteOpcode(" invite)
require_before("${invite}" "if (player->GetGuildId() && !Script_CanSwitchGuild(GetPlayer(), player->GetObjectGuid(), GetPlayer()->GetGuildId()))" "ERR_ALREADY_IN_GUILD_S" "invitation asks the hook before already-in-guild")
require_before("${invite}" "Script_CanSwitchGuild(" "if (player->GetGuildIdInvited())" "an open invitation still blocks")
require_before("${invite}" "Script_CanSwitchGuild(" "GR_RIGHT_INVITE" "the inviter's rank right still applies")
definition_body("${petition_handler}" "void WorldSession::HandleOfferPetitionOpcode(" offer)
require_before("${offer}" "if (player->GetGuildId() && !Script_CanSwitchGuild(_player, player->GetObjectGuid(), 0))" "ERR_ALREADY_IN_GUILD_S" "charter offer asks the hook before already-in-guild")

# The accept and sign handlers stay closed to guild members: the switch goes through GuildMgr.
definition_body("${guild_handler}" "void WorldSession::HandleGuildAcceptOpcode(" accept)
require_text("${accept}" "if (!guild || player->GetGuildId())" "accept handler closed to guild members")
reject_text("${accept}" "Script_CanSwitchGuild(" "hook in the accept handler")
definition_body("${petition_handler}" "void WorldSession::HandlePetitionSignOpcode(" sign)
require_before("${sign}" "if (_player->GetGuildId())" "ERR_ALREADY_IN_GUILD_S" "sign handler closed to guild members")
reject_text("${sign}" "Script_CanSwitchGuild(" "hook in the sign handler")

# 3. GuildMgr: the request only queues (map thread), the world thread applies.
foreach (required
    "void RequestGuildSwitch(ObjectGuid const& member, uint32 fromGuildId, uint32 toGuildId, uint32 petitionId);"
    "time_t GetLastGuildSwitch(uint32 memberLowGuid);"
    "bool GetGuildSummary(uint32 guildId, GuildSummary& out) const;"
    "uint32 GetPlayerGuildId(uint32 lowguid)"
    "void ApplyPendingGuildSwitches();"
    "std::mutex m_guildSwitchMutex;")
  require_text("${header}" "${required}" "GuildMgr.h declaration")
endforeach()

definition_body("${source}" "void GuildMgr::RequestGuildSwitch(" request)
require_before("${request}" "std::lock_guard<std::mutex> guard(m_guildSwitchMutex);" "m_pendingGuildSwitches[member.GetCounter()] = pending;" "request queued under the lock")
require_text("${request}" "(toGuildId == 0) == (petitionId == 0)" "exactly one target")
# found: the first guild or player change in text, "" when none.
function(find_guild_change text found)
  set(${found} "" PARENT_SCOPE)
  foreach (forbidden "DelMember(" "AddMember(" "AddPetitionSignature(" "SetGuildIdInvited(" "GetPlayer(" "CharacterDatabase")
    string(FIND "${text}" "${forbidden}" at)
    if (NOT at EQUAL -1)
      set(${found} "${forbidden}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()
find_guild_change("${request}" request_change)
if (NOT request_change STREQUAL "")
  message(FATAL_ERROR "Guild switch: RequestGuildSwitch must only queue (${request_change} on the caller's map thread)")
endif()
# Negative probe: a request that leaves the guild on the map thread must be caught.
find_guild_change("    if (Guild* from = GetGuildById(fromGuildId))\n        from->DelMember(member);" probe_change)
if (probe_change STREQUAL "")
  message(FATAL_ERROR "Guild switch: negative probe not caught (DelMember in the request) - the check is broken")
endif()

definition_body("${source}" "void GuildMgr::Update(" update)
require_before("${update}" "ApplyPendingPublicNotes();" "ApplyPendingGuildSwitches();" "switches applied on the world thread after the notes")
definition_body("${source}" "void GuildMgr::ApplyPendingGuildSwitches(" apply_all)
require_before("${apply_all}" "std::lock_guard<std::mutex> guard(m_guildSwitchMutex);" "pending.swap(m_pendingGuildSwitches);" "queue swapped out under the lock")
require_before("${apply_all}" "pending.swap(m_pendingGuildSwitches);" "ApplyGuildSwitch(entry.member" "switch applied after the swap")
require_before("${apply_all}" "ApplyGuildSwitch(entry.member" "m_lastGuildSwitch[entry.member.GetCounter()] = time(nullptr);" "cooldown stamped after a switch")
require_text("${apply_all}" "[RosterGuild] event=%s bot=%u from=%u to=%u via=%s%s%s" "trace line")
require_text("${apply_all}" "switched ? \"poached\" : \"poach_failed\"" "trace events")

# 4. The checks before leaving: leave rules, then the target's rules, then leave, then join or sign.
#    rules: the first missing or misordered rule in a switch body, "" when all hold.
function(find_switch_rule_gap body gap)
  set(${gap} "" PARENT_SCOPE)
  foreach (pair
      "player->GetGuildId() != fromGuildId@@from->DelMember(member)"
      "from->GetLeaderGuid() == member@@from->DelMember(member)"
      "GetPetitionSummaryById(petitionId, petition, player->GetSession()->GetAccountId(), member)@@from->DelMember(member)"
      "uint32(petition.signatureCount) >= GetPetitionSignsRequired()@@from->DelMember(member)"
      "petition.signedByAccount || petition.signedByPlayer@@from->DelMember(member)"
      "player->GetGuildIdInvited() != toGuildId@@from->DelMember(member)"
      "CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD@@from->DelMember(member)"
      "from->DelMember(member)@@to->AddMember(member, to->GetLowestRank())"
      "from->DelMember(member)@@AddPetitionSignature(petitionId, player)"
      "from->BroadcastEvent(GE_LEFT, member, name.c_str())@@to->BroadcastEvent(GE_JOINED, member, name.c_str())")
    string(REPLACE "@@" ";" parts "${pair}")
    list(GET parts 0 first)
    list(GET parts 1 second)
    string(FIND "${body}" "${first}" first_at)
    string(FIND "${body}" "${second}" second_at)
    if (first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
      set(${gap} "${first} before ${second}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()
definition_body("${source}" "bool GuildMgr::ApplyGuildSwitch(" apply_one)
find_switch_rule_gap("${apply_one}" apply_gap)
if (NOT apply_gap STREQUAL "")
  message(FATAL_ERROR "Guild switch: ApplyGuildSwitch misses a check or its order: ${apply_gap}")
endif()
# Negative probe: without the guild master check the scan must fail (a bot guild would lose its master).
string(REPLACE "from->GetLeaderGuid() == member" "false" probe_apply "${apply_one}")
find_switch_rule_gap("${probe_apply}" probe_gap)
if (probe_gap STREQUAL "")
  message(FATAL_ERROR "Guild switch: negative probe not caught (guild master check removed) - the check is broken")
endif()

definition_body("${source}" "bool GuildMgr::GetGuildSummary(" one_guild)
require_before("${one_guild}" "std::shared_lock<std::shared_mutex> guard(m_guildMutex);" "out.leaderGuid = itr->second->GetLeaderGuid();" "guild copied under the guild lock")

message(STATUS "GUILD_SWITCH_CONTRACT=PASS")
