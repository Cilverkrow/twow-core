#include "BotGroupPolicy.h"

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
}

int main()
{
    using namespace ai::bot_group;

    Require(ClampMaxBots(3) == 3, "the default of three bots is kept");
    Require(ClampMaxBots(1) == 2, "a bot-bot group is at least two bots");
    Require(ClampMaxBots(-4) == 2, "a negative value falls back to the minimum");
    Require(ClampMaxBots(9) == 5, "a party holds at most five");

    Require(Classify(true, 4, 20, 3, 3) == Verdict::PlayerLed, "player groups keep today's rules, whatever their shape");
    Require(Classify(false, 3, 3, 3, 3) == Verdict::Fits, "three bots within the window fit");
    Require(Classify(false, 2, 0, 3, 3) == Verdict::Fits, "two bots of one level fit");
    Require(Classify(false, 4, 0, 3, 3) == Verdict::TooManyBots, "a fourth bot is one too many");
    Require(Classify(false, 3, 4, 3, 3) == Verdict::LevelWindow, "a spread above the window is incoherent (#324)");
    Require(Classify(false, 4, 12, 3, 3) == Verdict::TooManyBots, "size is reported before the level spread");

    Require(std::strcmp(VerdictName(Verdict::PlayerLed), "player_led") == 0, "log code player_led");
    Require(std::strcmp(VerdictName(Verdict::Fits), "fits") == 0, "log code fits");
    Require(std::strcmp(VerdictName(Verdict::TooManyBots), "too_many_bots") == 0, "log code too_many_bots");
    Require(std::strcmp(VerdictName(Verdict::LevelWindow), "level_window") == 0, "log code level_window");
    return 0;
}
