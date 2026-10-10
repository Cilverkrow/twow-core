#pragma once

#include <cstddef>
#include <string>

namespace ai::action_trail
{
// twow-repo#541 (audit A11, AiPlayerbot.BotUpdateTraceTailFix): Engine::lastAction is "|entry|entry|...".
// Keep the newest `limit` characters, cut at the first entry start ('|') inside them, in place
// (erase = memmove, no allocation). No '|' inside the window (a single entry longer than limit - 1)
// -> empty, the same fallback as the old trim. The old trim dropped the first 512 characters and kept
// only what lay beyond them, so the trail collapsed to a short fragment or to "".
inline void KeepNewest(std::string& trail, std::size_t limit)
{
    if (trail.size() <= limit)
        return;
    std::size_t const pos = trail.find('|', trail.size() - limit);
    if (pos == std::string::npos)
        trail.clear();
    else
        trail.erase(0, pos);
}
}
