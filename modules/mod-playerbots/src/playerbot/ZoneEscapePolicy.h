#pragma once

#include "HomeBindPolicy.h"

#include <cstdint>

namespace ai::zone_escape
{
// #307: after the train-4 deploy two level-11 roster bots stood in Eastern
// Plaguelands (area level 58), carried there in an earlier train. They revived
// at the local spirit healer and died again and again: #129 only keeps a bot
// from binding or hearthing *into* such a zone, nothing got it *out*. A roster
// bot on its own in a zone clearly above its level now leaves it: hearthstone
// first (the home bind lies in the start area since the reset), otherwise a new
// level-appropriate travel target. No other teleports; a cooldown stops loops.
enum class Step : std::uint8_t
{
    None,
    Hearth,
    Travel,
};

struct Facts
{
    bool enabled = false;
    bool rosterOnItsOwn = false;
    bool alive = false;
    bool inInstanceOrBattleground = false;
    bool inRestArea = false;       // city or inn: trainers, guards (#306 visits)
    bool due = false;              // cooldown since the last escape attempt is over
    bool hearthUsable = false;     // hearthstone ready and its bind zone suits the bot
    std::uint32_t areaLevel = 0;
    std::uint32_t botLevel = 0;
};

struct Decision
{
    Step step = Step::None;
    char const* reason = "not_applicable";
};

inline Decision Decide(Facts const& facts)
{
    if (!facts.enabled || !facts.rosterOnItsOwn || !facts.alive || facts.inInstanceOrBattleground)
        return {};
    if (!homebind::IsZoneClearlyAboveLevel(facts.areaLevel, facts.botLevel))
        return { Step::None, "zone_ok" };
    if (facts.inRestArea)
        return { Step::None, "rest_area" };
    if (!facts.due)
        return { Step::None, "cooldown" };
    if (facts.hearthUsable)
        return { Step::Hearth, "zone_above_level" };
    return { Step::Travel, "no_hearthstone" };
}
}
