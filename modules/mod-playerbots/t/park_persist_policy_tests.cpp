// twow-repo#551 (AiPlayerbot.Park.Persist): stored target round trip, bounded restore retries.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "ParkPolicy.h"

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
    using namespace ai::park;

    // The park target survives the store: "" (inn choice) <-> "inn"; "here" and tele names unchanged.
    Require(std::string(PersistData("")) == "inn" && std::string(PersistData(nullptr)) == "inn", "inn choice stored as inn");
    Require(std::string(RestoreTarget(PersistData(""))).empty(), "inn restored as the normal inn choice");
    Require(std::string(RestoreTarget(PersistData("here"))) == "here", "here round trip");
    Require(std::string(RestoreTarget(PersistData("Goldshire"))) == "Goldshire", "tele name round trip");
    Require(std::string(RestoreTarget(nullptr)).empty(), "missing data: inn choice");

    // Busy right after login: retried, but bounded; other refusals are final.
    Require(RestoreRetries("busy", 1) && RestoreRetries("no_ai", 1), "busy / no ai: retry");
    Require(!RestoreRetries("busy", RestoreAttempts), "negative: no endless retry");
    Require(!RestoreRetries("no_spot", 1) && !RestoreRetries("already_parked", 1) && !RestoreRetries(nullptr, 1),
        "no spot / already parked: final");
    Require(RestoreIntervalSeconds * RestoreAttempts <= 600, "gives up within ten minutes");
    Require(PersistValidSeconds >= 7u * 24 * 3600, "row outlives a week of restarts");

    std::cout << "park_persist_policy_tests passed\n";
    return 0;
}
