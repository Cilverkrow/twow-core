#pragma once

// twow-repo#541 (owner 11.10.2026: "interessante Messzeile", relayed by OB-00; measurement only, switch
// AiPlayerbot.PacketTrace, default 0). Every packet the server sends to a session passes the module's
// CanPacketSend hook, after the core has fully built it. A bot session has no socket: the packet is either
// copied into the bot's inbox (the AI reacts to it) or dropped right there - built for nobody. Counted per
// receiver (bot packet read / bot packet ignored / real player), map bucket and opcode class, as count and
// bytes. Counting is thread-local and flushed in batches into shared atomics, so the hot path does not write
// one shared cache line from all map threads (the X4a lesson).

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ai::packet_trace
{
    enum Receiver { BotRead, BotIgnored, Real, Receivers };
    enum MapBucket { EasternKingdoms, Kalimdor, OtherMaps, MapBuckets };
    enum Class { Movement, Update, Spell, Combat, Chat, Other, Classes };

    inline MapBucket BucketOf(std::uint32_t mapId)
    {
        return mapId == 0 ? EasternKingdoms : mapId == 1 ? Kalimdor : OtherMaps;
    }

    inline bool StartsWith(char const* text, char const* prefix)
    {
        return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
    }

    // Class of an opcode by its core name (LookupOpcodeName), decided once per opcode.
    inline Class ClassifyName(char const* name)
    {
        if (!name)
            return Other;
        if (StartsWith(name, "SMSG_UPDATE_OBJECT") || StartsWith(name, "SMSG_COMPRESSED_UPDATE_OBJECT") ||
            StartsWith(name, "SMSG_DESTROY_OBJECT"))
            return Update;
        if (StartsWith(name, "MSG_MOVE_") || StartsWith(name, "SMSG_SPLINE_") || StartsWith(name, "SMSG_MONSTER_MOVE") ||
            StartsWith(name, "SMSG_FORCE_") || StartsWith(name, "SMSG_MOVE_"))
            return Movement;
        if (StartsWith(name, "SMSG_SPELL") || StartsWith(name, "SMSG_CAST") || StartsWith(name, "SMSG_PERIODICAURALOG") ||
            StartsWith(name, "SMSG_AURA") || StartsWith(name, "SMSG_SET_EXTRA_AURA"))
            return Spell;
        if (StartsWith(name, "SMSG_ATTACK") || StartsWith(name, "SMSG_AI_REACTION") ||
            StartsWith(name, "SMSG_ENVIRONMENTALDAMAGELOG") || StartsWith(name, "SMSG_PARTYKILLLOG"))
            return Combat;
        if (StartsWith(name, "SMSG_MESSAGECHAT") || StartsWith(name, "SMSG_EMOTE") || StartsWith(name, "SMSG_TEXT_EMOTE") ||
            StartsWith(name, "SMSG_CHANNEL"))
            return Chat;
        return Other;
    }

    struct Totals
    {
        std::atomic<std::uint64_t> count[Receivers][MapBuckets][Classes] = {};
        std::atomic<std::uint64_t> bytes[Receivers][MapBuckets][Classes] = {};
        std::atomic<std::uint64_t> inboxSampledNs{0};
        std::atomic<std::uint64_t> inboxSamples{0};
    };

    inline Totals& Global()
    {
        static Totals totals;
        return totals;
    }

    constexpr std::uint32_t FlushEvery = 1024;   // packets per thread between two flushes
    constexpr std::uint32_t TimeSampleEvery = 64; // one in this many bot packets has its inbox copy timed

    // Per map thread; added to Global() every FlushEvery packets (the minute line may lag by that much).
    struct Local
    {
        std::uint64_t count[Receivers][MapBuckets][Classes] = {};
        std::uint64_t bytes[Receivers][MapBuckets][Classes] = {};
        std::uint32_t pending = 0;
        std::uint32_t botPackets = 0;

        void Flush(Totals& totals)
        {
            for (int r = 0; r < Receivers; ++r)
                for (int m = 0; m < MapBuckets; ++m)
                    for (int c = 0; c < Classes; ++c)
                    {
                        if (count[r][m][c])
                            totals.count[r][m][c].fetch_add(count[r][m][c], std::memory_order_relaxed);
                        if (bytes[r][m][c])
                            totals.bytes[r][m][c].fetch_add(bytes[r][m][c], std::memory_order_relaxed);
                        count[r][m][c] = bytes[r][m][c] = 0;
                    }
            pending = 0;
        }

        void Add(Receiver receiver, MapBucket bucket, Class cls, std::uint64_t size, Totals& totals)
        {
            ++count[receiver][bucket][cls];
            bytes[receiver][bucket][cls] += size;
            if (++pending >= FlushEvery)
                Flush(totals);
        }

        // True for every TimeSampleEvery-th bot packet: that one's inbox copy is timed.
        bool SampleThisBotPacket()
        {
            return (++botPackets % TimeSampleEvery) == 0;
        }
    };

    inline Local& ThreadLocal()
    {
        thread_local Local local;
        return local;
    }
}
