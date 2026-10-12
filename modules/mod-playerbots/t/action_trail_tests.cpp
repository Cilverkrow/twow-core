// twow-repo#541 (audit A11): ai::action_trail::KeepNewest, the last-action trail trim of
// Engine::LogAction behind AiPlayerbot.BotUpdateTraceTailFix. Pure string logic, no game types.
#include "ActionTrail.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

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

bool EndsWith(std::string const& s, std::string const& tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The pre-A11 trim, for contrast (Engine::LogAction with the switch off).
void OldTrim(std::string& t)
{
    if (t.size() > 512)
    {
        t = t.substr(512);
        std::size_t pos = t.find("|");
        t = (pos == std::string::npos ? "" : t.substr(pos));
    }
}
}

int main()
{
    using ai::action_trail::KeepNewest;

    std::string const shortTrail = "|--- AI Tick ---|T:val|A:x - OK";
    std::string copy = shortTrail;
    KeepNewest(copy, 512);
    Require(copy == shortTrail, "trail at or under the limit unchanged");

    // Grow like Engine::LogAction: append "|" + entry, trim after each append.
    std::string trail, full, old;
    int oldEmptied = 0;
    for (int i = 0; i < 400; ++i)
    {
        std::string const entry = "|A:action number " + std::to_string(i) + (i % 3 ? " - OK" : " - FAILED");
        trail += entry;
        full += entry;
        old += entry;
        KeepNewest(trail, 512);
        OldTrim(old);
        if (old.empty())
            ++oldEmptied;
        Require(trail.size() <= 512, "never above the limit");
        Require(!trail.empty() && trail[0] == '|', "starts at an entry");
        Require(EndsWith(trail, entry), "newest entry kept");
        Require(EndsWith(full, trail), "a suffix of everything logged");
        if (full.size() > 512)
            Require(trail.size() > 512 - 40, "window stays full (entries < 40 chars), no collapse");
    }
    // The old trim drops the first 512 characters, so it empties the trail now and then
    // (18 times for this input) and otherwise keeps a short fragment that regrows.
    Require(oldEmptied > 0, "contrast: the old trim empties the trail");

    // Exact boundary: '|' is the first character of the window -> 512 kept.
    std::string edge = "x|" + std::string(511, 'a');
    KeepNewest(edge, 512);
    Require(edge.size() == 512 && edge[0] == '|', "window starting at '|' kept whole");

    // One entry longer than the window, no '|' inside it -> empty (old fallback).
    std::string huge = "|" + std::string(600, 'z');
    KeepNewest(huge, 512);
    Require(huge.empty(), "no entry start in the window -> empty");

    std::cout << "action_trail tests passed\n";
    return 0;
}
