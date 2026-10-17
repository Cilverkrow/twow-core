#include "AhBotGuardPolicy.h"

#include <cstdlib>
#include <cstring>
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

bool Is(char const* actual, char const* expected)
{
    return actual && expected && std::strcmp(actual, expected) == 0;
}
}

// twow-repo#597 (TICK-01): pure decision of AiPlayerbot.AhBot.RequireConfig. The integration (first statement of
// AhBot::Update, before the thread start and the mail query; the one [AhBotGuard] startup line; the switch default)
// is pinned by t/ahbot_guard_597_source_contract_tests.cmake.
int main()
{
    using namespace ahbot::guard;

    // Switch 0: never skips, whatever the config says (behaviour as before, also on the live realm without ahbot.conf).
    Require(!SkipUpdate(false, false, false), "switch 0, no ahbot.conf -> runs as before");
    Require(!SkipUpdate(false, true, false), "switch 0, AhBot.Enabled = 0 -> runs as before");
    Require(!SkipUpdate(false, false, true), "switch 0, inconsistent flags -> runs as before");
    Require(!SkipUpdate(false, true, true), "switch 0, loaded and enabled -> runs");

    // Switch 1: only a loaded AND enabled config lets Update run (fail closed).
    Require(SkipUpdate(true, false, false), "switch 1, no ahbot.conf (live) -> idle");
    Require(SkipUpdate(true, true, false), "switch 1, AhBot.Enabled = 0 -> idle");
    Require(SkipUpdate(true, false, true), "switch 1, enabled without a loaded config -> idle (fail closed)");
    Require(!SkipUpdate(true, true, true), "switch 1, loaded and enabled -> runs exactly as with 0");

    // update= token of the startup line follows the decision.
    Require(Is(UpdateMode(true, false, false), "idle"), "token idle without a config");
    Require(Is(UpdateMode(true, true, false), "idle"), "token idle when disabled");
    Require(Is(UpdateMode(true, true, true), "scheduled"), "token scheduled when loaded and enabled");
    Require(Is(UpdateMode(false, false, false), "scheduled"), "token scheduled with the switch off");

    std::cout << "ahbot_guard_597_policy tests passed\n";
    return 0;
}
