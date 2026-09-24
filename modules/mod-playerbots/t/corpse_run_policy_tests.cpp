#include "CorpseRunPolicy.h"

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
    using namespace ai::corpse_run;

    Require(Classify(true, false, false) == State::Alive, "living bot is alive");
    Require(Classify(true, true, false) == State::Alive, "alive wins over a stale corpse");
    Require(Classify(false, false, false) == State::DeadUnreleased,
        "dead bot without corpse object has not released (#277: never 'I am not dead')");
    Require(Classify(false, true, true) == State::GhostWithCorpse, "released ghost runs to its corpse");
    Require(Classify(false, true, false) == State::GhostWithCorpse, "corpse present means released");
    Require(Classify(false, false, true) == State::GhostWithoutCorpse, "ghost without corpse is reported, not run");
    return 0;
}
