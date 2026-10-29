#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// twow-repo#601 R01-A/B (owner rule 18.10.2026, twow-repo#615: every arm proves each A/B switch in the running process):
// "[ConfigEcho] <Key>=<value> ... part=<i>/<n>" lines at the end of PlayerbotAIConfig::Initialize with the values the
// process parsed (after clamps and defaults). <Key> is the full config key (a list key echoes its parsed size as
// <Key>.count), <value> the parsed member, so an arm proves a switch with one regex like
// " AiPlayerbot.QuestFirstProgression.GrindMode=1( |$)". No key of its own, one set of lines per Initialize (start and
// config reload), no state. Pure (std only), so the unit test and the source contract pin the format without game headers.
//
// OB-00 29.10.2026 (OB-30 finding): the log path cuts a line at about 4096 characters, and the single line had grown past
// it (from ece42753 every key after Rotation.InvalidTargetQuiet was gone), so arms that prove their keys went INVALID.
// FormatLines packs the entries into lines of at most kMaxLine characters: each starts with the same "[ConfigEcho]" tag,
// each entry stands whole in exactly one line, the sorted order runs on across the lines, and " part=<i>/<n>" is the last
// token, so the kits' per-key regex "[ConfigEcho].* <Key>=<value>( |$)" reads the new lines unchanged.
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

// The longest line FormatLines writes, well under the log path's cut at about 4096.
inline constexpr std::size_t kMaxLine = 3500;

// FormatLine over consecutive runs of the entries, each run as long as its line (with " part=<i>/<n>") stays within
// maxLine; " part=<i>/<n>" ends every line, also when one line holds all. An entry is never split: one whose own token
// does not fit stands alone in its line (no key comes near that; the unit test pins the case).
inline std::vector<std::string> FormatLines(std::vector<Entry> const& entries, std::size_t maxLine = kMaxLine)
{
    std::size_t constexpr kTag = sizeof("[ConfigEcho]") - 1;
    std::size_t constexpr kPartRoom = sizeof(" part=9999/9999") - 1;
    std::vector<std::vector<Entry>> runs(1);
    std::size_t len = kTag;
    for (Entry const& e : entries)
    {
        std::size_t const token = sizeof(" AiPlayerbot.") - 1 + std::strlen(e.first) + 1 + e.second.size();
        if (!runs.back().empty() && len + token + kPartRoom > maxLine)
        {
            runs.emplace_back();
            len = kTag;
        }
        runs.back().push_back(e);
        len += token;
    }
    std::vector<std::string> lines;
    lines.reserve(runs.size());
    for (std::size_t i = 0; i < runs.size(); ++i)
        lines.push_back(FormatLine(runs[i]) + " part=" + std::to_string(i + 1) + "/" + std::to_string(runs.size()));
    return lines;
}
}
