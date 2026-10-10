// twow-repo#541 (AiPlayerbot.PacketTrace): opcode classes by name, map buckets, thread-local counting with
// batched flush - nothing lost across threads once every thread has flushed.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "PacketTracePolicy.h"

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
    using namespace ai::packet_trace;

    Require(ClassifyName("SMSG_UPDATE_OBJECT") == Update && ClassifyName("SMSG_COMPRESSED_UPDATE_OBJECT") == Update &&
        ClassifyName("SMSG_DESTROY_OBJECT") == Update, "object updates");
    Require(ClassifyName("MSG_MOVE_HEARTBEAT") == Movement && ClassifyName("SMSG_MONSTER_MOVE") == Movement &&
        ClassifyName("SMSG_SPLINE_MOVE_SET_RUN_MODE") == Movement && ClassifyName("SMSG_FORCE_RUN_SPEED_CHANGE") == Movement,
        "movement");
    Require(ClassifyName("SMSG_SPELL_GO") == Spell && ClassifyName("SMSG_SPELLNONMELEEDAMAGELOG") == Spell &&
        ClassifyName("SMSG_PERIODICAURALOG") == Spell && ClassifyName("SMSG_CAST_RESULT") == Spell, "spells");
    Require(ClassifyName("SMSG_ATTACKERSTATEUPDATE") == Combat && ClassifyName("SMSG_AI_REACTION") == Combat, "combat");
    Require(ClassifyName("SMSG_MESSAGECHAT") == Chat && ClassifyName("SMSG_TEXT_EMOTE") == Chat && ClassifyName("SMSG_EMOTE") == Chat,
        "chat");
    Require(ClassifyName("SMSG_ITEM_PUSH_RESULT") == Other && ClassifyName(nullptr) == Other, "other");

    Require(BucketOf(0) == EasternKingdoms && BucketOf(1) == Kalimdor && BucketOf(33) == OtherMaps && BucketOf(489) == OtherMaps,
        "map buckets");

    // Local counts are flushed every FlushEvery packets; a final flush makes the totals exact.
    Totals totals;
    std::vector<std::thread> threads;
    for (int t = 0; t < 6; ++t)
        threads.emplace_back([&totals, t]()
        {
            Local local;
            for (int i = 0; i < 10000; ++i)
                local.Add(Receiver(i % Receivers), MapBucket(t % MapBuckets), Class(i % Classes), 10, totals);
            local.Flush(totals);
        });
    for (std::thread& t : threads)
        t.join();
    std::uint64_t count = 0, bytes = 0;
    for (int r = 0; r < Receivers; ++r)
        for (int m = 0; m < MapBuckets; ++m)
            for (int c = 0; c < Classes; ++c)
            {
                count += totals.count[r][m][c].load();
                bytes += totals.bytes[r][m][c].load();
            }
    Require(count == 60000 && bytes == 600000, "every packet counted once across threads");

    Local local;
    Totals partial;
    for (std::uint32_t i = 0; i < FlushEvery - 1; ++i)
        local.Add(BotIgnored, Kalimdor, Movement, 1, partial);
    Require(partial.count[BotIgnored][Kalimdor][Movement].load() == 0, "no shared write before the batch is full");
    local.Add(BotIgnored, Kalimdor, Movement, 1, partial);
    Require(partial.count[BotIgnored][Kalimdor][Movement].load() == FlushEvery, "batch flushed at FlushEvery");

    int sampled = 0;
    for (std::uint32_t i = 0; i < TimeSampleEvery * 10; ++i)
        sampled += local.SampleThisBotPacket() ? 1 : 0;
    Require(sampled == 10, "one timed inbox copy per TimeSampleEvery bot packets");

    std::cout << "packet_trace_policy_tests passed\n";
    return 0;
}
