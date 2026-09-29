#include "DeathSeriesPolicy.h"

#include <cstdlib>
#include <iostream>

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
    using namespace ai::death_series;

    // A) Four deaths anywhere within 30 minutes: cautious for 30 minutes.
    Series series;
    Require(!series.Record(1000), "one death");
    Require(!series.Record(1300), "two");
    Require(!series.Record(1600), "three");
    Require(series.Record(1900), "the fourth within 30 min starts the cautious mode");
    Require(series.Cautious(1900 + 1799) && !series.Cautious(1900 + 1800), "30 minutes cautious");
    Require(!series.Record(2000), "no second start while cautious");

    Series spread;
    Require(!spread.Record(0) && !spread.Record(1000) && !spread.Record(2000), "spread deaths");
    Require(!spread.Record(3000), "four deaths over 50 min: no series");
    for (uint32_t i = 0; i < 20; ++i)
        spread.Record(10000 + i);
    Require(spread.deaths.size() <= MaxEntries, "bounded");

    Require(RouteMargin(false) == 5 && RouteMargin(true) == 0, "route check: zone above the bot's level while cautious");
    Require(GrindMargin(false, 4) == 4 && GrindMargin(true, 4) == 0 && GrindMargin(true, -1) == -1, "no grinding above the level");

    // B) Two deaths without a killer within 30 minutes suppress the purpose.
    Require(IsEnvironmentalDeath(false, 0), "no target, no attackers: environmental");
    Require(!IsEnvironmentalDeath(true, 0) && !IsEnvironmentalDeath(false, 1), "a killer: not environmental");

    uint32_t const fishing = 1u << 17;
    uint32_t const herbs = 1u << 16;
    PurposeSuppression purposes;
    Require(!purposes.RecordEnvironmentalDeath(fishing, 100), "first fishing death");
    Require(!purposes.Suppressed(fishing, 100), "not yet suppressed");
    Require(purposes.RecordEnvironmentalDeath(fishing, 700), "second within 30 min: fishing suppressed");
    Require(purposes.Suppressed(fishing, 700 + 3599) && !purposes.Suppressed(fishing, 700 + 3600), "for 60 minutes");
    Require(!purposes.Suppressed(herbs, 800), "other purposes stay");
    Require(!purposes.Suppressed(0, 800), "no purpose: never suppressed");
    Require(!purposes.RecordEnvironmentalDeath(herbs, 0) && !purposes.RecordEnvironmentalDeath(herbs, 1800),
            "two herb deaths 30 min apart: no suppression");

    std::cout << "death_series_policy_tests passed\n";
    return 0;
}
