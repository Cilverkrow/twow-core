# Owner 05.10.2026 (OB-00): bot guilds only from AiPlayerbot.RosterGuild.MinLevel (0 = off): a roster bot below it founds no
# charter. Owner 04.11.2026: joining, signing a bot charter, being offered a charter and being invited by bot officers use
# AiPlayerbot.RosterGuild.JoinMinLevel (-1 = MinLevel, the default); a real player's invitation or charter stays allowed.
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildAcceptAction.cpp" accept)
file(READ "${PB_SOURCE_DIR}/strategy/actions/PetitionSignAction.cpp" sign)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.cpp" create)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildManagementActions.cpp" manage)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
foreach (pair
    "accept|!roster_guild::JoinAllowed(bot->GetLevel(), sPlayerbotAIConfig.rosterGuildJoinMinLevel,"
    "accept|IsRealPlayer(inviter)))"
    "accept|path=accept reason=min_level"
    "sign|!roster_guild::JoinAllowed(bot->GetLevel(), sPlayerbotAIConfig.rosterGuildJoinMinLevel,"
    "sign|IsRealPlayer(_inviter)))"
    "sign|path=sign reason=min_level"
    "create|!roster_guild::FoundAllowed(bot->GetLevel(), sPlayerbotAIConfig.rosterGuildMinLevel,"
    "create|!IsRealPlayer(player) && !roster_guild::JoinAllowed(player->GetLevel(), sPlayerbotAIConfig.rosterGuildJoinMinLevel,"
    "manage|!roster_guild::JoinAllowed(player->GetLevel(), sPlayerbotAIConfig.rosterGuildJoinMinLevel, true, false)"
    "config_cpp|AiPlayerbot.RosterGuild.MinLevel\", 0"
    "config_dist|AiPlayerbot.RosterGuild.MinLevel = 0"
    "config_cpp|config.GetIntDefault(\"AiPlayerbot.RosterGuild.JoinMinLevel\", -1);"
    "config_cpp|rosterGuildJoinMinLevel = joinMinLevel < 0 ? rosterGuildMinLevel : uint32(joinMinLevel);"
    "config_dist|AiPlayerbot.RosterGuild.JoinMinLevel = -1")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "guild min level: missing ${needle}")
  endif()
endforeach()
# The accept gate comes before the security check that would answer with a generic refusal.
string(FIND "${accept}" "path=accept reason=min_level" gate)
string(FIND "${accept}" "PLAYERBOT_SECURITY_GUILD, false, inviter, true)" security)
if (gate GREATER security)
  message(FATAL_ERROR "guild min level: the accept gate must come before the security check")
endif()
message(STATUS "GUILD_MIN_LEVEL_CONTRACT=PASS")
# Owner 04.11.2026: founding stays on MinLevel; no join path reads MinLevel any more.
foreach (var accept sign manage)
  string(FIND "${${var}}" "sPlayerbotAIConfig.rosterGuildMinLevel" old)
  if (NOT old EQUAL -1)
    message(FATAL_ERROR "guild min level: ${var} still reads rosterGuildMinLevel (join paths use JoinMinLevel)")
  endif()
endforeach()

# Owner 04.11.2026 (OB-00: the guild rule applies to all bots): the level gates take every bot on its own (no real master), not
# only roster members; a non-roster bot on its own (legacy path) also stops buying and handing in charters at the guild cap.
foreach (pair
    "accept|!ai->HasRealPlayerMaster(), IsRealPlayer(inviter)))"
    "sign|!ai->HasRealPlayerMaster(), IsRealPlayer(_inviter)))"
    "create|GetBotAI(bot) && !GetBotAI(bot)->HasRealPlayerMaster()))"
    "create|!(GetBotAI(player) && GetBotAI(player)->HasRealPlayerMaster()), false))"
    "create|bool RosterGuildPlan::MayBuyLegacyCharter(Player* bot, char const*& reason)"
    "create|!roster_guild::MayBuyCharter(fs.target, fs.ledger.guilds + fs.ledger.reserved + fs.otherBotGuilds, fs.openCharters))"
    "create|if (sPlayerbotAIConfig.randomBotGuildCount && allBotGuilds >= sPlayerbotAIConfig.randomBotGuildCount)"
    "create|else if (sRandomPlayerbotMgr.IsRandomBot(guild.leaderGuid.GetCounter()))"
    "create|else if (!ai->HasRealPlayerMaster() && !RosterGuildPlan::MayBuyLegacyCharter(bot, reason))"
    "create|return due && (ai->HasRealPlayerMaster() || RosterGuildPlan::MayBuyLegacyCharter(bot, capReason));")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "guild min level (all bots): missing ${needle}")
  endif()
endforeach()
foreach (var accept sign)
  string(FIND "${${var}}" "IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster(), IsRealPlayer" roster_only)
  if (NOT roster_only EQUAL -1)
    message(FATAL_ERROR "guild min level: ${var} still gates roster members only")
  endif()
endforeach()
message(STATUS "GUILD_JOIN_MIN_LEVEL_CONTRACT=PASS")
