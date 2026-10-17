// twow-repo#597 (TICK-01): module parts of the tick diagnostics - the [BotUpdateSlow] tick/region suffix
// (AiPlayerbot.BotUpdateTraceTick) and the [WorldBots] hook times (AiPlayerbot.WorldBotsHookTrace).
#include "BotUpdateTrace.h"
#include "WorldBotsTracePolicy.h"

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
}

int main()
{
    // [BotUpdateSlow]: the suffix goes at the end of the description (= the end of the line).
    Require(ai::bot_update::TickRegionSuffix(123456, 0, 7) == " tick=123456 region=0.7", "tick/region suffix");
    Require(ai::bot_update::TickRegionSuffix(0, 1, 0) == " tick=0 region=1.0", "zero values kept");
    std::string const line = std::string("[BotUpdateSlow] us=80000 ") + "Bot map=0 combat=1 action=A:x - ok 5us" +
        ai::bot_update::TickRegionSuffix(42, 0, 3);
    std::string const tail = " action=A:x - ok 5us tick=42 region=0.3";
    Require(line.size() > tail.size() && line.substr(line.size() - tail.size()) == tail, "suffix after action=, at the end");

    // [WorldBots]: hook sums, suffix only with the switch, reset with the window.
    using namespace ai::world_bots;
    Window w;
    Require(w.HookSuffix(false).empty(), "off: no suffix (line byte-identical)");
    Require(w.HookSuffix(true) == " hook_ms=0 remote_ms=0 ahbot_ms=0 dungeonclear_ms=0", "empty window, on");
    w.AddHook(12000, 500, 2500);
    w.AddHook(8999, 499, 1500);
    w.AddDungeonClear(1999);
    w.AddDungeonClear(1);
    Require(w.hookUs == 20999 && w.remoteUs == 999 && w.ahbotUs == 4000 && w.dungeonClearUs == 2000, "hook parts summed");
    Require(w.HookSuffix(true) == " hook_ms=20 remote_ms=0 ahbot_ms=4 dungeonclear_ms=2", "ms rounded down, field order");
    Require(w.HookSuffix(false).empty(), "off with data: still no suffix");
    // The hook times do not count into the pass statistics of the #541 line.
    Require(w.passes == 0 && w.maxPassUs == 0 && w.PerMille() == 0, "hooks are not passes");
    w.Due(600);
    w.Reset(660);
    Require(w.hookUs == 0 && w.remoteUs == 0 && w.ahbotUs == 0 && w.dungeonClearUs == 0, "reset clears the hook sums");

    std::cout << "tick_diag_597_tests passed\n";
    return 0;
}
