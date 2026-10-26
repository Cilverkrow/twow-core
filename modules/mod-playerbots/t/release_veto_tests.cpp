#include "ReleaseVeto.h"

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

bool KeepOne(Player* bot)
{
    ++g_calls;
    return bot == kKept;
}

bool KeepNone(Player*)
{
    return false;
}
}

int main()
{
    using namespace ai::release_veto;

    // twow-repo#629 G1 (26.10.2026): the stock auto release asks the registered predicate; without one it releases as before.
    Require(!Vetoed(kKept), "no predicate: never vetoed (stock)");
    Register(&KeepOne);
    Require(Vetoed(kKept), "the predicate keeps its bot a corpse");
    Require(!Vetoed(kOther), "other bots release as before");
    int const before = g_calls;
    Require(!Vetoed(nullptr) && g_calls == before, "no bot: no veto, the predicate is not even asked");
    Register(&KeepNone);
    Require(!Vetoed(kKept), "the last registration wins");
    Register(nullptr);
    Require(!Vetoed(kKept), "nullptr clears the predicate");

    std::cout << "release_veto_tests passed\n";
    return 0;
}
