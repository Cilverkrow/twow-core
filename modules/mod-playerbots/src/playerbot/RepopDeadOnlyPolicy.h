#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ai::repop_dead_only
{
// twow-repo#575 A3 step 4 S4-2 (AiPlayerbot.Rescue.RepopDeadOnly, OB-00 07.11.2026; design
// ws-10/cli-pathfinding/a3-step4-route/DESIGN-STEP4-RESCUE-DANGER-20261107.md d54dfd0e, addendum 1 3f6726b8): repop only when
// dead. Pure rules, no game types, no config reads, no logging.
//
// Why: RepopAction::isUseful checked only BG and an active player master (A3 W11, design F2). "unstuck" sent an ALIVE bot to
// repop for "move long stuck" / "combat long stuck" whenever its hearthstone was not useful (e.g. the homebind zone above the
// bot's level): a teleport to the race start or homebind, a group leave and the travel target nulled. With the switch an alive
// bot without a usable hearthstone asks for QuestRescue (the level-matched hub teleport with its per-bot and global limits),
// and gets a reset when that is not possible (OB-00 07.11. decision 2). A dead bot (ghost) repops as before.

// Cohort mode (CohortPolicy.h): 0 off, 1 all, 2 cohort B, 3 cohort A; anything else is off.
inline std::uint32_t ClampMode(int value)
{
    return value >= 0 && value <= 3 ? std::uint32_t(value) : 0u;
}

// The repop gate: a bot led by a real player keeps the legacy answer (its player's "repop" command); a dead bot repops.
inline bool BlocksRepop(bool alive, bool realPlayerMaster)
{
    return alive && !realPlayerMaster;
}

enum class Source : std::uint8_t
{
    MoveLong = 0,     // "move long stuck"
    CombatLong,       // "combat long stuck"
    Count
};

inline char const* SourceName(Source s)
{
    switch (s)
    {
        case Source::MoveLong: return "move_long";
        case Source::CombatLong: return "combat_long";
        default: return "unknown";
    }
}

// What an alive bot gets instead of the repop (the hearthstone is decided before, unchanged).
enum class Step : std::uint8_t
{
    QuestRescue = 0,  // the request is queued (new or already pending); the world thread teleports to a level-matched hub
    Reset,            // QuestRescue not possible now (per-bot limit): the same reset as for the short stuck
    Count
};

inline char const* StepName(Step s)
{
    switch (s)
    {
        case Step::QuestRescue: return "quest_rescue";
        case Step::Reset: return "reset";
        default: return "unknown";
    }
}

// questRescueQueued = after asking, a request is pending for the bot (the per-bot limit let it through, or one was pending).
inline Step AliveStep(bool questRescueQueued)
{
    return questRescueQueued ? Step::QuestRescue : Step::Reset;
}

// Realm-wide counters for the [RepopDeadOnlyMin] minute line (relaxed atomics; sums over a window are exact).
struct Counters
{
    std::array<std::array<std::atomic<std::uint32_t>, std::size_t(Step::Count)>, std::size_t(Source::Count)> steps{};
    std::atomic<std::uint64_t> lastLogSeconds{0};

    void Count(Source source, Step step)
    {
        if (source < Source::Count && step < Step::Count)
            steps[std::size_t(source)][std::size_t(step)].fetch_add(1, std::memory_order_relaxed);
    }

    std::uint32_t Take(Source source, Step step)
    {
        return steps[std::size_t(source)][std::size_t(step)].exchange(0, std::memory_order_relaxed);
    }

    // True once per minute for one caller.
    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t last = lastLogSeconds.load(std::memory_order_relaxed);
        if (nowSeconds < last + 60)
            return false;
        return lastLogSeconds.compare_exchange_strong(last, nowSeconds, std::memory_order_relaxed);
    }
};

inline Counters& SharedCounters()
{
    static Counters counters;
    return counters;
}
}
