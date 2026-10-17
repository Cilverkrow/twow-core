#pragma once

// twow-repo#596 D (AiPlayerbot.BotUpdateTraceTriggers, with AiPlayerbot.BotUpdateTrace): in block 1 (15.10.2026) the
// largest single item of [BotUpdateSlow] lines >= 50 ms was one bot's whole trigger pass (`TIME:values ... triggers
// 36419us`), without saying which trigger. With the switch the engine times each trigger check of the pass and, when the
// pass took at least SlowPhaseUs, names the three slowest in the TIME entry of the trail: `slow=name:us,name:us,name:us`.

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace ai::trigger_time
{
    constexpr std::uint64_t SlowPhaseUs = 20000;   // trigger pass >= 20 ms: name the slowest checks
    constexpr std::size_t TopCount = 3;

    class Top
    {
    public:
        void Clear()
        {
            for (auto& entry : entries)
                entry = { std::string(), 0 };
        }

        // Keeps the TopCount slowest; the name is copied only when the check enters the top.
        void Add(std::string const& name, std::uint64_t us)
        {
            if (!us || us <= entries[TopCount - 1].second)
                return;
            std::size_t at = TopCount - 1;
            while (at > 0 && us > entries[at - 1].second)
            {
                entries[at] = std::move(entries[at - 1]);
                --at;
            }
            entries[at] = { name, us };
        }

        std::string Format() const
        {
            std::string out;
            for (auto const& [name, us] : entries)
            {
                if (!us)
                    break;
                if (!out.empty())
                    out += ",";
                out += name + ":" + std::to_string(us);
            }
            return out;
        }

    private:
        std::array<std::pair<std::string, std::uint64_t>, TopCount> entries{};
    };
}
