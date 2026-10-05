# Owner 05.10.2026 (OB-00): bot guilds only from AiPlayerbot.RosterGuild.MinLevel (0 = off). A roster
# bot below it founds no charter, signs no bot charter, accepts no bot guild's invitation and is not
# offered a charter or invited by bot officers; a real player's invitation or charter stays allowed.
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildAcceptAction.cpp" accept)
file(READ "${PB_SOURCE_DIR}/strategy/actions/PetitionSignAction.cpp" sign)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.cpp" create)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildManagementActions.cpp" manage)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
foreach (pair
    "accept|!roster_guild::JoinAllowed(bot->GetLevel(), sPlayerbotAIConfig.rosterGuildMinLevel,"
    "accept|IsRealPlayer(inviter)))"
    "accept|path=accept reason=min_level"
    "sign|!roster_guild::JoinAllowed(bot->GetLevel(), sPlayerbotAIConfig.rosterGuildMinLevel,"
    "sign|IsRealPlayer(_inviter)))"
    "sign|path=sign reason=min_level"
    "create|!roster_guild::FoundAllowed(bot->GetLevel(), sPlayerbotAIConfig.rosterGuildMinLevel,"
    "create|!IsRealPlayer(player) && !roster_guild::JoinAllowed(player->GetLevel(), sPlayerbotAIConfig.rosterGuildMinLevel,"
    "manage|!roster_guild::JoinAllowed(player->GetLevel(), sPlayerbotAIConfig.rosterGuildMinLevel, true, false)"
    "config_cpp|AiPlayerbot.RosterGuild.MinLevel\", 0"
    "config_dist|AiPlayerbot.RosterGuild.MinLevel = 0")
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
