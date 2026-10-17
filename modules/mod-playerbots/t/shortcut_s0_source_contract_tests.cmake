# twow-repo#595 P4 S0 + B1 (OB-00 17.10.2026, shortcuts by player proximity): the real-player snapshot is built only
# in RandomPlayerbotMgr::UpdateAIInternal (world thread, after sMapMgr.Update) and published behind a short
# std::lock_guard<std::mutex>; ShortcutNear / CountShortcut never read the real-player map; every AiPlayerbot.Shortcut.*
# key defaults to 0; the [Shortcut] counters only measure (the four teleports are unchanged); B1: the group loop of the
# legacy random teleport keeps its 0-path, skips real members with the switch and never resets a null member AI.
# Reads only modules/mod-playerbots/src/playerbot (never src/modules/Eluna).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#595 shortcut S0: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#595 shortcut S0: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#595 shortcut S0: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle out_var)
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
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" n)
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#595 shortcut S0: ${description}: '${needle}' found ${n} times, expected ${expected}")
  endif()
endfunction()

# The text from `first` up to (not including) the next `second` after it.
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#595 shortcut S0: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#595 shortcut S0: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("ShortcutPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.h" mgr_h)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("strategy/actions/MovementActions.cpp" movement)
read_source("strategy/actions/ReviveFromCorpseAction.cpp" revive)
read_source("strategy/actions/ReleaseSpiritAction.h" repop)

# --- 1. Keys: every switch defaults to 0 / empty, in the code and in the dist file -------------------------------
require_text("${config_h}" "uint32 shortcutNearScope = 0;" "NearScope member default 0")
require_text("${config_h}" "float shortcutNearRadius = 0.0f;" "NearRadius member default 0")
require_text("${config_h}" "std::vector<uint32> shortcutTestWatcherGuids;" "watcher list member (empty)")
require_text("${config_h}" "bool shortcutTrace = false;" "Trace member default 0")
require_text("${config_h}" "bool shortcutGroupLoopFix = false;" "GroupLoopFix member default 0")
require_text("${config_cpp}" "shortcutNearScope = ai::shortcut::ClampScope(config.GetIntDefault(\"AiPlayerbot.Shortcut.NearScope\", 0));" "NearScope read default 0")
require_text("${config_cpp}" "shortcutNearRadius = std::max(0.0f, config.GetFloatDefault(\"AiPlayerbot.Shortcut.NearRadius\", 0.0f));" "NearRadius read default 0")
require_text("${config_cpp}" "shortcutTestWatcherGuids = ai::shortcut::ParseGuidList(config.GetStringDefault(\"AiPlayerbot.Shortcut.TestWatcherGuids\", \"\"));" "watchers read default empty")
require_text("${config_cpp}" "shortcutTrace = config.GetBoolDefault(\"AiPlayerbot.Shortcut.Trace\", false);" "Trace read default 0")
require_text("${config_cpp}" "shortcutGroupLoopFix = config.GetBoolDefault(\"AiPlayerbot.Shortcut.GroupLoopFix\", false);" "GroupLoopFix read default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Shortcut.NearScope = 0\n" "dist NearScope 0")
require_text("${conf_dist}" "\nAiPlayerbot.Shortcut.NearRadius = 0\n" "dist NearRadius 0")
require_text("${conf_dist}" "\nAiPlayerbot.Shortcut.TestWatcherGuids =\n" "dist watchers empty")
require_text("${conf_dist}" "\nAiPlayerbot.Shortcut.Trace = 0\n" "dist Trace 0")
require_text("${conf_dist}" "\nAiPlayerbot.Shortcut.GroupLoopFix = 0\n" "dist GroupLoopFix 0")
require_text("${conf_dist}" "TestWatcherGuids (TEST ONLY, never on a live realm)" "watchers documented as test only")
# each key exactly once (a second "= 1" line below the default would win at load)
foreach(key NearScope NearRadius TestWatcherGuids Trace GroupLoopFix)
  # without " =": a duplicate written as "Key=1" counts too (recheck P13)
  require_count("${conf_dist}" "\nAiPlayerbot.Shortcut.${key}" 1 "dist key AiPlayerbot.Shortcut.${key} once")
endforeach()

# --- 2. Policy: fail closed, bounded --------------------------------------------------------------------------------
require_text("${policy}" "constexpr std::uint64_t StaleMs = 5000;" "5 s staleness")
require_text("${policy}" "constexpr std::uint64_t BuildIntervalMs = 1000;" "at most one build a second")
require_text("${policy}" "constexpr std::size_t MaxWatchers = 64;" "watcher cap")
require_text("${policy}" "if (scope != ScopeRadius && scope != ScopeZoneOrRadius)\n        return Near::Unknown;" "scope off -> Unknown")
require_text("${policy}" "if (!snapshot || !Fresh(snapshot->builtAtMs, nowMs))\n        return Near::Unknown;" "missing / stale -> Unknown")
require_text("${policy}" "if (!onMap)\n        return Near::NotNear;" "no entry on the map -> NotNear")
require_text("${policy}" "return undecided ? Near::Unknown : Near::NotNear;" "zone 0 -> Unknown")
require_text("${policy}" "return configured > 0.0f ? configured : defaultRadius;" "radius 0 = default")
require_text("${policy}" "        if (radius <= 0.0f)\n            undecided = true;" "no usable radius -> Unknown (fail closed)")
require_text("${policy}" "if (!seen && guids.size() < MaxWatchers)" "watcher list capped")
forbid_text("${policy}" "#include <mutex>" "lock in the policy (the mutex lives in the manager)")
forbid_text("${policy}" "shared_mutex" "shared_mutex in the policy")
forbid_text("${policy}" "GetPlayers" "real-player map in the policy")
forbid_text("${policy}" "players)" "real-player map iterated in the policy")
# windows.h (included by RandomPlayerbotMgr.cpp under PLATFORM_WINDOWS) defines near and far as empty macros.
foreach(body policy mgr mgr_h)
  string(REGEX MATCH "[.](near|far)[^A-Za-z0-9_]|[ ](near|far)[{;]" win_macro "${${body}}")
  if(win_macro)
    message(FATAL_ERROR "#595 shortcut S0: member named near / far (empty macro under windows.h) in ${body}: ${win_macro}")
  endif()
endforeach()
require_text("${policy}" "std::atomic<std::uint32_t> nearAny{0};" "counter member nearAny")

# --- 3. Snapshot built only in UpdateAIInternal (world thread, after sMapMgr.Update) ---------------------------------
file(GLOB_RECURSE pb_sources "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(build_calls 0)
set(near_calls 0)
foreach(source ${pb_sources})
  file(READ "${source}" text)
  count_text("${text}" "BuildShortcutSnapshot()" n)
  math(EXPR build_calls "${build_calls} + ${n}")
  count_text("${text}" "ShortcutNear(" n)
  math(EXPR near_calls "${near_calls} + ${n}")
endforeach()
# declaration + definition + the one call
if(NOT build_calls EQUAL 3)
  message(FATAL_ERROR "#595 shortcut S0: BuildShortcutSnapshot() must appear 3 times (decl, def, one call), found ${build_calls}")
endif()
# S0 measures only: ShortcutNear( = the declaration, the definition and the policy header comment - no caller anywhere
# in the module. The first stage that gates a shortcut on it has to change this count on purpose.
if(NOT near_calls EQUAL 3)
  message(FATAL_ERROR "#595 shortcut S0: ShortcutNear( must appear 3 times (decl, def, policy comment; S0 has no caller), found ${near_calls}")
endif()
require_count("${mgr}" "BuildShortcutSnapshot()" 2 "definition and one call in RandomPlayerbotMgr.cpp")
between("${mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" "sPerformanceMonitor.Init(0, 0);" upd_pre_init)
require_text("${upd_pre_init}" "    ProcessQuestRescues();\n    BuildShortcutSnapshot();" "built right after ProcessQuestRescues")
require_text("${upd_pre_init}" "ReportShortcut();" "[Shortcut] minute line in the world pass")
between("${mgr}" "void RandomPlayerbotMgr::BuildShortcutSnapshot()" "\n}\n" build)
require_text("${build}" "if (!sPlayerbotAIConfig.shortcutTrace && !sPlayerbotAIConfig.shortcutNearScope)" "nothing built while all switches are 0")
require_text("${build}" "if (!ai::shortcut::BuildDue(shortcutLastBuildMs, nowMs))\n        return;" "rate limited")
require_text("${build}" "for (auto const& [guid, player] : players)" "real players read in the world thread")
require_text("${build}" "if (!player->IsGameMaster() || player->isGMVisible())" "GM rule of HasPlayerNearby")
require_text("${build}" "for (uint32 guid : sPlayerbotAIConfig.shortcutTestWatcherGuids)" "watchers added")
forbid_text("${build}" "TeleportTo" "teleport in the snapshot build")
# null / offline entries never reach the world thread's dereference; only real players (D6: alt bots are not players)
require_text("${build}" "        if (!player || !player->IsInWorld() || !IsRealPlayer(player))\n            continue;" "player null / in-world / real-player filter")
require_text("${build}" "        Player* watcher = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));\n        if (!watcher || !watcher->IsInWorld())\n            continue;" "watcher null / offline filter")
# one entry for the player and one for a camera body that differs (far sight)
between("${mgr}" "void AddShortcutEntries(ai::shortcut::Snapshot& snapshot, Player* player, bool watcher, float reactDistance)" "\n}\n" add_entries)
require_text("${add_entries}" "    snapshot.entries.push_back(entry);\n" "player entry")
require_text("${add_entries}" "    if (body && body != player && body->IsInWorld())\n    {" "camera body that differs, in world")
require_text("${add_entries}" "        snapshot.entries.push_back(view);\n" "camera entry")

# --- 4. One short std::mutex, lock_guard only, held for a pointer copy / swap ----------------------------------------
require_text("${mgr_h}" "std::mutex shortcutSnapshotMutex;" "plain std::mutex")
forbid_text("${mgr_h}" "std::shared_mutex shortcutSnapshotMutex" "shared_mutex for the snapshot")
require_count("${mgr}" "shortcutSnapshotMutex" 3 "every lock of the snapshot mutex")
require_count("${mgr}" "std::lock_guard<std::mutex> lock(shortcutSnapshotMutex);" 3 "lock_guard for every lock")
require_text("${build}" "{\n        std::lock_guard<std::mutex> lock(shortcutSnapshotMutex);\n        shortcutSnapshot.swap(published);\n    }" "publish = swap under the lock")
require_text("${mgr}" "{\n    std::lock_guard<std::mutex> lock(shortcutSnapshotMutex);\n    return shortcutSnapshot;\n}" "read = pointer copy under the lock")
require_count("${mgr}" "shortcutSnapshot.swap(" 2 "snapshot written only by the two swaps")
# the field itself: 3 x shortcutSnapshotMutex, the two swaps and the one locked return - nothing reads it unlocked
require_count("${mgr}" "shortcutSnapshot" 6 "every use of the snapshot field (mutex x3, swap x2, locked return)")
# the header: the comment, the mutex and the field only - no inline accessor that returns the field unlocked (recheck P12)
require_count("${mgr_h}" "shortcutSnapshot" 3 "snapshot field in RandomPlayerbotMgr.h (comment, mutex, field)")

# --- 5. No real-player map in the new map-thread code ---------------------------------------------------------------
between("${mgr}" "ai::shortcut::Near RandomPlayerbotMgr::ShortcutNear(" "\n}\n" near_body)
between("${mgr}" "void RandomPlayerbotMgr::CountShortcut(" "\n}\n" count_body)
between("${mgr}" "std::shared_ptr<ai::shortcut::Snapshot const> RandomPlayerbotMgr::ShortcutSnapshot()" "\n}\n" snap_body)
foreach(body near_body count_body snap_body)
  forbid_text("${${body}}" "GetPlayers" "real-player map read in ${body}")
  forbid_text("${${body}}" "players)" "real-player map iterated in ${body}")
  forbid_text("${${body}}" "TeleportTo" "teleport in ${body}")
endforeach()
foreach(body near_body count_body)
  require_text("${${body}}" "std::shared_ptr<ai::shortcut::Snapshot const> const snapshot = ShortcutSnapshot();" "locked pointer copy in ${body}")
endforeach()
# NearScope 0 = off at the query: ShortcutNear passes the configured scope (Evaluate answers Unknown for 0); only the
# trace measures with MeasureScope.
require_text("${near_body}" "ShortcutNowMs(), sPlayerbotAIConfig.shortcutNearScope," "ShortcutNear uses the configured NearScope")
forbid_text("${near_body}" "MeasureScope" "trace scope in ShortcutNear (NearScope 0 must answer Unknown)")
# the map-thread call sites keep their pre-S0 real-player map reads: one (FarMoveRouteWatched) in MovementActions.cpp,
# none at the ghost / repop sites
require_count("${movement}" "GetPlayers()" 1 "real-player map reads in MovementActions.cpp (pre-existing FarMoveRouteWatched only)")
require_count("${revive}" "GetPlayers()" 0 "real-player map reads in ReviveFromCorpseAction.cpp")
require_count("${repop}" "GetPlayers()" 0 "real-player map reads in ReleaseSpiritAction.h")
# the new manager header block (public query + private snapshot members) reads no real-player map
between("${mgr_h}" "// twow-repo#595 P4 S0 (AiPlayerbot.Shortcut.*): is a real player" "        PlayerBotMap players;" mgr_h_block)
forbid_text("${mgr_h_block}" "GetPlayers" "real-player map in the new manager header block")
forbid_text("${mgr_h_block}" "players)" "real-player map iterated in the new manager header block")
require_text("${count_body}" "if (!sPlayerbotAIConfig.shortcutTrace || !bot)\n        return;" "measurement only with Trace")
require_text("${count_body}" "ai::shortcut::Record(ai::shortcut::SharedCounters(), mech, source, destination);" "atomic counters")
require_text("${mgr_h}" "void CountShortcut(ai::shortcut::Mech mech, Player* bot, uint32 toMapId, float toX, float toY, float toZ);" "void: the count cannot steer the caller")

# --- 6. [Shortcut] lines -------------------------------------------------------------------------------------------
require_text("${mgr}" "[Shortcut] state=snapshot players=%u watchers=%u entries=%u age_ms=%llu stale=%u scope=%u radius=%.0f" "snapshot minute line")
require_text("${mgr}" "snapshot && ai::shortcut::Fresh(snapshot->builtAtMs, nowMs) ? 0u : 1u," "stale flag = not Fresh")
# Bounded output: only with Trace, at most once a minute; each line covers one minute (drained counters).
between("${mgr}" "void RandomPlayerbotMgr::ReportShortcut()" "\n}\n" report)
require_text("${report}" "std::shared_ptr<ai::shortcut::Snapshot const> const snapshot = ShortcutSnapshot();" "locked pointer copy in report")
require_text("${report}" "    if (!sPlayerbotAIConfig.shortcutTrace)\n    {\n        tracing = false;\n        return;\n    }" "minute line only with Trace")
require_text("${report}" "    if (now < lastReport + 60)\n        return;\n    lastReport = now;" "minute line at most once a minute")
require_text("${report}" "    if (!tracing)\n    {" "counts dropped when Trace is switched on again")
require_order("${report}" "if (now < lastReport + 60)" "sLog.outBasic(" "minute guard before the first line")
require_count("${report}" "ai::shortcut::Drain(counters.mech[i])" 2 "counters drained on Trace on and in each minute line")
forbid_text("${report}" ".load(" "cumulative counter read in the minute line")
require_count("${policy}" ".exchange(0, std::memory_order_relaxed);" 5 "Drain resets all five counters")
require_text("${mgr}" "[Shortcut] mech=%s teleports=%u near=%u notnear=%u unknown=%u near_src=%u near_dst=%u" "counter minute line")
require_text("${mgr}" "[Shortcut] mech=group_loop state=skip_real leader=%u member=%u" "B1 skip line")

# --- 7. Counters only measure: each count sits right before the unchanged teleport ----------------------------------
require_text("${movement}" "                // twow-repo#595 P4 S0 (AiPlayerbot.Shortcut.Trace): counted only, the teleport below is unchanged.\n                if (sPlayerbotAIConfig.shortcutTrace)\n                    sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::FarMove, bot, arrive.getMapId(), arrive.getX(), arrive.getY(), arrive.getZ());\n                return bot->TeleportTo(arrive.getMapId(), arrive.getX(), arrive.getY(), arrive.getZ(), farStart.getAngleTo(endPos));" "far-move arrival counted, teleport unchanged")
require_text("${movement}" "        if (!ai->HasPlayerNearby(teleportPosition) && !ai->HasPlayerNearby(startPos))\n        {\n            time_t now = time(0);\n            lastMove.nextTeleport = now + (time_t)MoveDelay(startPos.distance(teleportPosition));\n            // twow-repo#595 P4 S0 (AiPlayerbot.Shortcut.Trace): the hop (critic C02) counted only, the teleport is unchanged.\n            if (sPlayerbotAIConfig.shortcutTrace)\n                sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::Hop, bot, teleportPosition.getMapId(), teleportPosition.getX(), teleportPosition.getY(), teleportPosition.getZ());\n            return bot->TeleportTo(teleportPosition.getMapId(),\n                teleportPosition.getX(),\n                teleportPosition.getY(),\n                teleportPosition.getZ(),\n                startPos.getAngleTo(teleportPosition));" "hop counted, legacy guard and teleport unchanged")
require_text("${movement}" "if (farDistance > sPlayerbotAIConfig.reactDistance && !declined && !ai->HasPlayerNearby(farStart) && !ai->HasPlayerNearby(endPos) &&" "far-move gate unchanged in S0")
require_text("${revive}" "            bot->GetMotionMaster()->Clear();\n            // twow-repo#595 P4 S0 (AiPlayerbot.Shortcut.Trace): ghost to corpse (critic C06) counted only, the teleport is unchanged.\n            if (sPlayerbotAIConfig.shortcutTrace)\n                sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::GhostCorpse, bot, moveToPos.getMapId(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ());\n            bot->TeleportTo(moveToPos.getMapId(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), 0);" "ghost to corpse counted, teleport unchanged")
require_text("${repop}" "            if (sPlayerbotAIConfig.shortcutTrace)\n            {\n                if (defaultPlayerInfo)\n                    sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::Repop, bot, defaultPlayerInfo->mapId, defaultPlayerInfo->positionX, defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ);\n                else\n                    sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::Repop, bot, bot->GetHomebindMapId(), bot->GetHomebindX(), bot->GetHomebindY(), bot->GetHomebindZ());\n            }\n\n            if (defaultPlayerInfo)" "repop counted before the unchanged branch")
require_text("${repop}" "bot->TeleportTo(defaultPlayerInfo->mapId, defaultPlayerInfo->positionX, defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ, defaultPlayerInfo->orientation);" "repop spawn teleport unchanged")
require_text("${repop}" "                bot->TeleportToHomebind();\n                bot->SendHeartBeat();" "repop homebind teleport unchanged")
require_order("${repop}" "bot->ResurrectPlayer(1.0f, false);" "sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::Repop" "repop order unchanged (resurrect, then teleport)")
foreach(site movement revive repop)
  count_text("${${site}}" "CountShortcut(" n_calls)
  count_text("${${site}}" "if (sPlayerbotAIConfig.shortcutTrace)\n" n_guards)
  if(n_calls EQUAL 0 OR n_guards EQUAL 0)
    message(FATAL_ERROR "#595 shortcut S0: ${site}: no guarded CountShortcut")
  endif()
  forbid_text("${${site}}" "= sRandomPlayerbotMgr.CountShortcut(" "count result used in ${site}")
  forbid_text("${${site}}" "ShortcutNear(" "a proximity decision in ${site} (S0 measures only)")
endforeach()
count_text("${repop}" "CountShortcut(" n_repop)
if(NOT n_repop EQUAL 2)
  message(FATAL_ERROR "#595 shortcut S0: repop: expected 2 CountShortcut calls, found ${n_repop}")
endif()
count_text("${movement}" "CountShortcut(" n_move)
if(NOT n_move EQUAL 2)
  message(FATAL_ERROR "#595 shortcut S0: movement: expected 2 CountShortcut calls (hop, far move), found ${n_move}")
endif()

# --- 8. B1: the legacy random-teleport group loop -------------------------------------------------------------------
# The whole loop is pinned as one block - the 0-path (the leader's AI, the unchanged member move), the 1-path (the
# member's own AI, real members skipped, never the leader itself) and the null guard: any statement inserted, removed
# or changed fails here.
string(CONCAT b1_loop
  "            if (bot->GetGroup())\n"
  "            {\n"
  "                for (GroupReference* gref = bot->GetGroup()->GetFirstMember(); gref; gref = gref->next())\n"
  "                {\n"
  "                    Player* member = gref->getSource();\n"
  "                    PlayerbotAI* ai = GetBotAI(bot);\n"
  "                    // twow-repo#595 P4 B1 (AiPlayerbot.Shortcut.GroupLoopFix, critic X6): 1 = each member's own AI; a\n"
  "                    // real member (no bot AI, or a real session) is skipped and not moved. 0 = the leader's AI as before.\n"
  "                    // The leader itself is never a member to skip (it is excluded from the move by bot != member anyway).\n"
  "                    if (sPlayerbotAIConfig.shortcutGroupLoopFix && bot != member)\n"
  "                    {\n"
  "                        ai = GetBotAI(member);\n"
  "                        if (!::ai::shortcut::GroupLoopMovesMember(true, ai != nullptr, ai && ai->IsRealPlayer()))\n"
  "                        {\n"
  "                            sLog.outBasic(\"[Shortcut] mech=group_loop state=skip_real leader=%u member=%u\",\n"
  "                                bot->GetGUIDLow(), member->GetGUIDLow());\n"
  "                            continue;\n"
  "                        }\n"
  "                    }\n"
  "                    if (ai && bot != member)\n"
  "                    {\n"
  "                        if (member->IsTaxiFlying())\n"
  "                            member->GetMotionMaster()->MovementExpired();\n"
  "                        if (hearth && !hostileHome)\n"
  "                            member->SetHomebindToLocation(loc, area->ID);\n"
  "\n"
  "                        member->GetMotionMaster()->Clear();\n"
  "                        member->TeleportTo(loc.mapid, x, y, z, 0);\n"
  "                        member->SendHeartBeat();\n"
  "                        // twow-repo#595 P4 B1: null guard in both paths - with switch 0 a real member (no bot AI) is\n"
  "                        // still moved as before, but no longer crashes the world thread on a null AI.\n"
  "                        if (PlayerbotAI* memberAi = GetBotAI(member))\n"
  "                            memberAi->Reset(true);\n"
  "                    }\n"
  "\n"
  "                }\n"
  "            }\n"
  "            return;\n")
require_text("${mgr}" "${b1_loop}" "B1 group loop as one block (0-path, 1-path, null guard)")
require_count("${mgr}" "shortcutGroupLoopFix" 1 "the B1 switch read only in the group loop")
require_count("${mgr}" "state=skip_real" 1 "one B1 skip line")
require_text("${policy}" "inline bool GroupLoopMovesMember(bool fix, bool hasOwnBotAi, bool realSession)\n{\n    if (!fix)\n        return true;\n    return hasOwnBotAi && !realSession;\n}" "group-loop policy")
forbid_text("${mgr}" "GetBotAI(member)->Reset(true);" "unguarded member AI reset in RandomPlayerbotMgr.cpp")

message(STATUS "shortcut S0 source contract passed")
