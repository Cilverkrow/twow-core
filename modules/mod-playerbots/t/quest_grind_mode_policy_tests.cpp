// twow-repo#601 GrindMode (AiPlayerbot.QuestFirstProgression.GrindMode, owner decision 17.10.2026): who may not grind,
// which targets and destinations stay, the idle reasons and the bounded [QuestIdle] lines, and (S2 / S4) where mode 1 grinds
// in the gaps of the quest search (QuestGrindModePolicy.h).
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

// A copy of MaNGOS::XP::GetGrayLevel (src/game/Formulas.h, no game header here): the grey level the game code passes in.
std::uint32_t GrayLevel(std::uint32_t level)
{
    if (level <= 5)
        return 0;
    if (level <= 39)
        return level - 5 - level / 10;
    return level - 1 - level / 5;
}
}

int main()
{
    using namespace ai::quest_grind_mode;

    // 1) Modes: 2 = no grind, 1 = the S2 / S4 gap filler (its own rules, never NoGrind); unknown values act like 0.
    {
        Require(ModeToday == 0 && ModeGapFiller == 1 && ModeNoGrind == 2, "mode numbers");
        Require(!NoGrind(0), "mode 0 = today's behaviour");
        Require(!NoGrind(1), "mode 1 is no NoGrind mode (it grinds inside the backoff: GapFiller)");
        Require(GapFiller(1) && !GapFiller(0) && !GapFiller(2), "only mode 1 is the gap filler (twow-repo#601 S2 / S4)");
        Require(!GapFiller(3) && !GapFiller(0xFFFFFFFFu), "unknown values (also a negative conf value) are no gap filler");
        Require(NoGrind(2), "mode 2 = no grind");
        Require(!NoGrind(3) && !NoGrind(0xFFFFFFFFu), "unknown values (also a negative conf value) act like 0");
    }

    // 2) Population: quest-first roster bots on their own below the max level.
    {
        Require(Applies(2, true, false, 30, 60), "quest-first bot on its own below 60");
        Require(!Applies(0, true, false, 30, 60) && !Applies(1, true, false, 30, 60), "mode 2 rules apply to nobody under modes 0 and 1");
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

    // 8) twow-repo#601 S2 / S4 (GrindMode 1, OB-10 rev. 2): the gap filler's population, R1 entry, R2 exit, the window line.
    {
        // Population: the same bots as Applies (mode 2), only under mode 1.
        Require(GapFillerApplies(1, true, false, 30, 60), "mode 1: quest-first bot on its own below 60");
        Require(!GapFillerApplies(0, true, false, 30, 60) && !GapFillerApplies(2, true, false, 30, 60) &&
            !GapFillerApplies(3, true, false, 30, 60), "modes 0, 2 and unknown: nobody grinds in the gaps");
        Require(!GapFillerApplies(1, false, false, 30, 60), "not quest-first: untouched (today's grind)");
        Require(!GapFillerApplies(1, true, true, 30, 60), "led by a real player: untouched");
        Require(GapFillerApplies(1, true, false, 59, 60) && !GapFillerApplies(1, true, false, 60, 60), "below the max level only");
        Require(GapFillerApplies(1, true, false, 60, 0), "max level unknown (0): no level bound");
        for (std::uint32_t level = 1; level <= 61; ++level)
            for (int questFirst = 0; questFirst < 2; ++questFirst)
                for (int realMaster = 0; realMaster < 2; ++realMaster)
                    Require(GapFillerApplies(1, questFirst != 0, realMaster != 0, level, 60) ==
                        Applies(2, questFirst != 0, realMaster != 0, level, 60), "the same population as mode 2");

        // R1: a window only while the backoff runs, never as a refresh renewal.
        Require(GapWindowOpen(true, false), "backoff running: window open");
        Require(!GapWindowOpen(false, false), "no backoff (quest route found / backoff over / park reset): no grind");
        Require(!GapWindowOpen(true, true), "a refresh never renews a mode-1 grind trip");
        Require(!GapWindowOpen(false, true), "no backoff, refresh: no grind");

        // R2: remaining backoff, at most RouteBackoffMaxSeconds (600), + 30 s.
        Require(GapWindowGraceSeconds == 30, "grace 30 s");
        Require(GapWindowSeconds(1120, 1000, 600) == 150, "120 s left: 150 s window");
        Require(GapWindowSeconds(1600, 1000, 600) == 630, "a full 10 min backoff: 630 s");
        Require(GapWindowSeconds(5000, 1000, 600) == 630, "never more than the longest backoff + 30 s");
        Require(GapWindowSeconds(1000, 1000, 600) == 30 && GapWindowSeconds(900, 1000, 600) == 30, "backoff over: grace only");
        Require(GapStatusMs(300000, 0, 1000, 600) == 300000 && GapStatusMs(0, 0, 1000, 600) == 0 && GapStatusMs(1, 0, 1000, 600) == 1,
            "no window (every other target): the status time stays");
        Require(GapStatusMs(300000, 1150, 1000, 600) == 150000, "WORK (5 min delay) ends with the window: 150 s");
        Require(GapStatusMs(3600000, 1630, 1000, 600) == 630000, "READY (1 h) ends with the window: 630 s");
        Require(GapStatusMs(0, 1100, 1000, 600) == 100000, "an unlimited status time gets the window too");
        Require(GapStatusMs(300000, 1000, 1000, 600) == 1 && GapStatusMs(300000, 900, 1000, 600) == 1, "window over: expires at once");
        Require(GapStatusMs(300000, 1000000, 1000, 600) == 630000, "clock stepped back: never longer than the longest window");
        // Review C-F1 / B-F1: a COOLDOWN of a gap trip never outlives the window (the shorter of the two).
        Require(GapCooldownMs(60000, 0, 1000, 600) == 60000 && GapCooldownMs(0, 0, 1000, 600) == 0,
            "no window (every other target): the cooldown stays");
        Require(GapCooldownMs(60000, 1030, 1000, 600) == 30000, "60 s cooldown, 30 s left: ends with the window");
        Require(GapCooldownMs(60000, 1300, 1000, 600) == 60000, "60 s cooldown, 300 s left: the cooldown");
        Require(GapCooldownMs(0, 1100, 1000, 600) == 100000, "an unlimited cooldown gets the window");
        Require(GapCooldownMs(60000, 1000, 1000, 600) == 1 && GapCooldownMs(60000, 900, 1000, 600) == 1, "window over: at once");
        Require(GapCooldownMs(3600000, 1000000, 1000, 600) == 630000, "clock stepped back: never longer than the longest window");

        // The window line: level window [max(gray + 1, L - 5), L + 1].
        Require(GapWindowLowLevel(15, 9) == 10 && GapWindowHighLevel(15) == 16, "L15 (gray 9): 10-16");
        Require(GapWindowLowLevel(20, 13) == 15 && GapWindowHighLevel(20) == 21, "L20 (gray 13): 15-21");
        Require(GapWindowLowLevel(60, 47) == 55 && GapWindowHighLevel(60) == 61, "L60 (gray 47): 55-61");
        Require(GapWindowLowLevel(10, 4) == 5, "L10 (gray 4): 5");
        Require(GapWindowLowLevel(3, 0) == 1 && GapWindowLowLevel(1, 0) == 1 && GapWindowLowLevel(6, 0) == 1, "low levels: 1, no wrap");
        Require(FormatGapWindowLine(4711, 15, 120, 10, 16, 1) ==
            "[QuestSearch] state=grind_window bot=4711 level=15 backoff_s=120 window=10-16 map=1", "grind_window line");
    }

    // 9) twow-repo#601 S2 commit 2 (OB-10 rev. 2): the level window [max(gray + 1, L - 5), L + 1] (R3 destinations, R4 plain
    //    targets), own map, the R4 rule, no_dest once per window and the [GrindWindow] minute line.
    {
        // Gray edges (GetGrayLevel: L <= 5 -> 0, L <= 39 -> L - 5 - L / 10, else L - 1 - L / 5).
        Require(GrayLevel(5) == 0 && GrayLevel(6) == 1 && GrayLevel(9) == 4 && GrayLevel(10) == 4 && GrayLevel(39) == 31 &&
            GrayLevel(40) == 31 && GrayLevel(60) == 47, "reference grey levels");
        Require(GapWindowLowLevel(1, GrayLevel(1)) == 1 && GapWindowHighLevel(1) == 2, "L1: 1-2");
        Require(GapWindowLowLevel(5, GrayLevel(5)) == 1 && GapWindowHighLevel(5) == 6, "L5 (no grey yet): 1-6");
        Require(GapWindowLowLevel(6, GrayLevel(6)) == 2 && GapWindowHighLevel(6) == 7, "L6 (gray 1): gray + 1 = 2 above L - 5 = 1");
        Require(GapWindowLowLevel(9, GrayLevel(9)) == 5, "L9 (gray 4): gray + 1 = 5 above L - 5 = 4");
        Require(GapWindowLowLevel(10, GrayLevel(10)) == 5, "L10 (gray 4): gray + 1 = L - 5 = 5");
        Require(GapWindowLowLevel(18, GrayLevel(18)) == 13 && GapWindowHighLevel(18) == 19, "L18 (gray 12): 13-19");
        Require(GapWindowLowLevel(24, GrayLevel(24)) == 19 && GapWindowHighLevel(24) == 25, "L24 (gray 17): 19-25");
        Require(GapWindowLowLevel(30, GrayLevel(30)) == 25, "L30 (gray 22): 25");
        Require(GapWindowLowLevel(39, GrayLevel(39)) == 34 && GapWindowLowLevel(40, GrayLevel(40)) == 35, "L39 / L40 across the grey formula switch");
        Require(GapWindowLowLevel(60, GrayLevel(60)) == 55 && GapWindowHighLevel(60) == 61, "L60 (gray 47): 55-61");

        // L1-L60: never grey, never more than 5 below or 1 above, never empty, and L - 5 decides from L10 on.
        for (std::uint32_t level = 1; level <= 60; ++level)
        {
            std::uint32_t const gray = GrayLevel(level);
            std::uint32_t const low = GapWindowLowLevel(level, gray);
            std::uint32_t const high = GapWindowHighLevel(level);
            Require(low > gray, "the window is never grey (low above the grey level)");
            Require(low >= 1 && low <= level && high == level + 1, "low in 1..L, high L + 1");
            Require(level <= 5 || low >= level - 5, "never more than 5 levels below");
            Require(level < 10 || low == level - 5, "from L10 on the low end is L - 5");
            Require(level < 6 || level > 9 || low == gray + 1, "L6-9: the low end is gray + 1");
            Require(InGapWindow(low, high, low) && InGapWindow(low, high, high) && InGapWindow(low, high, level),
                "both ends and the bot level are inside");
            Require(!InGapWindow(low, high, high + 1), "L + 2 is outside");
            Require(!InGapWindow(low, high, gray) && (low == 1 || !InGapWindow(low, high, low - 1)), "grey / below the low end is outside");
        }
        Require(!InGapWindow(0, 0, 0) && !InGapWindow(0, 0, 10), "no window (not the population): nothing is inside (fails closed)");
        Require(!InGapWindow(15, 21, 0), "an unknown area level (0) is outside");
        Require(!InGapWindow(15, 21, 0xFFFFFFFEu), "an unknown area (-2 as uint32) is outside");

        // Own map (precedent #414).
        Require(GapGrindStaysOnMap(false, true, 0, 1), "not the gap filler: other maps as before");
        Require(GapGrindStaysOnMap(true, false, 0, 1), "no grind destination: other maps as before");
        Require(GapGrindStaysOnMap(true, true, 1, 1), "a grind destination on the own map stays");
        Require(!GapGrindStaysOnMap(true, true, 0, 1), "a gap grind destination on another map is skipped");

        // R4: only the plain grind source of the population, levels outside the window.
        Require(!GapTargetSkipped(false, false, false, 15, 21, 5), "not the gap filler: a grey candidate as before");
        Require(GapTargetSkipped(true, false, false, 15, 21, 14), "plain, one below the window: skipped");
        Require(GapTargetSkipped(true, false, false, 15, 21, 22), "plain, L + 2: skipped");
        Require(!GapTargetSkipped(true, false, false, 15, 21, 15) && !GapTargetSkipped(true, false, false, 15, 21, 21),
            "plain, at the window ends: kept");
        Require(!GapTargetSkipped(true, true, false, 15, 21, 5), "a quest mob stays, a grey one too");
        Require(!GapTargetSkipped(true, false, true, 15, 21, 5), "a creature on the bot stays (self-defence)");
        Require(GapTargetSkipped(true, false, false, 0, 0, 20), "no window although the gap filler: skipped (fails closed)");

        // no_dest: once per window (backoff), never after its trip started, never outside a window.
        Require(GapNoDestDue(true, 1600, 0, 0), "first empty fetch of a window counts");
        Require(!GapNoDestDue(true, 1600, 0, 1600), "the same window again: no second count");
        Require(!GapNoDestDue(true, 1600, 1600, 0), "a trip of this window started: no count");
        Require(GapNoDestDue(true, 2200, 1600, 1600), "the next backoff is a new window");
        Require(!GapNoDestDue(false, 1600, 0, 0), "no backoff running: no window, no count");

        // [GrindWindow] minute line, omitted when every count is 0.
        std::array<std::uint32_t, WindowEventCount> counts = {};
        Require(FormatWindowMinuteLine(counts).empty(), "all 0: no line");
        counts[std::size_t(WindowEvent::R4Skips)] = 1;
        Require(FormatWindowMinuteLine(counts) == "[GrindWindow] minute starts=0 expired=0 engaged=0 grey_engaged=0 no_dest=0 r4_skips=1",
            "one count: a line");
        counts[std::size_t(WindowEvent::Starts)] = 2;
        counts[std::size_t(WindowEvent::Expired)] = 3;
        counts[std::size_t(WindowEvent::Engaged)] = 4;
        counts[std::size_t(WindowEvent::GreyEngaged)] = 5;
        counts[std::size_t(WindowEvent::NoDest)] = 6;
        Require(FormatWindowMinuteLine(counts) == "[GrindWindow] minute starts=2 expired=3 engaged=4 grey_engaged=5 no_dest=6 r4_skips=1",
            "minute line format");
        Require(WindowEventCount == 6 && std::size_t(WindowEvent::R4Skips) == WindowEventCount - 1, "every event has a counter slot");
    }

    // 10) Review fixes: the gap trip's condition is R1, never the money condition (B-F3, recheck: the money and the plain
    // trigger share one basket, so the money event is not refused but replaced), no group grind copy (C-F3 / B-F2).
    {
        Require(GapTripCondition(true, "should get money", 4096) == "need travel purpose::4096",
            "a gap trip queued by the money trigger keeps R1 as its condition");
        Require(!IsMoneyCondition(GapTripCondition(true, "should get money", 4096)), "never the money condition for a gap trip");
        Require(GapTripCondition(true, "need travel purpose::4096", 4096) == "need travel purpose::4096", "the plain R1 request: R1");
        Require(GapTripCondition(false, "should get money", 4096) == "should get money", "not the gap filler: the event source");
        Require(GapTripCondition(false, "need travel purpose::4096", 4096) == "need travel purpose::4096", "not the gap filler: as before");
        Require(GapTripCondition(false, "", 4096).empty(), "not the gap filler: an empty source stays empty");
        Require(!GapGroupCopyAllowed(true, true, false), "a member's grind trip is no copy for the population");
        Require(GapGroupCopyAllowed(true, true, true), "a member's grind destination this bot needs for a quest stays");
        Require(GapGroupCopyAllowed(true, false, false), "a member's other trip (quest, vendor, ...) stays");
        Require(GapGroupCopyAllowed(false, true, false), "not the gap filler: every copy as before");
    }

    std::cout << "quest_grind_mode_policy_tests passed\n";
    return 0;
}
