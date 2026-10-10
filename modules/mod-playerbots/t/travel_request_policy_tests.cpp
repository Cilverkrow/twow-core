#include "TravelRequestPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

// twow-repo#541 (audit A18): pure policy of the travel request gate (AiPlayerbot.Perf.TravelRequestGate).
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

// The part after the first "::" - what NamedObjectFactory::Create passes to Qualify().
std::string Qualifier(std::string const& name)
{
    std::string::size_type const pos = name.find("::");
    return pos == std::string::npos ? std::string() : name.substr(pos + 2);
}
}

int main()
{
    using namespace ai::travel_request;

    // (a) Off keeps every name.
    Require(TriggerName("val::need travel purpose::256", false) == "val::need travel purpose::256", "off keeps purpose trigger");
    Require(TriggerName("val::and::{has strategy::rpg quest,should get money}", false) == "val::and::{has strategy::rpg quest,should get money}", "off keeps and trigger");

    // (b) On swaps only the prefix; the qualifier (event source, travel condition) is identical.
    for (char const* original : {
        "val::need travel purpose::256",
        "val::should travel named::trainer trade",
        "val::and::{should get money,can get mail,should get mail}",
        "val::has strategy::rpg quest",
        "val::and::{has strategy::rpg quest,has focus travel target}",
        "val::should get money" })
    {
        std::string const gated = TriggerName(original, true);
        Require(gated.compare(0, 16, "travel request::") == 0, "on maps to travel request::");
        Require(Qualifier(gated) == Qualifier(original), "on keeps the qualifier");
        Require(!Qualifier(gated).empty(), "qualifier not empty");
    }

    // (c) Only val:: names are mapped.
    Require(TriggerName("has nearby quest taker", true) == "has nearby quest taker", "plain trigger unchanged");
    Require(TriggerName("val", true) == "val", "bare val unchanged");
    Require(TriggerName("", true).empty(), "empty unchanged");

    // (d) Only the request actions are gated.
    Require(IsRequestAction("request travel target::256"), "request travel target");
    Require(IsRequestAction("request named travel target::trainer class"), "request named travel target");
    Require(IsRequestAction("request quest travel target"), "request quest travel target");
    Require(!IsRequestAction("refresh travel target"), "refresh not gated");
    Require(!IsRequestAction("choose group travel target"), "choose group not gated");
    Require(!IsRequestAction("choose travel target"), "choose not gated");
    Require(!IsRequestAction("reset travel target"), "reset not gated");
    Require(!IsRequestAction("request"), "bare word not gated");

    // (e) MayCheck mirrors RequestTravelTargetAction::isUseful (prepare, then active).
    Require(MayCheck(false, false), "no target: check");
    Require(!MayCheck(true, false), "prepare: skip");
    Require(!MayCheck(false, true), "active: skip");
    Require(!MayCheck(true, true), "both: skip");

    std::cout << "travel_request_policy tests passed\n";
    return 0;
}
