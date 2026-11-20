#pragma once

#include "playerbot/PlannerPolicy.h"
#include "playerbot/PlannerShadowPolicy.h"

#include <cstddef>
#include <cstdint>

// A3 step 3 planner P-2b (A3-S3-PLANER-ENTWURF.md v0.3, frozen d6e51878; AiPlayerbot.Planner, default 0): the planner's state
// in the supervisor instance (Supervisor::plan_), and the hooks of the other files. Numbers and ids only; the game side is
// PlannerShadow.cpp (Supervisor::PlanTick, the snapshot, the candidates, the lines). The state lives and dies with the
// supervisor instance (release / reset: the planner starts again with ev=start, the re-take counters start again; OB-10 B2).
class PlayerbotAI;

namespace ai
{
class TravelDestination;
class WorldPosition;
}

namespace ai::planner
{
// The source of a position jump, set by the teleport sites (CLI-PATH R9-2) and read and cleared by PlanTick. A placeholder of
// ai::replan::RelocSource (ReplanPolicy.h, core#1088) with the same values, in its own namespace so both pairs can land in one
// pin; R9-2 switches the field to ai::replan::RelocSource. P-2b has no setter: every jump is "unknown".
enum class RelocSource : std::uint8_t
{
    Unknown = 0,
    Hearth = 1,
    HomeTeleport = 2,
    Rescue = 3,
    FarMoveArrival = 4,
    TeleportAck = 5,
};

inline char const* RelocSourceName(RelocSource s)
{
    switch (s)
    {
        case RelocSource::Unknown: return "unknown";
        case RelocSource::Hearth: return "hearth";
        case RelocSource::HomeTeleport: return "home_teleport";
        case RelocSource::Rescue: return "rescue";
        case RelocSource::FarMoveArrival: return "far_move_arrival";
        case RelocSource::TeleportAck: return "teleport_ack";
    }
    return "unknown";
}

// The planner's own reward note (OB-10 B2, OB-60 K1): the two reward sources of #1078 (PlayerbotAI::NoteQuestRewarded and the
// SMSG_QUESTGIVER_QUEST_COMPLETE handler) note the quest id here, on the bot's thread, with or without a quest book; PlanTick
// takes the note when the quest leaves the log (quest_gone + note = quest_reward). A full note drops the oldest id.
struct RewardNotes
{
    static constexpr std::size_t kSlots = 4;
    std::uint32_t ids[kSlots] = {};
    std::size_t n = 0;

    void Note(std::uint32_t questId)
    {
        if (!questId)
            return;
        for (std::size_t i = 0; i < n; ++i)
            if (ids[i] == questId)
                return;
        if (n == kSlots)
        {
            for (std::size_t i = 1; i < kSlots; ++i)
                ids[i - 1] = ids[i];
            --n;
        }
        ids[n++] = questId;
    }

    // True (and the note gone) when the quest was noted.
    bool Take(std::uint32_t questId)
    {
        for (std::size_t i = 0; i < n; ++i)
            if (ids[i] == questId)
            {
                for (std::size_t j = i + 1; j < n; ++j)
                    ids[j - 1] = ids[j];
                ids[--n] = 0;
                return true;
            }
        return false;
    }

    void Clear()
    {
        for (std::uint32_t& id : ids)
            id = 0;
        n = 0;
    }
};

// When a quest in the log turned complete (for the maximum holding time).
struct CompleteMark
{
    std::uint32_t questId = 0;
    std::uint32_t sinceMs = 0;
};

// Keeps one mark per complete quest of cur: a new complete quest gets nowMs, a quest no longer complete loses its mark.
inline void UpdateComplete(CompleteMark (&marks)[kQuestSlots], Snapshot const& cur, std::uint32_t nowMs)
{
    CompleteMark next[kQuestSlots];
    std::size_t n = 0;
    for (QuestSlot const& q : cur.quests)
    {
        if (!q.id || q.state != QuestState::Complete || n >= kQuestSlots)
            continue;
        std::uint32_t since = nowMs ? nowMs : 1u;
        for (CompleteMark const& m : marks)
            if (m.questId == q.id && m.sinceMs)
                since = m.sinceMs;
        next[n].questId = q.id;
        next[n].sinceMs = since;
        ++n;
    }
    for (std::size_t i = 0; i < kQuestSlots; ++i)
        marks[i] = i < n ? next[i] : CompleteMark{};
}

inline std::uint32_t CompleteSince(CompleteMark const (&marks)[kQuestSlots], std::uint32_t questId)
{
    for (CompleteMark const& m : marks)
        if (m.questId == questId)
            return m.sinceMs;
    return 0;
}

// The current plan pick.
struct Pick
{
    bool has = false;
    Kind kind = Kind::QuestObjective;
    std::uint32_t questId = 0;
    std::uint32_t destKey = 0;    // DestKey of the destination (0 for a grind hub)
    std::uint32_t key = 0;        // TargetKey(kind, questId, destKey or zone)
    float value = 0.0f;
    std::uint32_t sinceMs = 0;    // when this pick was taken (the bind time runs from here)
    std::uint32_t atMs = 0;       // the last plan that confirmed it (fresh for the shadow comparison)
    std::uint32_t zone = 0;
    std::uint32_t respawnSec = 0; // objectives: the shortest respawn (W8)
    std::int32_t entry = 0;       // the destination's entry (W8 probe; < 0 a game object)
    std::uint32_t map = 0;        // the pick point (W8 probe)
    float x = 0.0f;
    float y = 0.0f;
    bool forced = false;          // P-3a: a forced pick (max_hold) may expire a target in WORK (B3); back_to_band marks its
                                  // pick with it but never reaches the expire (OB-10 V-1)
};

// The shadow comparison's last agree (for overwritten_shadow: a quest book lane writer replaces it inside the bind time).
struct AgreeMark
{
    std::uint32_t destKey = 0;
    std::uint32_t atMs = 0;
};

// The B1 proxy overwritten_shadow (section 14.1): the live target that agreed with the pick left inside the bind time while it was
// still open (TRAVEL or WORK) and the last writer is in the questbook lane. A finished trip followed by the ordinary re-select, a
// group or neutral writer, or the end of the bind time only ends the mark.
enum class AgreeEnd : std::uint8_t
{
    Keep = 0,
    Overwritten = 1,
    Ended = 2,
};

inline AgreeEnd CheckAgree(AgreeMark const& a, Snapshot const& prev, Snapshot const& cur, std::uint32_t nowMs, std::uint32_t minBindMs,
    bool questbookWriter)
{
    if (!a.atMs)
        return AgreeEnd::Keep;
    if (nowMs - a.atMs >= minBindMs)
        return AgreeEnd::Ended;
    if (!prev.valid || prev.travelDest != a.destKey || cur.travelDest == a.destKey)
        return AgreeEnd::Keep;
    bool const open = prev.travel == TravelPhase::Travel || prev.travel == TravelPhase::Work;
    return open && questbookWriter ? AgreeEnd::Overwritten : AgreeEnd::Ended;
}

// W8 wait: an unknown respawn (0, a source without stats) waits the full DeadWaitMaxSec, never 0 (a 0 wait would close the target at
// the next probe and count a zone_change_dead).
inline std::uint32_t DeadWaitFor(std::uint32_t respawnSec, std::uint32_t deadWaitMaxSec)
{
    return DeadWaitMs(respawnSec ? respawnSec : deadWaitMaxSec, deadWaitMaxSec);
}

struct PlanState
{
    Snapshot prev;
    std::uint32_t lastPlanMs = 0;
    CompleteMark complete[kQuestSlots];
    RepeatEntry repeat[kRepeatSlots];
    MemorySlot memory[kMemorySlots];
    Pick pick;
    AgreeMark agree;
    std::uint32_t lastBackMs = 0;      // the last back_to_band (loop guard)
    std::uint32_t deadWaitSinceMs = 0; // W8: the wait at the pick began (0 = no wait)
    std::uint32_t deadKey = 0;         // W8: the pick the wait belongs to
    std::uint32_t lastProbeMs = 0;     // W8: the last probe at the pick (at most one per kProbeEveryMs)
    std::uint32_t passiveTicks = 0;    // ticks in PASSIVE since the last flush (PlanMin passive_ticks)
    std::uint32_t passiveMinute = 0;   // the minute of passiveTicks
    // ---- P-3a (P3-ENTWURF-v0.2; AiPlayerbot.Planner = 2 with Planner.WriteCohort, default 0): the write path ----------------
    // Set only for a writing bot, except modeSinceMs and the fb_* ticks (Planner = 2). Planner 0 / 1 never touch them.
    std::uint32_t expirePending = 0;   // section 2.3: the pick key a denied expire waits for (0 = none)
    bool pendingSawWork = false;       // F8 (a): the pending expire saw WORK (an expire in TRAVEL after it is expire_after_work)
    std::uint32_t pendingMinute = 0;   // expire_pending: once per bot and minute
    std::uint32_t expiredKey = 0;      // rule 4: the pick (key and sinceMs) that expired once
    std::uint32_t expiredSinceMs = 0;
    std::uint32_t fallbackAtMs = 0;    // rule 5: the last fallback (the plan pick was not applied)
    std::uint32_t feedBudgetKey = 0;   // #416: the pick whose plan attempt ran out of time (no second attempt for it)
    std::uint32_t appliedGen = 0;      // overwritten*: the write generation of the applied plan target
    std::uint32_t appliedAtMs = 0;     // 0 = no applied target inside the bind time
    std::uint32_t appliedDest = 0;     // its DestKey, or the zone of an applied grind pick
    bool appliedGrindZone = false;     // appliedDest is a zone (grind compares the live target's zone, P3-1)
    std::uint32_t turninQuest = 0;     // turnin_unreached (B12): the applied turn-in's quest
    std::uint32_t turninAtMs = 0;
    std::uint32_t modeSinceMs = 0;     // section 2.6: the last mode change the snapshot saw (the dwell of Decide)
    std::uint32_t fbArbiterTicks = 0;  // fb_arbiter / fb_cohort ticks since the last flush, and their minute
    std::uint32_t fbCohortTicks = 0;
    std::uint32_t fbMinute = 0;
    std::uint32_t evNext = 0;          // events for the next tick (back_to_band: EvMode, section 2.5)
};
}

namespace ai::planner_shadow
{
// The shadow comparison after SetBestTarget (OB-10 B1), from ChooseTravelTargetAction::Execute on the bot's own thread:
// chosen = the accepted destination, or nullptr when the choice found nothing (then back_to_band_would by purpose, OB-10 B4).
// One config read when AiPlayerbot.Planner is 0.
void NoteChoice(PlayerbotAI* ai, char const* purpose, ai::TravelDestination const* chosen);

// One number per destination (purpose, entry, quest), the same in the snapshot, the pick and the comparison.
std::uint32_t DestKey(ai::TravelDestination const* dest);

// P-3a: the quest of a quest destination (0 otherwise) and the zone of a point (terrain lookup; 0 = unknown), the planner's rules
// for the write path (PlannerWrite.cpp) and the feed.
std::uint32_t QuestOfDest(ai::TravelDestination const* dest);
std::uint32_t ZoneOf(ai::WorldPosition const* point);

// P-3a: adds counts of the write path (PlannerWrite.cpp: the feed and its outcome) to the bot's cohort minute of [PlanMin].
void Count(std::uint32_t guidLow, ai::planner::PlanMinCounts const& c);
}
