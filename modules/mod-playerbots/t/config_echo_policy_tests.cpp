#include "ConfigEchoPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

// twow-repo#601 R01-A/B (twow-repo#615): the [ConfigEcho] boot line format the arms' effect checks grep.
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

using namespace ai::config_echo;
}

int main()
{
    Require(Value(true) == "1" && Value(false) == "0", "bool as 1 / 0");
    Require(Value(std::uint32_t(0)) == "0" && Value(std::uint32_t(15)) == "15" && Value(std::uint32_t(4294967295u)) == "4294967295",
        "uint32 as decimal");
    Require(Value(60.0f) == "60.0" && Value(0.0f) == "0.0" && Value(12.26f) == "12.3" && Value(12.24f) == "12.2", "float with one decimal");

    Require(FormatLine({}) == "[ConfigEcho]", "no entries: only the tag");
    std::string const line = FormatLine({
        {"QuestFirstProgression.GrindMode", Value(std::uint32_t(1))},
        {"QuestFeasibility.Blocklist.count", Value(std::uint32_t(87))},
        {"ZoneExit.TurnInNearRadius", Value(60.0f)},
        {"QuestFirstProgression.FailureClasses", Value(false)},
    });
    Require(line == "[ConfigEcho] AiPlayerbot.QuestFirstProgression.GrindMode=1 AiPlayerbot.QuestFeasibility.Blocklist.count=87"
        " AiPlayerbot.ZoneExit.TurnInNearRadius=60.0 AiPlayerbot.QuestFirstProgression.FailureClasses=0",
        "entries in order, one space each, key=value");
    // a proof regex " <key>=<v>( |$)" must not match a longer key with the same prefix
    Require(line.find(" AiPlayerbot.QuestFirstProgression.GrindMode=1 ") != std::string::npos, "token bounded by spaces");
    Require(line.back() == '0' && line.find('\n') == std::string::npos, "single line, no trailing space");

    std::cout << "config_echo_policy_tests passed\n";
    return 0;
}
