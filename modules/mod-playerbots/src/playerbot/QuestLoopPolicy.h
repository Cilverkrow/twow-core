#pragma once

// twow-repo#581 (QUEST-LOOP-01, owner 11.10.2026 via OB-00): bots looped on emptied quest camps. A quest objective
// destination goes inactive when no living target is in sight, the bot cools down for 60 s (respawn: 120-600 s) and
// picks the same camp again; nothing remembered the failure, and the #405 work-timeout memory is per square, so a bot
// rotating between a quest's targets never reached it.
//   QL-1 AiPlayerbot.QuestLoop.MaxFailures (0 = off): inactive drops and work timeouts of a quest's objectives count
//        per bot and QUEST; MaxFailures of them within WindowSeconds skip all objectives of that quest for
//        CooldownSeconds (turn-ins and givers are not affected).
//   QL-2 AiPlayerbot.QuestLoop.InactiveCooldownSeconds (0 = off): the cooldown after an inactive objective lasts at
//        least this long instead of the destination's 60 s.

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ai::quest_loop
{
    struct Record
    {
        std::uint8_t failures = 0;
        std::uint32_t windowStart = 0;
        std::uint32_t suppressUntil = 0;
    };

    inline bool IsSuppressed(Record const& record, std::uint32_t nowMs)
    {
        return record.suppressUntil && std::int32_t(record.suppressUntil - nowMs) > 0;
    }

    // Returns true when this failure suppresses the quest's objectives.
    inline bool RecordFailure(Record& record, std::uint32_t nowMs, std::uint32_t maxFailures, std::uint32_t windowMs,
        std::uint32_t cooldownMs)
    {
        if (!maxFailures)
            return false;
        if (IsSuppressed(record, nowMs))
            return false;
        if (record.suppressUntil || !record.failures || nowMs - record.windowStart >= windowMs)
            record = Record{ 0, nowMs, 0 };
        if (record.failures < 255)
            ++record.failures;
        if (record.failures < maxFailures)
            return false;
        record = Record{ 0, nowMs, nowMs + cooldownMs };
        if (!record.suppressUntil)
            record.suppressUntil = 1;   // a wrapped clock must not read as "never suppressed"
        return true;
    }

    // twow-repo#601 root cause of the cand-v466r standing (CLI-Deep 25.10.2026, AiPlayerbot.QuestLoop.InactiveAfterProgress): the
    // objective goes inactive as soon as no living target of its entry is in sight, also right after the bot killed the visible
    // ones - normal progress was booked as an emptied camp (QL-1 failure + QL-2 180 s cooldown; cohort B: 32 % of its time between
    // an inactive objective and the next quest target). With the switch an inactive objective whose count (kill / use credit or
    // quest items) grew since the count was taken is progress: no QL-1, no QL-2, a short cooldown. -1 = no count (not an
    // objective, quest not in the log): never progress, the rule stays as before.
    constexpr std::uint32_t ProgressCooldownMs = 1000;

    inline bool InactiveAfterProgress(std::int32_t countAtStart, std::int32_t countNow)
    {
        return countAtStart >= 0 && countNow > countAtStart;
    }

    // twow-repo#601 cr2-C gap analysis (CLI-Deep 26.10.2026, AiPlayerbot.QuestLoop.GiverTakerDone): a quest giver / taker destination
    // goes inactive as soon as its job is done - the giver offers nothing more once the quest is taken, the taker once it is turned in -
    // and that success sent the bot into the destination's 60 s COOLDOWN at the NPC (cohort B: 167 drops at 4 yd, median 96 s to the
    // next target). Done = giver (relation 0): the quest is in the log or rewarded; taker (relation 1): the quest is no longer in the
    // log. Anything else (not taken, still in the log) is no success: the COOLDOWN path as before.
    inline bool GiverTakerTripDone(std::uint8_t relation, bool questInLog, bool questRewarded)
    {
        return relation == 0 ? (questInLog || questRewarded) : !questInLog;
    }

    // QL-2: the cooldown after an inactive objective, never shorter than the destination's own.
    inline std::uint32_t InactiveCooldownMs(std::uint32_t destinationCooldownMs, std::uint32_t minSeconds)
    {
        return std::max(destinationCooldownMs, minSeconds * 1000u);
    }

    // twow-repo#601 cr2-C gap analysis (CLI-Deep 25.10.2026, AiPlayerbot.QuestLoop.InactiveReselect): 450 of 662 inactive objective
    // drops of cohort B had no new credit (an emptied camp) and sat out the whole QL-2 cooldown (median 212 s) in COOLDOWN - the next
    // quest search, once it ran, found another target within 1 s; a quarter went back to the same camp through the refresh. With the
    // switch the camp (quest, target entry) is remembered for the cooldown instead and the bot searches at once without it. A small
    // fixed ring per bot: a bot empties only a few camps per cooldown, the oldest mark gives way.
    constexpr std::size_t EmptyCampSlots = 4;

    struct EmptyCamp
    {
        std::uint32_t quest = 0;
        std::int32_t entry = 0;
        std::uint32_t untilMs = 0;   // 0 = free
    };

    struct EmptyCamps
    {
        EmptyCamp slots[EmptyCampSlots];
    };

    inline bool CampMarkLive(EmptyCamp const& camp, std::uint32_t nowMs)
    {
        return camp.untilMs && std::int32_t(camp.untilMs - nowMs) > 0;
    }

    // Time left on the mark of (quest, entry), 0 = not marked.
    inline std::uint32_t EmptyCampLeftMs(EmptyCamps const& camps, std::uint32_t quest, std::int32_t entry, std::uint32_t nowMs)
    {
        for (EmptyCamp const& camp : camps.slots)
            if (camp.quest == quest && camp.entry == entry && CampMarkLive(camp, nowMs))
                return camp.untilMs - nowMs;
        return 0;
    }

    // Marks (quest, entry) for durationMs: its own slot again, else a free or run-out one, else the one that runs out first.
    inline void MarkEmptyCamp(EmptyCamps& camps, std::uint32_t quest, std::int32_t entry, std::uint32_t nowMs, std::uint32_t durationMs)
    {
        EmptyCamp* slot = nullptr;
        for (EmptyCamp& camp : camps.slots)
            if (camp.quest == quest && camp.entry == entry)
                slot = &camp;
        for (EmptyCamp& camp : camps.slots)
            if (!slot && !CampMarkLive(camp, nowMs))
                slot = &camp;
        if (!slot)
        {
            slot = &camps.slots[0];
            for (EmptyCamp& camp : camps.slots)
                if (std::int32_t(camp.untilMs - slot->untilMs) < 0)
                    slot = &camp;
        }
        *slot = EmptyCamp{ quest, entry, nowMs + durationMs };
        if (!slot->untilMs)
            slot->untilMs = 1;   // a wrapped clock must not read as "free"
    }
}
