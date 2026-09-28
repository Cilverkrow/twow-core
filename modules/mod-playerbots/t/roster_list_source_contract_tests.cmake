if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#419 variant B: `.bot roster` is wired, rate-limited before any work,
# built from the roster snapshot (not the session list), logged as [BotCtl],
# and never prints account data. The policy itself is roster_list_policy_tests.
file(READ "${PB_SOURCE_DIR}/PlayerbotMgr.cpp" mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}")
  endif()
endfunction()

function(function_region text signature next_signature output)
  string(FIND "${text}" "${signature}" begin)
  if(begin EQUAL -1)
    message(FATAL_ERROR "Missing function ${signature}")
  endif()
  string(SUBSTRING "${text}" ${begin} -1 tail)
  string(FIND "${tail}" "${next_signature}" end)
  if(end EQUAL -1)
    set(${output} "${tail}" PARENT_SCOPE)
  else()
    string(SUBSTRING "${tail}" 0 ${end} region)
    set(${output} "${region}" PARENT_SCOPE)
  endif()
endfunction()

require_text("${mgr}" "m_holderHandlers[\"roster\"] = &PlayerbotHolder::HandleRoster;" "`.bot roster` registration")

# The list comes from the roster snapshot and the character cache.
function_region("${mgr}" "void BuildRosterListEntries" "std::list<std::string> PlayerbotHolder::HandleRoster" build)
require_text("${build}" "PersistentRosterGuids()" "roster snapshot as the source")
require_text("${build}" "GetBotAI(bot)" "real players excluded")
forbid_text("${build}" "GetPlayers()" "walk over every session")
forbid_text("${build}" "CharacterDatabase" "database query per rebuild")

function_region("${mgr}" "std::list<std::string> PlayerbotHolder::HandleRoster" "std::list<std::string> PlayerbotHolder::HandleHelp" handler)
require_text("${handler}" "rosterListEnabled" "feature switch")
require_text("${handler}" "RateLimited(" "per-session rate limit")
require_text("${handler}" "FactionFilterAllowed(" "cross-faction gate")
require_text("${handler}" "IsGmBypass(" "GM decided by the roster-control threshold")
require_text("${handler}" "rosterListCacheMs" "shared cache")
require_text("${handler}" "[BotCtl] cmd=roster" "usage log")
require_text("${handler}" "build_us=" "measuring point")
foreach(forbidden "GetAccountName" "GetRemoteAddress" "username" "last_ip" "LoginDatabase" "GetPlayers()")
  forbid_text("${handler}" "${forbidden}" "${forbidden} in the roster list")
endforeach()

# The rate limit runs before the filter is parsed and before the cache is touched.
string(FIND "${handler}" "RateLimited(" rate_at)
string(FIND "${handler}" "ParseFilter(" parse_at)
string(FIND "${handler}" "BuildRosterListEntries(" build_at)
if(NOT rate_at LESS parse_at OR NOT rate_at LESS build_at)
  message(FATAL_ERROR "the rate limit must run before any other work")
endif()

# Neutral default: off unless the profile turns it on.
require_text("${config}" "\"AiPlayerbot.RosterList.Enabled\", false" "neutral default")
foreach(key "Enabled = 0" "MinIntervalMs = 2000" "PageSize = 50" "CacheMs = 1000")
  require_text("${dist}" "AiPlayerbot.RosterList.${key}" "dist entry AiPlayerbot.RosterList.${key}")
endforeach()
