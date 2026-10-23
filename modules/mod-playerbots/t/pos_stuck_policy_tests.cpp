#include "PosStuckPolicy.h"

#include <cstdlib>
#include <iostream>

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

constexpr std::uint32_t Minute = 60 * 1000;
constexpr std::uint32_t Window = 10 * Minute;
constexpr float Progress = 5.0f;

ai::pos_stuck::Observation At(std::uint32_t now, float distance, bool combat = false, std::uint64_t key = 7, float x = 0.0f)
{
    ai::pos_stuck::Observation o;
    o.now = now;
    o.key = key;
    o.mapId = 0;
    o.x = x;
    o.y = 0.0f;
    o.z = 0.0f;
    o.distance = distance;
    o.combat = combat;
    return o;
}
}

int main()
{
    using namespace ai::pos_stuck;

    // twow-repo#594: QFO-clean bot 1244 - 335 yd to the taker, unchanged; the first observation only starts the window.
    {
        State state;
        Require(Observe(state, At(1000, 335.0f), Window, Progress) == Verdict::None, "first observation starts the window");
        Require(Observe(state, At(1000 + Window - 1, 335.0f), Window, Progress) == Verdict::None, "inside the window: nothing");
        Require(Observe(state, At(1000 + Window, 335.0f), Window, Progress) == Verdict::Detect, "window over without progress: detect");
        Require(Observe(state, At(1000 + Window + 1, 335.0f), Window, Progress) == Verdict::None, "detect restarts the window");
    }

    // Progress restarts the window and sets the anchor; less than the progress yards does not.
    {
        State state;
        Observe(state, At(0, 335.0f), Window, Progress);
        Require(Observe(state, At(5 * Minute, 331.0f, false, 7, 4.0f), Window, Progress) == Verdict::None, "4 yd closer: no progress");
        Require(!state.hasAnchor, "no anchor without progress");
        Require(Observe(state, At(Window, 331.5f), Window, Progress) == Verdict::Detect, "3.5 yd net over the window: detect");
    }
    {
        State state;
        Observe(state, At(0, 335.0f), Window, Progress);
        Require(Observe(state, At(5 * Minute, 330.0f, false, 7, 12.0f), Window, Progress) == Verdict::None, "5 yd closer: progress");
        Require(state.hasAnchor && state.anchorX == 12.0f, "progress point is the anchor");
        Require(Observe(state, At(Window + Minute, 330.0f), Window, Progress) == Verdict::None, "window restarted at the progress");
        Require(Observe(state, At(5 * Minute + Window, 330.0f), Window, Progress) == Verdict::Detect, "a full window after the progress: detect");
    }

    // Moving away and back without net progress: still stuck (distance, not position, counts).
    {
        State state;
        Observe(state, At(0, 100.0f), Window, Progress);
        Observe(state, At(3 * Minute, 160.0f), Window, Progress);
        Require(Observe(state, At(Window, 98.0f), Window, Progress) == Verdict::Detect, "2 yd net over the window: detect");
    }

    // Fights count into the window but never fire it: the next observation after the fight decides.
    {
        State state;
        Observe(state, At(0, 335.0f), Window, Progress);
        Require(Observe(state, At(4 * Minute, 335.0f, true), Window, Progress) == Verdict::None, "fight inside the window");
        Require(Observe(state, At(Window + Minute, 335.0f, true), Window, Progress) == Verdict::None, "window over, still fighting: no detect");
        Require(Observe(state, At(Window + 2 * Minute, 335.0f, false), Window, Progress) == Verdict::Detect, "first observation after the fight: detect");
    }

    // Excluded (dead, taxi, transport, master, instance, rest, other map, PartialNoop): no window; a fresh one after it.
    {
        State state;
        Observe(state, At(0, 335.0f), Window, Progress);
        Observation off = At(Window, 335.0f);
        off.excluded = true;
        Require(Observe(state, off, Window, Progress) == Verdict::None && !state.initialized, "excluded: window cleared");
        Require(Observe(state, At(Window + Minute, 335.0f), Window, Progress) == Verdict::None, "back in: fresh window");
        Require(Observe(state, At(2 * Window, 335.0f), Window, Progress) == Verdict::None, "fresh window not over yet");
        state.Reset();
        Require(Observe(state, At(2 * Window + Minute, 335.0f), Window, Progress) == Verdict::None, "Reset (PartialNoop hit): fresh window");
    }

    // A new target starts a fresh window; a map change clears the anchor.
    {
        State state;
        Observe(state, At(0, 335.0f), Window, Progress);
        Observe(state, At(Minute, 320.0f, false, 7, 30.0f), Window, Progress);
        Require(Observe(state, At(Window + Minute - 1, 50.0f, false, 8), Window, Progress) == Verdict::None, "new target: fresh window");
        Require(state.hasAnchor, "anchor kept across targets");
        Observation other = At(Window + 2 * Minute, 50.0f, false, 8);
        other.mapId = 1;
        Observe(state, other, Window, Progress);
        Require(!state.hasAnchor, "map change clears the anchor");
    }

    // The anchor is usable for a local unstuck only 5..300 yd away on the same map.
    {
        State state;
        state.hasAnchor = true;
        state.anchorMapId = 0;
        state.anchorX = 10.0f;
        state.anchorY = 0.0f;
        Require(AnchorUsable(state, 0, 0.0f, 0.0f), "10 yd away: usable");
        Require(!AnchorUsable(state, 0, 8.0f, 0.0f), "2 yd away: no help");
        Require(!AnchorUsable(state, 0, 400.0f, 0.0f), "390 yd away: not local");
        Require(!AnchorUsable(state, 1, 0.0f, 0.0f), "other map: never");
        state.hasAnchor = false;
        Require(!AnchorUsable(state, 0, 0.0f, 0.0f), "no anchor: never");
    }

    // The series: park, unstuck, home bind; a later episode starts a new series; moves rate-limited.
    {
        constexpr std::uint32_t Episode = 30 * Minute, Rate = 30 * Minute;
        Episodes e;
        Require(Escalate(e, 1000, Episode, Rate, true) == Step::Park, "1st episode: park");
        Require(Escalate(e, 1000 + 12 * Minute, Episode, Rate, true) == Step::Unstuck, "2nd within 30 min: unstuck");
        Require(Escalate(e, 1000 + 24 * Minute, Episode, Rate, true) == Step::Park, "3rd, last move 12 min ago: park only (rate)");
        Require(Escalate(e, 1000 + 44 * Minute, Episode, Rate, true) == Step::Teleport, "4th, last move 44 min ago: home bind");
        Require(Escalate(e, 1000 + 120 * Minute, Episode, Rate, true) == Step::Park, "76 min after the last: new series, park");
        Require(Escalate(e, 1000 + 125 * Minute, Episode, Rate, true) == Step::Unstuck, "new series 2nd, last move 81 min ago: unstuck");
        Require(Escalate(e, 1000 + 130 * Minute, Episode, Rate, true) == Step::Park, "rate holds across series");
    }

    // OB-10 review: a move only for a bot that stood - a long detour (no closer, but the bot moved) is parked only, the series
    // and the rate stay untouched.
    {
        constexpr std::uint32_t Episode = 30 * Minute, Rate = 30 * Minute;
        Episodes e;
        Require(Escalate(e, 1000, Episode, Rate, false) == Step::Park, "1st episode of a detour: park (as always)");
        Require(Escalate(e, 1000 + 12 * Minute, Episode, Rate, false) == Step::Park, "2nd, but the bot moved: park only");
        Require(!e.moved, "no move consumed");
        Require(Escalate(e, 1000 + 20 * Minute, Episode, Rate, true) == Step::Unstuck, "then a real stand: unstuck");
    }
    {
        State state;
        Observe(state, At(0, 335.0f, false, 7, 100.0f), Window, Progress);
        Require(Observe(state, At(Window, 333.0f, false, 7, 105.0f), Window, Progress) == Verdict::Detect, "stood (5 yd drift): detect");
        Require(StoodStill(state) && state.detectDisplacement < 6.0f, "5 yd net displacement: stood");
        Observe(state, At(Window + Minute, 333.0f, false, 7, 105.0f), Window, Progress);
        Require(Observe(state, At(2 * Window, 332.0f, false, 7, 400.0f), Window, Progress) == Verdict::Detect, "detour: no closer, detect");
        Require(!StoodStill(state) && state.detectDisplacement > 290.0f, "295 yd net displacement: no move");
    }

    // OB-10 review: a home bind only on the bot's own map and zone (a #587 rebind lies in another start area).
    Require(BindNear(0, 12, 0, 12), "bind in Elwynn, bot in Elwynn: near");
    Require(!BindNear(0, 1, 0, 12), "bind in Dun Morogh, bot in Elwynn: far");
    Require(!BindNear(1, 12, 0, 12), "other map: far");
    Require(!BindNear(0, 0, 0, 0), "unknown zone: far");

    // Keys: entry and quest; a gameobject entry (negative) is a distinct key.
    Require(KeyOf(197, 21) != KeyOf(197, 22) && KeyOf(197, 21) != KeyOf(198, 21), "entry and quest make the key");
    Require(KeyOf(-1730, 0) != KeyOf(1730, 0), "negative entry distinct");
    Require(StepName(Step::Park)[0] == 'p' && StepName(Step::Unstuck)[0] == 'u' && StepName(Step::Teleport)[0] == 't', "step names");

    // Counters: summary once per minute to one caller, line budget.
    {
        Counters counters;
        std::uint32_t d = 0, u = 0, t = 0, l = 0;
        Require(!counters.Roll(600, d, u, t, l), "no summary for the very first minute");
        Require(counters.Take(1), "first line within the budget");
        Require(!counters.Take(1), "second line over the budget");
        counters.unstuck.fetch_add(1);
        Require(counters.Roll(660, d, u, t, l), "next minute: summary");
        Require(d == 2 && u == 1 && t == 0 && l == 1, "every episode counted, one logged");
    }

    std::cout << "pos_stuck_policy_tests passed\n";
    return 0;
}
