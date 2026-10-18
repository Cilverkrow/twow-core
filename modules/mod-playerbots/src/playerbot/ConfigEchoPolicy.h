#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// twow-repo#601 R01-A/B (owner rule 18.10.2026, twow-repo#615: every arm proves each A/B switch in the running process):
// one "[ConfigEcho] <Key>=<value> ..." line at the end of PlayerbotAIConfig::Initialize with the values the process parsed
// (after clamps and defaults). <Key> is the full config key (a list key echoes its parsed size as <Key>.count), <value>
// the parsed member, so an arm proves a switch with one regex like " AiPlayerbot.QuestFirstProgression.GrindMode=1( |$)".
// No key of its own, one line per start, no state. Pure (std only), so the unit test and the source contract pin the
// format without game headers.
namespace ai::config_echo
{
// Entry key without "AiPlayerbot." (so the config source keeps each quoted full key exactly once); printed with it.
using Entry = std::pair<char const*, std::string>;

inline std::string Value(bool v) { return v ? "1" : "0"; }
inline std::string Value(std::uint32_t v) { return std::to_string(v); }
inline std::string Value(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", double(v));
    return buf;
}

inline std::string FormatLine(std::vector<Entry> const& entries)
{
    std::string line = "[ConfigEcho]";
    for (Entry const& e : entries)
    {
        line += " AiPlayerbot.";
        line += e.first;
        line += '=';
        line += e.second;
    }
    return line;
}
}
