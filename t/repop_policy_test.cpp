// twow-repo#575 F9-2: a revive clears a graveyard repop that is still pending (src/game/Objects/RepopPolicy.h).
#include "../src/game/Objects/RepopPolicy.h"

#include <iostream>

namespace
{
    int failures = 0;

    void Require(bool ok, char const* label)
    {
        if (!ok)
        {
            std::cerr << "FAILED: " << label << "\n";
            ++failures;
        }
    }
}

int main()
{
    bool pending = true;
    Require(repop::ClearStale(pending), "a pending repop at the revive is reported as stale");
    Require(!pending, "the stale repop is cleared, Player::Update no longer moves the living player");
    Require(!repop::ClearStale(pending), "a second revive finds nothing pending");
    Require(!pending, "nothing pending stays nothing pending");

    pending = false;
    Require(!repop::ClearStale(pending) && !pending, "a revive without a release (no repop scheduled) changes nothing");

    if (failures)
        return 1;
    std::cout << "repop_policy_test: ok\n";
    return 0;
}
