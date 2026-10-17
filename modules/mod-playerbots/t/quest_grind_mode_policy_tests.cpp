// twow-repo#601 GrindMode (AiPlayerbot.QuestFirstProgression.GrindMode, owner decision 17.10.2026): who may not grind,
// which targets and destinations stay, the idle reasons and the bounded [QuestIdle] lines (QuestGrindModePolicy.h).
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "QuestGrindModePolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace ai::quest_grind_mode;

    // 1) Modes: only 2 changes anything; 1 is reserved (S2 / S4) and acts like 0; unknown values act like 0.
    {
        Require(ModeToday == 0 && ModeGapFiller == 1 && ModeNoGrind == 2, "mode numbers");
        Require(!NoGrind(0), "mode 0 = today's behaviour");
        Require(!NoGrind(1), "mode 1 = mode 0 until S4");
        Require(NoGrind(2), "mode 2 = no grind");
        Require(!NoGrind(3) && !NoGrind(0xFFFFFFFFu), "unknown values (also a negative conf value) act like 0");
    }

    // 2) Population: quest-first roster bots on their own below the max level.
    {
        Require(Applies(2, true, false, 30, 60), "quest-first bot on its own below 60");
        Require(!Applies(0, true, false, 30, 60) && !Applies(1, true, false, 30, 60), "modes 0 and 1 apply to nobody");
        Require(!Applies(2, false, false, 30, 60), "not quest-first (not on the roster / Enabled 0): untouched");
        Require(!Applies(2, true, true, 30, 60), "a bot led by a real player: untouched");
        Require(Applies(2, true, false, 59, 60), "level 59 still questing");
        Require(!Applies(2, true, false, 60, 60), "at the max level grinding is allowed again (\"bis die Stufe 60\")");
        Require(Applies(2, true, false, 60, 0), "max level unknown (0): no level bound");
        Require(Applies(2, true, false, 1, 60), "level 1");
    }

    // 3) Grind targets: off -> everything as before; on -> attackers and quest objectives only.
    {
        Require(TargetAllowed(false, false, false), "off: any candidate");
        Require(!TargetAllowed(true, false, false), "on: a plain mob is no target");
        Require(TargetAllowed(true, true, false), "on: an attacker stays (self-defence, group help)");
        Require(TargetAllowed(true, false, true), "on: a quest objective stays");
        Require(TargetAllowed(true, true, true), "on: an attacking quest objective stays");
    }

    // 4) Grind travel destinations: only the quest objective mob destinations stay.
    {
        Require(GrindDestinationActive(false, false), "off: every grind destination as before");
        Require(!GrindDestinationActive(true, false), "on: a pure grind destination is gone");
        Require(GrindDestinationActive(true, true), "on: a quest objective mob destination stays");
        // Forced kill trips (guild Farm / Kill order: grind mob, boss, skinning) - review fix.
        Require(ForcedKillTripAllowed(false, true, false), "off: every forced trip as before");
        Require(!ForcedKillTripAllowed(true, true, false), "on: no forced kill trip to a creature no quest needs");
        Require(ForcedKillTripAllowed(true, true, true), "on: a forced kill trip to a quest objective mob stays");
        Require(ForcedKillTripAllowed(true, false, false), "on: a forced trip that kills nothing (guild meeting, petition, gather node) stays");
    }

    // 5) Reasons.
    {
        Require(TargetReason(true) == Reason::Opportunistic, "on the way: opportunistic");
        Require(TargetReason(false) == Reason::NoTarget, "idle: no_target");
        Require(TargetNoteDue(true, false), "at the destination (working): noted");
        Require(TargetNoteDue(false, false), "no travel target: noted");
        Require(!TargetNoteDue(false, true), "on the way to a quest: silent (the action would not attack)");
        Require(IsMoneyCondition("val::should get money"), "money trigger");
        Require(IsMoneyCondition("travel request::val::should get money"), "money trigger behind the request gate");
        Require(!IsMoneyCondition("val::need travel purpose::64"), "plain grind trigger is no money trigger");
        Require(!IsMoneyCondition(""), "no condition is no money trigger");
        Require(RequestReason(true) == Reason::Money && RequestReason(false) == Reason::Fallback, "request reasons");
        Require(std::strcmp(ReasonName(Reason::Fallback), "fallback") == 0, "name fallback");
        Require(std::strcmp(ReasonName(Reason::Money), "money") == 0, "name money");
        Require(std::strcmp(ReasonName(Reason::Opportunistic), "opportunistic") == 0, "name opportunistic");
        Require(std::strcmp(ReasonName(Reason::NoTarget), "no_target") == 0, "name no_target");
        Require(std::strcmp(ReasonName(Reason::None), "-") == 0, "name none");
        Require(ReasonCount == 5 && std::size_t(Reason::NoTarget) == ReasonCount - 1, "every reason has a counter slot");
    }

    // 6) Bounded log (review fix): one line per reason and idle episode, never for None - alternating reasons do not
    //    log again (live shape: money, no_target, money, ... once per 1-2 s per idle bot).
    {
        std::uint8_t logged = 0;
        Require(LogDue(logged, Reason::Money), "first reason logs");
        logged = MarkLogged(logged, Reason::Money);
        Require(!LogDue(logged, Reason::Money), "same reason again: no line");
        Require(LogDue(logged, Reason::Fallback), "a new reason logs");
        Require(!LogDue(logged, Reason::None), "None never logs");
        Require(!LogDue(0, Reason::None), "None never logs (fresh bot)");
        Require(ReasonBit(Reason::None) == 0 && MarkLogged(0, Reason::None) == 0, "None has no bit");

        // Alternating Money / NoTarget / Money / NoTarget / Money: 2 lines, not one per change.
        logged = 0;
        unsigned lines = 0;
        Reason const walk[] = { Reason::Money, Reason::NoTarget, Reason::Money, Reason::NoTarget, Reason::Money,
            Reason::Fallback, Reason::NoTarget, Reason::Fallback };
        for (std::size_t i = 0; i < 5; ++i)
            if (LogDue(logged, walk[i]))
            {
                ++lines;
                logged = MarkLogged(logged, walk[i]);
            }
        Require(lines == 2, "alternating money / no_target logs 2 lines, not 5");
        for (Reason const reason : walk)
            if (LogDue(logged, reason))
            {
                ++lines;
                logged = MarkLogged(logged, reason);
            }
        Require(lines == 3, "fallback joins once");

        // Upper bound: every reason, many times - at most 4 lines per episode.
        logged = 0;
        lines = 0;
        for (unsigned round = 0; round < 1000; ++round)
            for (std::size_t r = 0; r < ReasonCount; ++r)
                if (LogDue(logged, Reason(r)))
                {
                    ++lines;
                    logged = MarkLogged(logged, Reason(r));
                }
        Require(lines == 4, "at most 4 lines per idle episode");
        Require(logged == (ReasonBit(Reason::Fallback) | ReasonBit(Reason::Money) | ReasonBit(Reason::Opportunistic) |
            ReasonBit(Reason::NoTarget)), "the four reason bits are distinct");

        // A found quest route ends the episode (the AI clears the mask): the first reason logs again.
        logged = 0;
        Require(LogDue(logged, Reason::Money), "new episode: money logs again");
    }

    // 7) Line formats.
    {
        Require(TargetText("grind_target", 299) == "grind_target:299", "target with entry");
        Require(TargetText("travel_purpose_grind", 0) == "travel_purpose_grind", "target without entry");
        Require(TargetText("", 5) == "-" && TargetText(nullptr, 5) == "-", "nothing blocked: -");
        Require(FormatIdleLine(Reason::Money, 4711, 12, 40, "request_travel_grind", "transient") ==
            "[QuestIdle] reason=money bot=4711 level=12 zone=40 target=request_travel_grind class=transient", "idle line");
        Require(FormatIdleLine(Reason::NoTarget, 1, 2, 3, "", "none") ==
            "[QuestIdle] reason=no_target bot=1 level=2 zone=3 target=- class=none", "idle line with nothing named");
        Require(FormatIdleLine(Reason::Fallback, 1, 2, 3, "x", nullptr) == "[QuestIdle] reason=fallback bot=1 level=2 zone=3 target=x class=none" &&
            FormatIdleLine(Reason::Fallback, 1, 2, 3, "x", "") == "[QuestIdle] reason=fallback bot=1 level=2 zone=3 target=x class=none",
            "no class name: class=none");
        std::array<std::uint32_t, ReasonCount> counts = {};
        counts[std::size_t(Reason::Fallback)] = 1;
        counts[std::size_t(Reason::Money)] = 2;
        counts[std::size_t(Reason::Opportunistic)] = 3;
        counts[std::size_t(Reason::NoTarget)] = 4;
        Require(FormatMinuteLine(counts) == "[QuestIdle] minute fallback=1 money=2 opportunistic=3 no_target=4", "minute line");
    }

    std::cout << "quest_grind_mode_policy_tests passed\n";
    return 0;
}
