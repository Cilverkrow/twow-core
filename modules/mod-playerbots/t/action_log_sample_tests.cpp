// twow-repo#601 / #614: AiPlayerbot.ActionLog.Guids - the action log for a fixed sample of bots.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "ActionLogSamplePolicy.h"

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
    using namespace ai::action_log;

    std::vector<std::uint32_t> const none = ParseSample("");
    Require(none.empty(), "empty text: no sample");
    Require(Sampled(none, 1) && Sampled(none, 4242), "no sample: every bot, as before");

    std::vector<std::uint32_t> const sample = ParseSample(" 900, 12 ,77,12, x, 0,-5");
    Require(sample.size() == 3, "plain guids > 0 only, duplicates dropped");
    Require(sample[0] == 12 && sample[1] == 77 && sample[2] == 900, "sorted for the binary search");
    Require(Sampled(sample, 12) && Sampled(sample, 77) && Sampled(sample, 900), "listed bots are sampled");
    Require(!Sampled(sample, 13) && !Sampled(sample, 0) && !Sampled(sample, 4242), "other bots are not");

    std::cout << "action_log_sample_tests passed\n";
    return 0;
}
