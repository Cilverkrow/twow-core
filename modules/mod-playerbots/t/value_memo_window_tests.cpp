#include "strategy/ValueMemoWindow.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

// twow-repo#541 (audit A25): semantics of the per-bot DoNextAction memo window behind
// AiPlayerbot.Perf.PartyTargetMemo. The Memo struct below mirrors PartyMemberValue::Get/Set/Reset
// (the source contract pins those to the same shape): MemoAllowed() == false always recomputes, and
// a hit whose unit fails the same-map re-check recomputes too.

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

struct Memo
{
    ai::ValueMemoWindow& window;
    bool memoAllowed = true;
    bool keptUnitOnBotMap = true;  // stands for "in world and FindMap() == bot->FindMap()"
    int computations = 0;
    std::uint32_t memoEpoch = 0;

    int Get()
    {
        if (!memoAllowed)
        {
            ++computations;
            return computations;
        }
        std::uint32_t const epoch = window.Epoch();
        if (epoch && epoch == memoEpoch && keptUnitOnBotMap)
            return computations;
        ++computations;  // stands for UnitCalculatedValue::Get -> Calculate
        memoEpoch = epoch;
        return computations;
    }
    void Set() { memoEpoch = 0; }
    void Reset() { memoEpoch = 0; }
};
}

int main()
{
    // Closed window: every read recomputes (= today's interval-1 behaviour).
    {
        ai::ValueMemoWindow w;
        Require(w.Epoch() == 0, "fresh window is closed");
        Memo m{ w };
        m.Get(); m.Get(); m.Get();
        Require(m.computations == 3, "closed window never memoizes");
    }

    // Switch off: a null scope opens nothing.
    {
        ai::ValueMemoWindow w;
        {
            ai::ValueMemoWindowScope const scope(nullptr);
            Require(w.Epoch() == 0, "null scope leaves the window closed");
        }
    }

    // One pass: trigger + isUseful + reach isUseful + isPossible + Execute = 1 computation.
    {
        ai::ValueMemoWindow w;
        Memo m{ w };
        {
            ai::ValueMemoWindowScope const scope(&w);
            Require(w.Epoch() != 0, "open window has a non-zero epoch");
            for (int i = 0; i < 5; ++i)
                m.Get();
            Require(m.computations == 1, "five reads in one window compute once");

            w.Advance();  // ListenAndExecute after Execute
            m.Get(); m.Get();
            Require(m.computations == 2, "Advance after Execute forces exactly one recompute");

            m.Set();
            m.Get();
            Require(m.computations == 3, "Set drops the memo");
            m.Reset();
            m.Get();
            Require(m.computations == 4, "Reset drops the memo");
        }
        Require(w.Epoch() == 0, "scope end closes the window");
        m.Get(); m.Get();
        Require(m.computations == 6, "reads after the pass recompute every time");

        {
            ai::ValueMemoWindowScope const scope(&w);
            m.Get();
            Require(m.computations == 7, "next pass recomputes on its first read");
        }
    }

    // Randomised value (soulstone) opts out: every read in the window recomputes.
    {
        ai::ValueMemoWindow w;
        Memo m{ w };
        m.memoAllowed = false;
        ai::ValueMemoWindowScope const scope(&w);
        m.Get(); m.Get(); m.Get();
        Require(m.computations == 3, "MemoAllowed() == false never memoizes");
    }

    // A kept unit that left the bot's map (or the world) is not returned: the read recomputes.
    {
        ai::ValueMemoWindow w;
        Memo m{ w };
        ai::ValueMemoWindowScope const scope(&w);
        m.Get();
        m.keptUnitOnBotMap = false;
        m.Get();
        Require(m.computations == 2, "a hit that fails the same-map re-check recomputes");
        m.keptUnitOnBotMap = true;
        m.Get();
        Require(m.computations == 2, "the recomputed result is kept again");
    }

    // Nested open (e.g. a re-entrant DoNextAction) keeps the outer window open but new.
    {
        ai::ValueMemoWindow w;
        Memo m{ w };
        ai::ValueMemoWindowScope const outer(&w);
        m.Get();
        {
            ai::ValueMemoWindowScope const inner(&w);
            Require(w.Epoch() != 0, "inner scope open");
            m.Get();
            Require(m.computations == 2, "inner open advances the epoch");
        }
        Require(w.Epoch() != 0, "outer window still open after inner close");
        m.Get();
        Require(m.computations == 3, "inner close advances the epoch");
    }

    // Other threads: never inside the window; their Advance still invalidates; their Close is ignored.
    {
        ai::ValueMemoWindow w;
        Memo m{ w };
        ai::ValueMemoWindowScope const scope(&w);
        m.Get();

        std::uint32_t foreignEpoch = 12345;
        std::thread([&] { foreignEpoch = w.Epoch(); }).join();
        Require(foreignEpoch == 0, "a foreign thread sees no window");

        std::thread([&] { w.Close(); }).join();
        Require(w.Epoch() != 0, "a foreign Close does not close the owner's window");

        std::thread([&] { w.Advance(); }).join();
        m.Get();
        Require(m.computations == 2, "a foreign Advance invalidates the owner's memo");
    }

    // Region change: the bot is updated by another thread next tick.
    {
        ai::ValueMemoWindow w;
        { ai::ValueMemoWindowScope const scope(&w); }
        std::uint32_t otherEpoch = 0;
        std::uint32_t mainSeesDuringOther = 1;
        std::thread([&] {
            ai::ValueMemoWindowScope const scope(&w);
            otherEpoch = w.Epoch();
            std::thread([&] { mainSeesDuringOther = w.Epoch(); }).join();
        }).join();
        Require(otherEpoch != 0, "the new update thread owns the window");
        Require(mainSeesDuringOther == 0, "any thread other than the current owner sees no window");
        Require(w.Epoch() == 0, "window closed after the other thread's pass");
    }

    std::cout << "value_memo_window_tests passed\n";
    return 0;
}
