#include "BotDialoguePolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; \
    ++failures; \
} } while (false)

void TestNarrowLiveRoutes()
{
    using ai::BotDialogueRoute;
    using ai::IsBotDialogueRouteEligible;

    CHECK(IsBotDialogueRouteEligible(BotDialogueRoute::Whisper, true, false));
    CHECK(IsBotDialogueRouteEligible(BotDialogueRoute::Whisper, true, true));
    CHECK(IsBotDialogueRouteEligible(BotDialogueRoute::Party, true, true));
    CHECK(IsBotDialogueRouteEligible(BotDialogueRoute::Raid, true, true));

    CHECK(!IsBotDialogueRouteEligible(BotDialogueRoute::Party, true, false));
    CHECK(!IsBotDialogueRouteEligible(BotDialogueRoute::Raid, true, false));
    CHECK(!IsBotDialogueRouteEligible(BotDialogueRoute::Other, true, true));
    CHECK(!IsBotDialogueRouteEligible(BotDialogueRoute::Whisper, false, true));
    CHECK(!IsBotDialogueRouteEligible(BotDialogueRoute::Party, false, true));
}

void TestBoundedDialogueDelay()
{
    using ai::BotDialogueDelayMs;

    CHECK(BotDialogueDelayMs(20, 40, 0, 2000) == 800);
    CHECK(BotDialogueDelayMs(100, 40, 0, 2000) == 2000);
    CHECK(BotDialogueDelayMs(100, 40, 1500, 2000) == 2000);
    CHECK(BotDialogueDelayMs(20, 40, 900, 2000) == 0);
    CHECK(BotDialogueDelayMs(20, 40, 200, 2000) == 600);
    CHECK(BotDialogueDelayMs(100, 200, 0, 0) == 20000);
}
}

int main()
{
    TestNarrowLiveRoutes();
    TestBoundedDialogueDelay();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "BOT_DIALOGUE_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
