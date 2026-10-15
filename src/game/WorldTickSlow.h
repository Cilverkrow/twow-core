#pragma once

// twow-repo#597 (TICK-01, Refs #592 #541): [WorldTickSlow] - one line per world tick whose work time
// reaches PerformanceLog.WorldTickSlowMs (default 0 = off), with the [WorldTick] phases of THAT tick
// plus the split of "managers" and "rest" into the jobs World::Update runs. Closes G1/G2/G4 of the
// tick map (one outlier tick could not be split up before). The same part ids are what the world
// thread publishes for [StallWatch] (PerformanceLog.StallWatchMs). Measurement only.
//
// Header-only and free of server types so t/tick_diag_597_test.cpp can check it without a world.
// World thread only, no lock.

#include "WorldTickTrace.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace world_tick_slow
{
// The parts of one world tick in execution order. Parts that are a [WorldTick] phase (Sessions,
// Map*, AsyncTasks, Results) are timed by the #541 clocks; the mgr_* and r_* parts are the split of
// "managers" and "rest". Idle = outside World::Update (sleep).
enum Part : uint8_t
{
    Idle,
    RPre,           // timers, game time, mass mail, total money and pending command queries
    RAh,            // sAuctionMgr.Update (expired auctions, once a minute)
    Sessions,
    RDispatch,      // uptime row, async task dispatch
    MgrTransport,
    Maps,           // sMapMgr.Update before its own phases start (timer, skipped ticks)
    MapTeleports,
    MapsPre,
    MapsWait,       // instances + wait_continents
    MapsSwitch,
    MapsPost,
    MgrBg,
    MgrLfg,
    MgrLft,
    MgrGuard,
    MgrZone,
    MgrDynVis,
    MgrEluna,
    MgrOffline,     // groups with offline leaders
    AsyncTasks,
    RSaveVars,      // SaveVariables + [PathUnitless] (once a minute)
    Results,
    RCorpses,
    REvents,
    RRemove,        // RemoveAllObjectsInRemoveList
    RMps,           // sMapPersistentStateMgr.Update
    RAccount,       // sAccountMgr.Update (ban list)
    RCli,           // ProcessCliCommands
    RTerrain,       // sTerrainMgr.Update (CleanUpGrids)
    RGuild,         // sGuildMgr.Update
    RHook,          // WorldScript OnUpdate hooks (bots, AhBot, remote commands, dungeon clear)
    ROther,         // census, shellcoin, maintenance, broadcasts, world buffs, daily quests, restart check
    PartCount
};

inline char const* PartName(uint32_t part)
{
    static char const* const names[PartCount] = { "idle", "r_pre", "r_ah", "sessions", "r_dispatch", "mgr_transport",
        "maps", "teleports", "maps_pre", "maps_wait", "switch_instances", "maps_post", "mgr_bg", "mgr_lfg", "mgr_lft",
        "mgr_guard", "mgr_zone", "mgr_dynvis", "mgr_eluna", "mgr_offline", "async_tasks", "r_savevars", "results",
        "r_corpses", "r_events", "r_remove", "r_mps", "r_account", "r_cli", "r_terrain", "r_guild", "r_hook", "r_other" };
    return part < PartCount ? names[part] : "?";
}

// The managers split, in the order of the line.
inline constexpr std::array<Part, 9> ManagerParts = { MgrTransport, MgrBg, MgrLfg, MgrLft, MgrGuard, MgrZone, MgrDynVis,
    MgrEluna, MgrOffline };
// The rest split, in the order of the line; r_other is the remainder of rest and printed last.
inline constexpr std::array<Part, 13> RestParts = { RPre, RAh, RDispatch, RSaveVars, RCorpses, REvents, RRemove, RMps,
    RAccount, RCli, RTerrain, RGuild, RHook };

// Microseconds per part of the running tick.
struct PartTimes
{
    std::array<uint64_t, PartCount> us{};

    void Add(uint32_t part, uint64_t value)
    {
        if (part < PartCount)
            us[part] += value;
    }

    void Clear() { us.fill(0); }
};

inline void AppendMs(std::string& line, char const* name, uint64_t us)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), " %s=%.1f", name, us / 1000.0);
    line += buf;
}

// The [WorldTickSlow] line of one tick. phases = PhaseWindow::tick after CloseTick (rest filled in).
// r_other = rest minus the timed rest parts (never negative): everything in rest no part covers,
// including the clock reads themselves.
inline std::string FormatLine(uint32_t tick, uint64_t updateUs, std::array<uint64_t, world_tick::PhaseCount> const& phases,
    PartTimes const& parts, uint32_t dropped)
{
    // world_tick and world_tick_slow share enumerator names (Sessions, MapsPre, Results ...): always
    // qualified here.
    static constexpr std::array<world_tick::Phase, 8> mapPhases = { world_tick::Sessions, world_tick::Teleports,
        world_tick::MapsPre, world_tick::Instances, world_tick::WaitContinents, world_tick::SwitchInstances,
        world_tick::MapsPost, world_tick::Managers };
    std::string line = "[WorldTickSlow] tick=" + std::to_string(tick);
    AppendMs(line, "ms", updateUs);
    for (world_tick::Phase p : mapPhases)
        AppendMs(line, world_tick::PhaseName(p), phases[p]);
    for (Part part : ManagerParts)
        AppendMs(line, PartName(part), parts.us[part]);
    AppendMs(line, world_tick::PhaseName(world_tick::AsyncTasks), phases[world_tick::AsyncTasks]);
    AppendMs(line, world_tick::PhaseName(world_tick::Results), phases[world_tick::Results]);
    uint64_t const rest = phases[world_tick::Rest];
    AppendMs(line, world_tick::PhaseName(world_tick::Rest), rest);
    uint64_t restParts = 0;
    for (Part part : RestParts)
    {
        AppendMs(line, PartName(part), parts.us[part]);
        restParts += parts.us[part];
    }
    AppendMs(line, PartName(ROther), rest > restParts ? rest - restParts : 0);
    line += " dropped=" + std::to_string(dropped);
    return line;
}

// True when the tick's work time reaches the threshold (0 = off).
inline bool IsSlow(uint64_t updateUs, uint32_t thresholdMs)
{
    return thresholdMs && updateUs >= uint64_t(thresholdMs) * 1000;
}
}
