#include "ReleaseVeto.h"

#include <cstdint>
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

Player* const kKept = reinterpret_cast<Player*>(0x1000);
Player* const kOther = reinterpret_cast<Player*>(0x2000);
int g_calls = 0;
std::int64_t g_seenSeconds = -2;

// Keeps kKept for its first 600 s; an unknown death time releases.
bool KeepOne(Player* bot, std::int64_t deadSeconds)
{
    ++g_calls;
    g_seenSeconds = deadSeconds;
    return bot == kKept && deadSeconds >= 0 && deadSeconds < 600;
}

bool KeepNone(Player*, std::int64_t)
{
    return false;
}
}

int main()
{
    using namespace ai::release_veto;

    // twow-repo#629 G1 (26.10.2026): the stock auto release asks the registered predicate; without one it releases as before.
    Require(!Vetoed(kKept, 10), "no predicate: never vetoed (stock)");
    Register(&KeepOne);
    Require(Vetoed(kKept, 10) && g_seenSeconds == 10, "the predicate keeps its bot a corpse and sees the seconds since the death");
    Require(!Vetoed(kKept, 600), "the predicate's own bound releases");
    Require(!Vetoed(kKept, -1) && g_seenSeconds == -1, "an unknown death time is passed on as -1");
    Require(!Vetoed(kOther, 10), "other bots release as before");
    int const before = g_calls;
    Require(!Vetoed(nullptr, 10) && g_calls == before, "no bot: no veto, the predicate is not even asked");
    Register(&KeepNone);
    Require(!Vetoed(kKept, 10), "the last registration wins");
    Register(nullptr);
    Require(!Vetoed(kKept, 10), "nullptr clears the predicate");

    std::cout << "release_veto_tests passed\n";
    return 0;
}
