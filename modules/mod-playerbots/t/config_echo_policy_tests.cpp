#include "ConfigEchoPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

    // FormatLines (OB-00 29.10.2026: the log path cut the single line at about 4096): numbered parts of at most kMaxLine.
    Require(kMaxLine == 3500, "the cap is 3500, under the log path's 4096");
    {
        std::vector<std::string> const one = FormatLines({{"ZoneExit.TurnInNearRadius", Value(60.0f)}});
        Require(one.size() == 1 && one[0] == "[ConfigEcho] AiPlayerbot.ZoneExit.TurnInNearRadius=60.0 part=1/1",
            "one part: the line plus part=1/1");
        std::vector<std::string> const none = FormatLines({});
        Require(none.size() == 1 && none[0] == "[ConfigEcho] part=1/1", "no entries: the tag plus part=1/1");
    }

    // The real block's shape at scale: 600 sorted keys (5x the 118 of today), values up to the uint32 maximum.
    std::vector<std::string> keys;
    for (int i = 0; i < 600; ++i)
    {
        char key[64];
        std::snprintf(key, sizeof(key), "Section%03d.SomeLongerSwitchName.Detail%03d", i / 7, i);
        keys.push_back(key);
    }
    std::vector<Entry> many;
    for (std::size_t i = 0; i < keys.size(); ++i)
        many.push_back({keys[i].c_str(), i % 3 == 0 ? Value(std::uint32_t(4294967295u)) : i % 3 == 1 ? Value(true) : Value(1234.5f)});
    std::vector<std::string> const parts = FormatLines(many);
    Require(parts.size() > 1, "600 entries need more than one part");
    std::string joined = "[ConfigEcho]";
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        std::string const& p = parts[i];
        Require(p.size() <= kMaxLine, "no part over kMaxLine");
        Require(p.rfind("[ConfigEcho] AiPlayerbot.", 0) == 0, "every part starts with the same tag and an entry");
        std::string const suffix = " part=" + std::to_string(i + 1) + "/" + std::to_string(parts.size());
        Require(p.size() > suffix.size() && p.compare(p.size() - suffix.size(), suffix.size(), suffix) == 0,
            "part=<i>/<n> is the last token");
        Require(p.find('\n') == std::string::npos && p.find("  ") == std::string::npos, "one line, single spaces");
        joined += p.substr(sizeof("[ConfigEcho]") - 1, p.size() - (sizeof("[ConfigEcho]") - 1) - suffix.size());
    }
    // every key whole in exactly one part, exactly once, and the parts glued back are the single line (order kept)
    Require(joined == FormatLine(many), "the parts in order are the single line");
    for (std::size_t i = 0; i < many.size(); ++i)
    {
        std::string const token = " AiPlayerbot." + keys[i] + "=" + many[i].second + " ";
        int in = 0;
        for (std::string const& p : parts)
            for (std::size_t at = p.find(token); at != std::string::npos; at = p.find(token, at + 1))
                ++in;
        Require(in == 1, "each key whole in exactly one part, once");
    }
    // greedy: a part ends only when the next entry would push it over the cap
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        std::size_t const nextKey = parts[i + 1].find(" AiPlayerbot.");
        std::size_t const nextToken = parts[i + 1].find(' ', nextKey + 1) - nextKey;
        std::size_t const body = parts[i].rfind(" part=");
        Require(body + nextToken + sizeof(" part=9999/9999") - 1 > kMaxLine, "parts filled before a new one");
    }

    // a cap below one entry: the entry is never split, it stands alone in its part
    {
        std::vector<std::string> const tiny = FormatLines({{"Alpha.Key", Value(true)}, {"Beta.Key", Value(false)}}, 20);
        Require(tiny.size() == 2 && tiny[0] == "[ConfigEcho] AiPlayerbot.Alpha.Key=1 part=1/2"
            && tiny[1] == "[ConfigEcho] AiPlayerbot.Beta.Key=0 part=2/2", "an oversize entry whole, alone in its part");
    }

    std::cout << "config_echo_policy_tests passed\n";
    return 0;
}
