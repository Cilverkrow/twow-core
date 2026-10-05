if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#485, owner decision 5 (poaching): a roster bot of a bot guild takes a real player's guild
# invitation or charter and switches. One rule (GuildPoachPolicy.h) for the core hook and both bot
# actions; it reads copies only, because the core asks on the inviter's map thread. The bots never
# leave or join themselves: GuildMgr::RequestGuildSwitch does it on the world thread (core contract
# guild_switch_contract). AllowPoaching = 0 (default) keeps "already in a guild".

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

# Text from start_marker up to end_marker (both must exist, in that order).
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

file(READ "${PB_SOURCE_DIR}/GuildPoachPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildAcceptAction.cpp" accept)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildAcceptAction.h" accept_h)
file(READ "${PB_SOURCE_DIR}/strategy/actions/PetitionSignAction.cpp" sign)
file(READ "${PB_SOURCE_DIR}/PlayerbotScripts.cpp" scripts)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.h" ai_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# 1. Pure policy, std only, included only by the .cpp files that use it.
require_text("${policy}" "namespace ai::guild_poach" "policy namespace")
reject_text("${policy}" "#include \"" "game or playerbot include in the pure policy")
foreach(header_name ai_header config_header accept_h)
  reject_text("${${header_name}}" "GuildPoachPolicy.h\"" "policy include in a header (${header_name})")
endforeach()

# 2. The rules of draft v2 section 5.1, in this order. found: the first missing rule, "" when all hold.
function(find_rule_gap text gap)
  set(${gap} "" PARENT_SCOPE)
  set(previous -1)
  foreach(rule
      "if (!in.enabled)"
      "return PoachDecision::Disabled;"
      "return PoachDecision::NotRosterBot;"
      "return PoachDecision::NotInGuild;"
      "if (!in.inviterRealPlayer)"
      "return PoachDecision::InviterNotReal;"
      "if (!in.sameFaction)"
      "return PoachDecision::Faction;"
      "if (!in.currentGuildIsBotGuild)"
      "return PoachDecision::PlayerGuild;"
      "if (in.leadsCurrentGuild)"
      "return PoachDecision::GuildMaster;"
      "return PoachDecision::SameGuild;"
      "if (in.targetIsBotGuild)"
      "return PoachDecision::TargetBotGuild;"
      "return PoachDecision::NotOwnCharter;"
      "if (in.lastSwitch && in.now < in.lastSwitch + std::time_t(in.cooldownSeconds))"
      "return PoachDecision::Cooldown;"
      "return PoachDecision::Allow;")
    string(FIND "${text}" "${rule}" at)
    if(at EQUAL -1 OR NOT at GREATER previous)
      set(${gap} "${rule}" PARENT_SCOPE)
      return()
    endif()
    set(previous ${at})
  endforeach()
endfunction()
text_between("${policy}" "inline PoachDecision DecidePoach(" "inline char const* PoachDecisionName(" decide)
find_rule_gap("${decide}" decide_gap)
if(NOT decide_gap STREQUAL "")
  message(FATAL_ERROR "Poaching rule missing or out of order: ${decide_gap}")
endif()
# Negative probe: without the player guild rule a bot would leave a player guild - must be caught.
string(REPLACE "return PoachDecision::PlayerGuild;" "return PoachDecision::Allow;" probe_decide "${decide}")
find_rule_gap("${probe_decide}" probe_gap)
if(probe_gap STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: player guild rule removed - the rule scan is broken")
endif()
require_text("${policy}" "std::uint32_t const minimum = 3600;" "cooldown at least 1 h")

# 3. The glue reads copies only (GuildMgr summaries and maps, the player cache): the core calls it
#    on the inviter's map thread, where the invitee belongs to another thread.
text_between("${accept}" "char const* RosterGuildPoach::Refusal(" "bool GuildAcceptAction::Execute(" refusal)
foreach(needle
    "sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.rosterGuildAllowPoaching"
    "sRandomPlayerbotMgr.IsPersistentRosterMember(invitee.GetCounter())"
    "sObjectMgr.GetPlayerDataByGUID(invitee.GetCounter())"
    "sGuildMgr.GetPlayerGuildId(invitee.GetCounter())"
    "sGuildMgr.GetGuildSummary(in.currentGuildId, current)"
    "sRandomPlayerbotMgr.IsPersistentRosterMember(current.leaderGuid.GetCounter())"
    "sRandomPlayerbotMgr.IsPersistentRosterMember(target.leaderGuid.GetCounter())"
    "sGuildMgr.GetLastGuildSwitch(invitee.GetCounter())"
    "guild_poach::PoachCooldown(sPlayerbotAIConfig.rosterGuildPoachCooldownSeconds)"
    "guild_poach::DecidePoach(in)"
    "[RosterGuild] event=poach_refused bot=%u inviter=%u from=%u to=%u path=%s reason=%s")
  require_text("${refusal}" "${needle}" "poaching rule input")
endforeach()
# found: the first live object or SQL access in text, "" when none.
function(find_live_access text found)
  set(${found} "" PARENT_SCOPE)
  foreach(forbidden "GetGuildById(" "GetPlayerGuild(" "ObjectAccessor::" "sObjectMgr.GetPlayer(" "->GetGuildId()"
      "->GetTeam()" "->GetGuildIdInvited()" "Query(" "PExecute(" "CharacterDatabase")
    string(FIND "${text}" "${forbidden}" at)
    if(NOT at EQUAL -1)
      set(${found} "${forbidden}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()
find_live_access("${refusal}" live_access)
if(NOT live_access STREQUAL "")
  message(FATAL_ERROR "Forbidden live access (${live_access}) in RosterGuildPoach::Refusal - read copies only")
endif()
# Negative probe: the former way to read the invitee's guild must be caught.
find_live_access("    Player* invited = sObjectMgr.GetPlayer(invitee);\n    in.currentGuildId = invited->GetGuildId();" probe_access)
if(probe_access STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: live Player* of the invitee - the scan is broken")
endif()

# 4. Core hook: off with AllowPoaching = 0, the same rule otherwise.
text_between("${scripts}" "bool CanSwitchGuild(Player* inviter, ObjectGuid const& invitee, uint32 guildId) override" "// Was the CreatePlayerbotMgr() call" hook)
require_order("${hook}" "!sPlayerbotAIConfig.rosterGuildAllowPoaching" "RosterGuildPoach::Refusal(inviter, invitee, guildId, true" "switch checked first")
require_text("${hook}" "return false;" "hook refuses by default")

# 5. Invitation: same rule again in the bot; the switch only through GuildMgr; security still applies.
text_between("${accept}" "bool GuildAcceptAction::Execute(" "\n}" accept_execute)
require_text("${accept_execute}" "RosterGuildPoach::Refusal(inviter, bot->GetObjectGuid(), invitedGuildId, false, \"accept\")" "bot-side rule on invitation")
require_order("${accept_execute}" "else if (fromGuildId && !poach)" "CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD" "poach passes the security check")
require_order("${accept_execute}" "if (accept && poach)" "sGuildMgr.RequestGuildSwitch(bot->GetObjectGuid(), fromGuildId, invitedGuildId, 0);" "switch requested from GuildMgr")
text_between("${accept_execute}" "if (accept && poach)" "else if (accept)" accept_poach)
foreach(forbidden "HandleGuildAcceptOpcode(" "HandleGuildLeaveOpcode(" "DelMember(" "AddMember(")
  reject_text("${accept_poach}" "${forbidden}" "guild change on the bot's map thread")
endforeach()
require_text("${accept_execute}" "bot->SetGuildIdInvited(0);" "refused poaching invitation dropped")

# 6. Charter: the inviter's own charter, as a copy; same rule; security first; switch via GuildMgr.
require_text("${sign}" "RosterGuildPoach::Refusal(charterOwner, bot->GetObjectGuid(), 0, poachCharter.ownerGuid == inviter, \"sign\")" "bot-side rule on a charter")
require_text("${sign}" "sGuildMgr.GetPetitionSummaryByCharterGuid(petitionGuid, poachCharter)" "charter read as a copy")
require_text("${sign}" "if (accept && !isArena && !poach && RosterGuildPlan::UsesRosterPath(ai))" "roster sign rule not applied to a player's charter")
require_order("${sign}" "CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD" "sGuildMgr.RequestGuildSwitch(bot->GetObjectGuid(), fromGuildId, 0, poachPetitionId);" "security before the switch")
require_order("${sign}" "if (accept && poach)" "sGuildMgr.RequestGuildSwitch(bot->GetObjectGuid(), fromGuildId, 0, poachPetitionId);" "switch requested from GuildMgr")
text_between("${sign}" "if (accept && poach)" "if (accept)\n" sign_poach)
foreach(forbidden "HandlePetitionSignOpcode(" "HandleGuildLeaveOpcode(" "DelMember(" "AddPetitionSignature(")
  reject_text("${sign_poach}" "${forbidden}" "guild or petition change on the bot's map thread")
endforeach()

# 6b. No ping-pong on the charter path: after a switch by charter the bot is guildless and only
#     signed the player's charter. Within the cooldown it signs no other charter (on every path, also
#     outside the roster path), joins no other guild and buys no charter. The rule is pure
#     (KeepsPoachedCharter, policy test); the glue reads copies only (section 3 scans it too).
text_between("${policy}" "inline bool KeepsPoachedCharter(" "\n}\n" keeps_rule)
foreach(needle
    "if (!signsCharter || offeredIsSigned || !lastSwitch)"
    "return now < lastSwitch + std::time_t(cooldownSeconds);")
  require_text("${keeps_rule}" "${needle}" "keep-charter rule")
endforeach()
text_between("${accept}" "uint32 RosterGuildPoach::KeptCharter(" "bool GuildAcceptAction::Execute(" kept_glue)
foreach(needle
    "sGuildMgr.GetLastGuildSwitch(bot.GetCounter())"
    "sGuildMgr.GetPetitionSummaryBySigner(bot, signedCharter)"
    "signedCharter.charterGuid == offeredCharter"
    "guild_poach::PoachCooldown(sPlayerbotAIConfig.rosterGuildPoachCooldownSeconds)"
    "guild_poach::KeepsPoachedCharter(")
  require_text("${kept_glue}" "${needle}" "keep-charter input")
endforeach()
require_text("${accept_h}" "static uint32 KeptCharter(ObjectGuid const& bot, ObjectGuid const& offeredCharter);" "keep-charter declaration")
# found: "" when the sign action keeps a poached bot's charter before both sign paths, else the gap.
function(find_keep_charter_gap text gap)
  set(${gap} "" PARENT_SCOPE)
  set(guard "if (accept && !isArena && !poach)\n    {\n        uint32 const keptCharter = RosterGuildPoach::KeptCharter(bot->GetObjectGuid(), petitionGuid);")
  string(FIND "${text}" "${guard}" guard_at)
  if(guard_at EQUAL -1)
    set(${gap} "keep-charter guard" PARENT_SCOPE)
    return()
  endif()
  foreach(later
      "reason=poached_keeps_charter"
      "if (accept && !isArena && !poach && RosterGuildPlan::UsesRosterPath(ai))"
      "bot->GetSession()->HandlePetitionSignOpcode(data);")
    string(FIND "${text}" "${later}" later_at)
    if(later_at EQUAL -1 OR NOT guard_at LESS later_at)
      set(${gap} "${later}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()
find_keep_charter_gap("${sign}" keep_gap)
if(NOT keep_gap STREQUAL "")
  message(FATAL_ERROR "Poached bot's charter not kept before the sign paths: ${keep_gap}")
endif()
# Negative probe: the review's ping-pong (guard removed, DecideSign moves the signature) must be caught.
string(REPLACE "uint32 const keptCharter = RosterGuildPoach::KeptCharter(bot->GetObjectGuid(), petitionGuid);" "uint32 const keptCharter = 0;" probe_sign "${sign}")
find_keep_charter_gap("${probe_sign}" probe_keep_gap)
if(probe_keep_gap STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: keep-charter guard removed - the scan is broken")
endif()
require_text("${accept_execute}" "else if (!fromGuildId && RosterGuildPoach::KeptCharter(bot->GetObjectGuid(), ObjectGuid()))" "poached bot joins no other guild within the cooldown")
require_order("${accept_execute}" "else if (!fromGuildId && RosterGuildPoach::KeptCharter(" "else if (accept)" "keep-charter refusal before the accept")
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.cpp" create_actions)
text_between("${create_actions}" "bool BuyPetitionAction::canBuyPetition(Player* bot)" "\n}" can_buy)
require_text("${can_buy}" "if (RosterGuildPoach::KeptCharter(bot->GetObjectGuid(), ObjectGuid()))" "poached bot buys no charter within the cooldown")

# 7. Config: neutral defaults, read into members without in-class defaults, documented.
foreach(pair
    "\"AiPlayerbot.RosterGuild.AllowPoaching\", false)"
    "\"AiPlayerbot.RosterGuild.PoachCooldownSeconds\", 86400)")
  require_text("${config_source}" "${pair}" "poaching config default")
endforeach()
foreach(member "bool rosterGuildAllowPoaching;" "uint32 rosterGuildPoachCooldownSeconds;")
  require_text("${config_header}" "${member}" "poaching member")
endforeach()
foreach(line "AiPlayerbot.RosterGuild.AllowPoaching = 0" "AiPlayerbot.RosterGuild.PoachCooldownSeconds = 86400" "(3600-2592000, other values are clamped")
  require_text("${config_template}" "${line}" "documented poaching default")
endforeach()

message(STATUS "GUILD_POACH_SOURCE_CONTRACT=PASS")
