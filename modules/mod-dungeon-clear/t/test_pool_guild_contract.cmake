# CLI-GRP G1 pilot 25.10.2026 (Refs twow-repo#629): a pool run's provisioning drafted the claimed characters into random
# guilds (Randomize -> InitGuild), and the claim skips every guilded character (compat IsRealGuild is always true), so the
# pool ran dry after one plan. The pool path now undoes the guild like the roster path. Pinned as source text (the job is
# core-coupled, not in the fast suite).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

file(READ "${DC_SOURCE_DIR}/src/TestRun/DcTestRunJob.cpp" job)
string(REPLACE "\r" "" job "${job}")

function(require_order first second description)
  string(FIND "${job}" "${first}" a)
  string(FIND "${job}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "pool guild: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

# Pool provisioning: after the factory (whose InitGuild drafts the guild) and before the comp entry is recorded.
require_order("    factory.AddFood();   // public wrapper for InitFood" "    UndoUnwantedGuild(bot, slot);

    // Not inline: provisioning ticks in the world thread and this bot is" "pool path undoes the guild after the factory")
require_order("    UndoUnwantedGuild(bot, slot);

    // Not inline: provisioning ticks in the world thread and this bot is" "    entry.spec = specNo >= 0 ? pickedSpec : \"(random)\";" "before the comp entry")
# The claim still refuses a guilded character (a pool character never comes in with a guild).
require_order("            uint32 const guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);" "            if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))" "claim guild filter unchanged")
