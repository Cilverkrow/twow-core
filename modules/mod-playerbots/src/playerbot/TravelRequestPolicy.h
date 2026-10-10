#pragma once

#include <string>

// twow-repo#541 (audit A18, AiPlayerbot.Perf.TravelRequestGate, default 0): the travel request triggers
// of the travel strategy read their condition value only while the travel target is neither being
// prepared nor active. In both states RequestTravelTargetAction::isUseful rejects every request
// action, so the condition value (need travel purpose / should travel named / rpg quest) was
// computed for nothing. Pure string and bool logic, tested in t/travel_request_policy_tests.cpp.
namespace ai::travel_request
{
    // "val::<condition>" -> "travel request::<condition>" when gated. The part after the first
    // "::" (the qualifier) stays the same, so the trigger name, the event source and the travel
    // condition stored by the request action are unchanged. Gate off or no "val::" prefix:
    // returned as is.
    inline std::string TriggerName(std::string const& valueTrigger, bool gate)
    {
        if (!gate || valueTrigger.compare(0, 5, "val::") != 0)
            return valueTrigger;
        return "travel request::" + valueTrigger.substr(5);
    }

    // The strategy's request actions ("request travel target::<purpose>",
    // "request named travel target::<name>", "request quest travel target").
    inline bool IsRequestAction(std::string const& action)
    {
        return action.compare(0, 8, "request ") == 0;
    }

    // The gate: the same two states, in the same order, as RequestTravelTargetAction::isUseful.
    inline bool MayCheck(bool prepare, bool targetActive)
    {
        return !prepare && !targetActive;
    }
}
