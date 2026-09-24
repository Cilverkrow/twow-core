#include "MasterWaitPolicy.h"

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
}

int main()
{
    using namespace ai::master_wait;

    Require(LimitFor(false, 180, 600) == 180, "open world waits three minutes");
    Require(LimitFor(true, 180, 600) == 600, "dungeons and raids wait ten minutes");
    Require(!IsExpired(300, LimitFor(true, 180, 600)), "a dungeon death at five minutes still waits");
    Require(IsExpired(300, LimitFor(false, 180, 600)), "an open-world death at five minutes no longer waits");

    Require(!IsExpired(179, 180), "still waiting for the master before three minutes");
    Require(IsExpired(180, 180), "three minutes end the wait");
    Require(IsExpired(3600, 180), "a long wait is expired");
    Require(!IsExpired(100000, 0), "0 keeps the old unbounded wait");

    Require(SecondsSinceDeath(1000, 820, 900) == 180, "death time wins over the later ghost time");
    Require(SecondsSinceDeath(1000, 0, 900) == 100, "ghost time is the fallback when the death was not observed");
    Require(SecondsSinceDeath(1000, 0, 0) == 0, "nothing observed never counts as expired");
    Require(SecondsSinceDeath(1000, 2000, 0) == 0, "clock skew never counts as expired");
    Require(!IsExpired(SecondsSinceDeath(1000, 0, 0), 180), "unknown death time keeps waiting");
    return 0;
}
