#include "playerbot/playerbot.h"
#include "playerbot/PlannerShadow.h"
#include "playerbot/PlannerWrite.h"   // A3 step 3 planner P-3a: the write path (Planner = 2, WriteCohort)
#include "playerbot/Supervisor.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/WorldPosition.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/BandTable.h"
#include "playerbot/DangerMapConfig.h"
#include "playerbot/QuestBook.h"
#include "playerbot/RotCastPolicy.h"
#include "playerbot/ZoneKeepPolicy.h"
#include "Formulas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

// A3 step 3 planner P-2b (A3-S3-PLANER-ENTWURF.md v0.3, frozen d6e51878, Nachtrag 1; AiPlayerbot.Planner, default 0): the planner
// in the shadow. Supervisor::PlanTick is the last statement of Supervisor::Tick (the bot's map thread): it compares a small
// snapshot of the bot every tick (PlannerShadowPolicy.h Diff), plans on an event or after MaxIdleSec in QUEST or GRIND, and keeps
// its own pick with the hysteresis. It writes nothing of the bot - no travel target, no value, no strategy: [Plan] lines (own
// budget, 300 per minute), [PlanMin] per cohort and minute, and its own state (Supervisor::plan_). Planner = 2 is read as 1 until
// the pair P-3. The pure rules are in PlannerPolicy.h and PlannerShadowPolicy.h (unit tests t/planner_policy_tests.cpp,
// t/planner_shadow_policy_tests.cpp); the shadow comparison after SetBestTarget is ai::planner_shadow::NoteChoice (OB-10 B1).
// A3 R9 / R2 wiring pair R9-2 (twow-repo#575; R9-REPLAN-INTERFACE v5 9575095d, R9-PREREG C1/C2; DESIGN-R9-2-20261120.md D1-D13;
// AiPlayerbot.Replan / Replan.Shadow, default 0): ReplanTick feeds the bot's progress meter (ReplanPolicy.h) every planner tick,
// relocation first, and the supervisor answers as its sink (OnStall / OnRelocate at the end of this file). Lines and counters only:
// "[Replan]" (own budget, 300 per minute) and "[ReplanMin]" per cohort and minute. No verdict acts before P-3 (D1).
namespace
{
using namespace ai::planner;
using ai::supervisor::Lane;
using ai::supervisor::Mode;
using ai::supervisor::WriterId;
using ai::supervisor::PlanLine;
using ai::supervisor::FormatPlanLine;
using ai::replan::RelocSource;

constexpr std::size_t kMaxCandidates = 64;            // Planner.MaxCandidates is capped at this (PlayerbotAIConfig.cpp)
constexpr std::size_t kAlts = 3;
constexpr std::uint32_t kPlanLinesPerMinute = 300;    // section 7: an own budget, not the enforcement lines
constexpr std::uint32_t kGrindTag = 0x80000000u;      // a grind target is its zone with this bit (never a DestKey of a quest)
constexpr std::uint32_t kProbeEveryMs = 5000;         // W8: at most one grid probe per bot every 5 s
constexpr float kObjectiveWorkSec = 120.0f;           // the work of an objective until QB1/QB3 deliver one (section 2)
constexpr float kPickupWorkSec = 10.0f;               // the work of a pickup (talk and accept)
constexpr float kGrindWorkSec = 60.0f;                // the grind candidate: GrindKillsPerMin kills in one minute
constexpr float kArriveYards = 30.0f;                 // W8: the bot is at its pick (the probe runs from here)
constexpr float kCandidateRadius = 10000.0f;          // the destination search radius (as the travel choice)
constexpr float kPickupRadius = 1500.0f;              // givers: the fetch walks every giver of every map, so range first
constexpr std::size_t kMsSamples = 512;               // per cohort and minute, for ms_p50

// ---- the minute line and the line budget (one short file-local mutex, the pattern of SupervisorShadow.cpp) -----------------
std::mutex g_mutex;
std::uint32_t g_window = 0, g_used = 0;   // the [Plan] budget window
std::uint32_t g_minute = 0;
PlanMinCounts g_min[2];                   // [0] cohort A (even low guid), [1] cohort B
bool g_active[2] = {false, false};
std::uint32_t g_ms[2][kMsSamples];
std::size_t g_msN[2] = {0, 0};

struct MinuteOut
{
    bool due = false;
    std::uint32_t minute = 0;
    bool active[2] = {false, false};
    PlanMinCounts sums[2];
};

std::size_t CohortIndex(std::uint32_t guidLow)
{
    return ai::cohort::InB(guidLow) ? 1 : 0;
}

std::uint32_t MedianLocked(std::size_t i)
{
    if (!g_msN[i])
        return 0;
    std::uint32_t* const first = g_ms[i];
    std::uint32_t* const mid = first + g_msN[i] / 2;
    std::nth_element(first, mid, first + g_msN[i]);
    return *mid;
}

MinuteOut RollLocked(std::uint32_t nowSec)
{
    MinuteOut out;
    std::uint32_t const before = g_minute;
    if (!ai::supervisor::ArbiterMinuteRoll(g_minute, nowSec / 60))
        return out;
    out.due = true;
    out.minute = before;
    for (std::size_t i = 0; i < 2; ++i)
    {
        g_min[i].msP50 = MedianLocked(i);
        out.active[i] = g_active[i];
        out.sums[i] = g_min[i];
        g_min[i] = PlanMinCounts();
        g_active[i] = false;
        g_msN[i] = 0;
    }
    return out;
}

void EmitMinute(MinuteOut const& out)
{
    if (!out.due)
        return;
    for (std::size_t i = 0; i < 2; ++i)
    {
        if (!out.active[i])
            continue;
        // P-3a: the P-3a fields only at Planner = 2 (section 7.5: the key value, never the cohort); Planner 0 / 1 byte-identical.
        char buf[kPlanMinBuf];
        FormatPlanMin(buf, sizeof(buf), i ? 'B' : 'A', out.minute, out.sums[i], sPlayerbotAIConfig.planner == 2);
        sLog.outBasic("%s", buf);
    }
}

// Adds one tick's counts of a bot (and one plan time, msSample >= 0) to its cohort's minute; rolls the minute first.
void Flush(std::uint32_t guidLow, PlanMinCounts const& c, int msSample)
{
    std::size_t const i = CohortIndex(guidLow);
    MinuteOut out;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        out = RollLocked(std::uint32_t(time(nullptr)));
        Add(g_min[i], c);
        g_active[i] = true;
        if (msSample >= 0 && g_msN[i] < kMsSamples)
            g_ms[i][g_msN[i]++] = std::uint32_t(msSample);
    }
    EmitMinute(out);
}

// One [Plan] line from the planner's own budget; a refused line is plan_dropped.
bool TakePlanLine()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ai::rot_cast::BudgetTake(g_window, g_used, std::uint32_t(time(nullptr)), kPlanLinesPerMinute);
}

// ---- R9-2 (twow-repo#575, D11): [Replan] has its own budget and [ReplanMin] its own minute, under the same short mutex ----------
constexpr std::uint32_t kReplanLinesPerMinute = 300;
std::uint32_t g_rWindow = 0, g_rUsed = 0;   // the [Replan] budget window
std::uint32_t g_rMinute = 0;
ai::replan::Counters g_rMin[2];             // [0] cohort A, [1] cohort B
std::uint32_t g_rDropped[2] = {0, 0};       // [Replan] lines the budget refused (dropped=)
bool g_rShadow[2] = {false, false};         // the cohort's arm (one arm per cohort: Replan is a cohort mode)
bool g_rActive[2] = {false, false};

struct ReplanMinuteOut
{
    bool due = false;
    bool active[2] = {false, false};
    bool shadow[2] = {false, false};
    ai::replan::Counters sums[2];
    std::uint32_t dropped[2] = {0, 0};
};

ReplanMinuteOut RollReplanLocked(std::uint32_t nowSec)
{
    ReplanMinuteOut out;
    if (!ai::supervisor::ArbiterMinuteRoll(g_rMinute, nowSec / 60))
        return out;
    out.due = true;
    for (std::size_t i = 0; i < 2; ++i)
    {
        out.active[i] = g_rActive[i];
        out.shadow[i] = g_rShadow[i];
        out.sums[i] = g_rMin[i];
        out.dropped[i] = g_rDropped[i];
        g_rMin[i] = ai::replan::Counters();
        g_rDropped[i] = 0;
        g_rActive[i] = false;
    }
    return out;
}

void EmitReplanMinute(ReplanMinuteOut const& out)
{
    if (!out.due)
        return;
    for (std::size_t i = 0; i < 2; ++i)
    {
        if (!out.active[i])
            continue;
        char buf[512];
        ai::replan::FormatReplanMinLine(buf, sizeof(buf), i ? 'B' : 'A', out.shadow[i], out.sums[i], out.dropped[i]);
        sLog.outBasic("%s", buf);
    }
}

// Adds the counts a bot held since its last flush (meter counters, refused lines) to its cohort's minute; rolls the minute first.
void FlushReplan(std::uint32_t guidLow, ai::replan::Counters const& k, std::uint32_t dropped, bool shadow)
{
    std::size_t const i = CohortIndex(guidLow);
    ReplanMinuteOut out;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        out = RollReplanLocked(std::uint32_t(time(nullptr)));
        ai::replan::Add(g_rMin[i], k);
        g_rDropped[i] += dropped;
        g_rShadow[i] = shadow;
        g_rActive[i] = true;
    }
    EmitReplanMinute(out);
}

// One [Replan] line from the R9 budget.
bool TakeReplanLine()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ai::rot_cast::BudgetTake(g_rWindow, g_rUsed, std::uint32_t(time(nullptr)), kReplanLinesPerMinute);
}

// ---- small reads ------------------------------------------------------------------------------------------------------------
bool IsNullDest(ai::TravelDestination const* dest)
{
    return !dest || dynamic_cast<ai::NullTravelDestination const*>(dest) != nullptr;
}

std::uint32_t QuestOf(ai::TravelDestination const* dest)
{
    auto const* quest = dynamic_cast<ai::QuestTravelDestination const*>(dest);
    return quest ? quest->GetQuestId() : 0;
}

ai::TravelTarget* LiveTarget(PlayerbotAI* ai)
{
    AiObjectContext* const context = ai->GetAiObjectContext();
    return context ? context->GetValue<ai::TravelTarget*>("travel target")->Get() : nullptr;
}

TravelPhase PhaseOf(ai::TravelStatus status)
{
    switch (status)
    {
        case ai::TravelStatus::TRAVEL_STATUS_TRAVEL: return TravelPhase::Travel;
        case ai::TravelStatus::TRAVEL_STATUS_WORK: return TravelPhase::Work;
        case ai::TravelStatus::TRAVEL_STATUS_COOLDOWN: return TravelPhase::Cooldown;
        case ai::TravelStatus::TRAVEL_STATUS_EXPIRED: return TravelPhase::Expired;
        default: return TravelPhase::None;
    }
}

QuestState StateOfStatus(QuestStatus status)
{
    switch (status)
    {
        case QUEST_STATUS_INCOMPLETE: return QuestState::Incomplete;
        case QUEST_STATUS_COMPLETE: return QuestState::Complete;
        case QUEST_STATUS_FAILED: return QuestState::Failed;
        default: return QuestState::None;
    }
}

// The zone of a point (terrain lookup): the area's zone, or the area when it is a zone. 0 = unknown.
std::uint32_t ZoneOfPoint(WorldPosition const& pos)
{
    AreaEntry const* area = AreaEntry::GetByAreaFlagAndMap(pos.getAreaFlag(), pos.getMapId());
    if (!area)
        return 0;
    return area->ZoneId ? area->ZoneId : area->Id;
}

// The band table key of the bot (race up to L12, then faction and continent).
std::uint32_t BandKey(Player const* bot)
{
    return ai::band_table::KeyFor(bot->getRace(), bot->GetTeam() == ALLIANCE ? 'A' : 'H', bot->GetMapId(), bot->GetLevel());
}

// The floor of a zone for the bot's key: the smallest min_level of its rows with this zone (0 = no row).
std::uint32_t ZoneFloor(ai::band_table::Table const* table, std::uint32_t key, std::uint32_t zone)
{
    std::uint32_t floor = 0;
    if (!table || !key || !zone)
        return 0;
    for (ai::band_table::Row const& row : table->rows)
        if (row.keyIndex == key && row.zoneId == zone && (!floor || row.minLevel < floor))
            floor = row.minLevel;
    return floor;
}

// The next zone of the band (tier C): the rank 2 row of the bot's band in another zone, else the row that starts right above.
ai::band_table::Row const* NextRow(ai::band_table::Table const* table, std::uint32_t key, ai::band_table::Row const* band,
    std::uint32_t level)
{
    if (!table || !band)
        return nullptr;
    for (ai::band_table::Row const& row : table->rows)
        if (row.keyIndex == key && row.rank == 2 && row.minLevel <= level && level <= row.maxLevel && row.zoneId != band->zoneId)
            return &row;
    return ai::band_table::Find(*table, key, band->maxLevel + 1);
}

// The danger map gate of the travel choice (ChooseTravelTargetAction.cpp UsesQuestFirstProgression is file-local; same rule).
bool DangerGate(PlayerbotAI* ai, Player const* bot)
{
    return sPlayerbotAIConfig.dangerMapEnabled && sPlayerbotAIConfig.questFirstProgressionEnabled &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
}

// ---- canReward without a packet (section 2; OB-10 264a55af (c) (d), OB-60 20.11.) -----------------------------------------
// Player::CanRewardQuest without its two packets: SatisfyQuestChallenges and CanTakeQuest with msg = false send nothing (read at
// 8d76c1d1); the money check is copied (the core sends NOT_ENOUGH_MONEY there without a msg check). Not copied: the bag space for
// the reward (that one is a choice, not a fact of the quest).
bool PlannerCanReward(Player* bot, Quest const* quest)
{
    if (!bot->SatisfyQuestChallenges(quest, false))
        return false;
    if (quest->IsAutoComplete())
    {
        if (!bot->CanTakeQuest(quest, false, true))
            return false;
    }
    else if (bot->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_COMPLETE)
        return false;
    if (bot->GetQuestRewardStatus(quest->GetQuestId()))
        return false;
    if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER))
        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (quest->ReqItemCount[i] != 0 && bot->GetItemCount(quest->ReqItemId[i]) < quest->ReqItemCount[i])
                return false;
    if (quest->GetRewOrReqMoney() < 0 && bot->GetMoney() < uint32(-quest->GetRewOrReqMoney()))
        return false;
    return true;
}

// The taker takes exactly this quest (its involved relations), not the dialog status (a maximum over all its quests).
bool TakerTakes(std::int32_t entry, std::uint32_t questId)
{
    if (entry > 0)
    {
        auto const bounds = sObjectMgr.GetCreatureQuestInvolvedRelationsMapBounds(std::uint32_t(entry));
        for (auto it = bounds.first; it != bounds.second; ++it)
            if (it->second == questId)
                return true;
        return false;
    }
    if (entry < 0)
    {
        auto const bounds = sObjectMgr.GetGOQuestInvolvedRelationsMapBounds(std::uint32_t(-entry));
        for (auto it = bounds.first; it != bounds.second; ++it)
            if (it->second == questId)
                return true;
    }
    return false;
}

// ---- one candidate with what the line and the pick need ---------------------------------------------------------------------
struct Slot
{
    Candidate c;
    std::uint32_t key = 0;
    std::uint32_t zone = 0;
    std::uint32_t respawnSec = 0;
    std::int32_t entry = 0;
    float z = 0.0f;
    bool tierA = false;          // a quest candidate of tier A (turn-in anywhere; objective and pickup in the plan zone)
    bool canRewardTurnIn = false;
    bool dangerQueried = false;
};

ai::questbook::Facts BaseFacts(Player* bot, Quest const* quest)
{
    ai::questbook::Facts f;
    f.questId = quest->GetQuestId();
    f.botLevel = int(bot->GetLevel());
    f.questLevel = int(bot->GetQuestLevelForPlayer(quest));
    f.minLevel = int(quest->GetMinLevel());
    f.grayLevel = int(MaNGOS::XP::GetGrayLevel(bot->GetLevel()));
    f.classQuest = quest->GetRequiredClasses() != 0;
    f.excluded = ai::questbook::Excluded(f.questId);
    return f;
}

std::uint32_t FreeQuestSlots(Snapshot const& s)
{
    std::uint32_t used = 0;
    for (QuestSlot const& q : s.quests)
        if (q.id)
            ++used;
    return used < kQuestSlots ? std::uint32_t(kQuestSlots) - used : 0;
}

std::uint32_t QuestsInLog(Snapshot const& s)
{
    return std::uint32_t(kQuestSlots) - FreeQuestSlots(s);
}
// P-3a: the planner writes for this bot (condition 1 of section 2.3). Planner 0 / 1: false after one config read.
bool WritingBot(PlayerbotAI* ai, ai::supervisor::Supervisor const& sup, std::uint32_t arbiter)
{
    return sPlayerbotAIConfig.planner == 2 && ai::planner_write::Writes(ai, sup, arbiter);
}

// One number per destination: its purpose, entry and quest (FNV-1a). 0 = no destination or the null target.
std::uint32_t DestKeyOf(ai::TravelDestination const* dest)
{
    if (IsNullDest(dest))
        return 0;
    std::uint32_t h = 2166136261u;
    std::uint32_t const parts[3] = {std::uint32_t(dest->GetPurpose()), std::uint32_t(dest->GetEntry()), QuestOf(dest)};
    for (std::uint32_t p : parts)
        for (int i = 0; i < 4; ++i)
        {
            h ^= (p >> (8 * i)) & 0xffu;
            h *= 16777619u;
        }
    h &= ~kGrindTag;
    return h ? h : 1u;
}

// OB-10 B1 / B4 (section 14): after the main SetBestTarget (ChooseTravelTargetAction::Execute), read only. purpose = the choice's
// future travel purpose ("quest", the Grind purpose number, or another errand).
void NoteChoiceImpl(PlayerbotAI* ai, char const* purpose, ai::TravelDestination const* chosen)
{
    if (!sPlayerbotAIConfig.planner || !ai)
        return;
    Player* const bot = ai->GetBot();
    ai::supervisor::Supervisor* const supervisor = ai->GetSupervisor();
    if (!bot || !supervisor)
        return;
    std::string const p = purpose ? purpose : "";
    bool const quest = p == "quest";
    bool const grind = p == std::to_string(std::uint32_t(ai::TravelDestinationPurpose::Grind));
    PlanState& st = supervisor->Plan();
    std::uint32_t const nowMs = WorldTimer::getMSTime();
    PlanMinCounts c;

    if (!chosen || IsNullDest(chosen))
    {
        // B4: the choice found nothing. back_to_band_would by purpose, with the loop guard of P-3 (one per bot and bind time).
        ai::band_table::Table const* const table = ai::band_table::Get();
        if (!table)
            return;
        std::uint32_t const key = BandKey(bot);
        ai::band_table::Row const* const band = ai::band_table::Find(*table, key, bot->GetLevel());
        std::uint32_t const zone = st.prev.valid ? st.prev.zone : bot->GetCachedZoneId();
        FloorVerdict const here = FloorCheck(ZoneFloor(table, key, zone), bot->GetLevel());
        if (!BackToBandDue(true, QuestsInLog(st.prev), here, band && band->zoneId == zone, band != nullptr))
            return;
        // back_to_band_would: once per bot and bind time (the episodes, the loop guard of P-3); the purpose fields count every
        // failed choice (the distribution OB-00 asked for, section 14.4). back_to_band_repeat stays 0 in the shadow: nothing acts, so
        // a stranded bot fails again by construction (the guard "= 0" is P-3's).
        bool const allowed = BackToBandAllowed(st.lastBackMs, nowMs, sPlayerbotAIConfig.plannerMinBindSec * 1000u);
        // P-3a (section 2.5, B15; Planner = 2, writing bot, quest or grind search): back_to_band acts - the pick becomes the band
        // hub (grind, forced), EvMode plans in the next tick and holds it (Bind; OB-10 V-1: no mode switch and no expire from
        // back_to_band - the mode stays, the grind comes through the existing grind fallback), the next grind choice is fed. lastBackMs
        // is set only by the effect, so back_to_band_repeat (guard = 0) is live. Every other bot keeps the shadow rule below.
        if ((quest || grind) && WritingBot(ai, *supervisor, ai::supervisor::ArbiterValue(sPlayerbotAIConfig.supervisorArbiter)))
        {
            if (allowed && band)
            {
                st.lastBackMs = nowMs ? nowMs : 1u;
                ++c.backToBand;
                ++c.backToBandWould;
                Pick p;
                p.has = true;
                p.kind = Kind::Grind;
                p.key = TargetKey(std::uint8_t(Kind::Grind), 0, band->zoneId | kGrindTag);
                p.sinceMs = st.lastBackMs;
                p.atMs = st.lastBackMs;
                p.zone = band->zoneId;
                p.map = band->hubMap;
                p.x = band->hubX;
                p.y = band->hubY;
                p.forced = true;
                st.pick = p;
                st.expirePending = 0;
                st.evNext |= EvMode;
            }
            else
                ++c.backToBandRepeat;
        }
        else if (allowed)
        {
            st.lastBackMs = nowMs ? nowMs : 1u;
            ++c.backToBandWould;
        }
        if (quest)
            ++c.backToBandWouldQuest;
        else if (grind)
            ++c.backToBandWouldGrind;
        Flush(bot->GetGUIDLow(), c, -1);
        return;
    }

    // B1: the denominator is a fresh plan pick and a quest or grind search. Quest kinds compare quest and destination; grind
    // compares the zone (B6: a planner hub never matches a mob target by entry).
    std::uint32_t chosenQuest = 0, chosenDest = 0;
    if (auto const* q = dynamic_cast<ai::QuestTravelDestination const*>(chosen))
    {
        chosenQuest = q->GetQuestId();
        chosenDest = DestKeyOf(chosen);
    }
    else if (grind)
    {
        WorldPosition const botPos(bot);
        WorldPosition const* const point = chosen->GetClosestPoint(botPos);
        chosenDest = (point ? ZoneOfPoint(*point) : 0) | kGrindTag;
    }
    else
        chosenDest = DestKeyOf(chosen);
    Pick const& pick = st.pick;
    std::uint32_t const pickQuest = pick.kind == Kind::Grind ? 0 : pick.questId;
    std::uint32_t const pickDest = pick.kind == Kind::Grind ? (pick.zone | kGrindTag) : pick.destKey;
    Compare const verdict = ComparePick(quest || grind, pick.has, pick.atMs, nowMs, sPlayerbotAIConfig.plannerMinBindSec * 1000u,
        pickQuest, pickDest, chosenQuest, chosenDest);
    switch (verdict)
    {
        case Compare::Agree:
            ++(grind ? c.agreeGrind : c.agree);
            st.agree.destKey = DestKeyOf(chosen);
            st.agree.atMs = nowMs ? nowMs : 1u;
            break;
        case Compare::Disagree: ++(grind ? c.disagreeGrind : c.disagree); break;
        case Compare::NaPurpose: ++c.naPurpose; break;
        case Compare::NaStale: ++c.naStale; break;
    }
    Flush(bot->GetGUIDLow(), c, -1);
}

// P-3a: the counts of one expire step (section 6.1). The switch tick counts every step; a pending expire counts only its end.
void CountExpireStep(PlanMinCounts& c, ExpireStep step, ExpireGate gate, bool grind)
{
    switch (step)
    {
        case ExpireStep::Expired:
            ++c.expired;
            if (gate == ExpireGate::WorkOverride)
                ++c.expireWorkOverride;
            break;
        case ExpireStep::AppliedLive: ++(grind ? c.appliedLiveGrind : c.appliedLiveQuest); break;
        case ExpireStep::ZoneUnknown: ++c.expireDeniedZoneUnknown; break;
        case ExpireStep::DeniedCooldown: ++c.expireDenied; ++c.expireDeniedCooldown; break;
        case ExpireStep::DeniedWork: ++c.expireDenied; ++c.expireDeniedWork; break;
        case ExpireStep::DeniedLane: ++c.expireDenied; ++c.expireDeniedLane; break;
        case ExpireStep::DeniedGroup: ++c.expireDenied; ++c.expireDeniedGroup; break;
        case ExpireStep::DeniedThread: ++c.expireDenied; ++c.expireDeniedThread; break;
        case ExpireStep::Repeat: ++c.expireRepeat; break;
        case ExpireStep::BlockedFb: ++c.expireBlockedFb; break;
        case ExpireStep::Go:
        case ExpireStep::NotOpen:
            break;
    }
}

// P-3a: after an expire (rule 4 and switch_ms).
void NoteExpired(PlanState& st, PlanMinCounts& c, std::uint32_t nowMs)
{
    st.expiredKey = st.pick.key;
    st.expiredSinceMs = st.pick.sinceMs;
    st.expirePending = 0;
    st.pendingSawWork = false;
    std::uint32_t const switchMs = nowMs - st.pick.sinceMs;
    if (switchMs > c.switchMsMax)
        c.switchMsMax = switchMs;
}

// P-3a: expire_pending, once per bot and minute.
void NotePending(PlanState& st, PlanMinCounts& c)
{
    std::uint32_t const minute = std::uint32_t(time(nullptr)) / 60;
    if (st.pendingMinute == minute)
        return;
    st.pendingMinute = minute;
    ++c.expirePending;
}

// ---- R9 / R2 (twow-repo#575; R9-REPLAN-INTERFACE v5; DESIGN-R9-2-20261120.md D1-D13) ----------------------------------------
// The target kind of the live destination, by purpose bit (D13: a live objective carries one QuestObjectiveN bit, so a mask).
ai::replan::TargetKind ReplanKindOf(ai::TravelDestinationPurpose purpose)
{
    std::uint32_t const p = std::uint32_t(purpose);
    auto has = [p](ai::TravelDestinationPurpose bits) { return (p & std::uint32_t(bits)) != 0; };
    if (has(ai::TravelDestinationPurpose::QuestTaker))
        return ai::replan::TargetKind::Taker;
    if (has(ai::TravelDestinationPurpose::QuestAllObjective))
        return ai::replan::TargetKind::Objective;
    if (has(ai::TravelDestinationPurpose::QuestGiver))
        return ai::replan::TargetKind::Giver;
    if (has(ai::TravelDestinationPurpose::Grind))
        return ai::replan::TargetKind::Grind;
    if (has(ai::TravelDestinationPurpose::Trainer) || has(ai::TravelDestinationPurpose::GatherSkinning) ||
        has(ai::TravelDestinationPurpose::GatherMining) || has(ai::TravelDestinationPurpose::GatherHerbalism) ||
        has(ai::TravelDestinationPurpose::GatherFishing) || has(ai::TravelDestinationPurpose::Station))
        return ai::replan::TargetKind::Profession;
    if (has(ai::TravelDestinationPurpose::GenericRpg))
        return ai::replan::TargetKind::Rpg;
    return ai::replan::TargetKind::Other;
}

// D9: the credit signature of a quest from the core status (kill / object and item counts, explored, the complete bit). A change
// is progress. find(), never operator[]: a read must not add a status row.
std::uint32_t CreditSeqOf(Player* bot, std::uint32_t questId)
{
    QuestStatusMap& statuses = bot->getQuestStatusMap();
    auto const it = statuses.find(questId);
    if (it == statuses.end())
        return 0;
    QuestStatusData const& q = it->second;
    std::uint32_t seq = 0;
    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        seq += q.m_creatureOrGOcount[i] + q.m_itemcount[i];
    if (q.m_explored)
        seq += 1;
    if (q.m_status == QUEST_STATUS_COMPLETE)
        seq += 0x10000u;
    return seq;
}

// Sample.skillUps for a profession target: the skill values and maxima (a learned rank raises the maximum) of the professions,
// without temporary bonuses.
std::uint32_t ProfessionSkillSum(Player* bot)
{
    static std::uint16_t const skills[] = {SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_ENCHANTING, SKILL_ENGINEERING, SKILL_HERBALISM,
        SKILL_LEATHERWORKING, SKILL_MINING, SKILL_SKINNING, SKILL_TAILORING, SKILL_COOKING, SKILL_FIRST_AID, SKILL_FISHING};
    std::uint32_t sum = 0;
    for (std::uint16_t const skill : skills)
        sum += std::uint32_t(bot->GetSkillValuePure(skill)) + std::uint32_t(bot->GetSkillMaxPure(skill));
    return sum;
}

bool AnyCount(ai::replan::Counters const& k)
{
    return k.stalls || k.relocates || k.keep || k.replan || k.close || k.forced || k.rateLimited || k.forcedAfterRateLimited ||
           k.gaps || k.plannedCredit || k.hops || k.grindNoKill || k.offMap;
}

// One [Replan] line per decision (v5 6.5, 7.4). D11: a forced record is never dropped (ReplanWirePolicy.h OutsideLineBudget);
// every other line takes the [Replan] budget, and a refused one counts as dropped= of its cohort.
void EmitReplan(ai::replan::LineCtx const& ctx, ai::replan::Outcome const& o, std::uint32_t& dropped)
{
    if (!o.decided)
        return;
    ai::replan::Record const& r = o.rec;
    if (!ai::replan_wire::OutsideLineBudget(r) && !TakeReplanLine())
    {
        ++dropped;
        return;
    }
    char buf[1024];
    ai::replan::FormatReplanLine(buf, sizeof(buf), ctx, r);
    sLog.outBasic("%s", buf);
}

// The sink's view (D13): the group-held part comes from the supervisor member, the rest from what this tick stored.
ai::replan_wire::PlannerView SinkView(PlanState const& st, bool groupHeld)
{
    ai::replan_wire::PlannerView v;
    v.groupMember = groupHeld || (st.replan && st.replan->groupCopy);
    v.respawnWait = st.replan && st.replan->respawnWait;
    return v;
}

// The meter in the planner tick, only while AiPlayerbot.Replan or Replan.Shadow is on (the caller checks ReplanWired). The meter
// key is the live travel target, not the pick. Lines and counters only (D1): no planner memory write, no event bit, no pick change.
void ReplanTick(PlayerbotAI* ai_, Player* bot, PlanState& st, Snapshot const& cur, ai::TravelTarget* target, std::uint32_t ev,
    ai::replan_wire::Taken const& taken, std::uint32_t guid, std::uint32_t nowMs, std::uint32_t arbiter)
{
    std::uint32_t const mode = sPlayerbotAIConfig.replan;
    // D4: the quest book population, not in a battleground, not parked (ArmEnforce's facts); other bots hold no meter.
    bool const scope = ai_->InQuestBookScope() && !bot->InBattleGround() && !ai_->IsParked();
    if (!ai::replan_wire::MeterOn(mode, sPlayerbotAIConfig.replanShadow, guid, scope))
    {
        // the counts this bot still holds go to its cohort's minute before the meter is dropped
        if (st.replan && (st.replan->pendingDropped || AnyCount(st.replan->pending)))
            FlushReplan(guid, st.replan->pending, st.replan->pendingDropped, !st.replan->active);
        st.replan.reset();
        return;
    }
    if (!st.replan)
        st.replan = std::make_unique<ReplanState>();
    ReplanState& rs = *st.replan;
    rs.active = ai::replan_wire::Active(mode, guid);
    ai::supervisor::Supervisor* const sink = ai_->GetSupervisor();
    if (!sink)
        return;

    // The sample (v5 2.1, 6.1): a target on its way or at work; otherwise key 0 and the meter idles.
    ai::replan::Sample s;
    s.nowMs = nowMs;
    s.mapId = cur.map;
    s.x = cur.x;
    s.y = cur.y;
    s.z = cur.z;
    ai::TravelDestination* const dest = target ? target->GetDestination() : nullptr;
    WorldPosition const* const point = target ? target->GetPosition() : nullptr;
    if (!IsNullDest(dest) && point && (cur.travel == TravelPhase::Travel || cur.travel == TravelPhase::Work))
    {
        s.quest = QuestOf(dest);
        s.targetEntry = dest->GetEntry();
        s.tx = point->getX();
        s.ty = point->getY();
        s.tz = point->getZ();
        s.kind = ReplanKindOf(dest->GetPurpose());
        s.targetKey = ai::replan::TargetKey(s.quest, s.targetEntry, s.tx, s.ty, s.tz);
        s.remaining = ai::replan_wire::RemainingYd(point->getMapId() == cur.map, s.tx - cur.x, s.ty - cur.y, s.tz - cur.z);
    }
    s.routeKnown = false;   // no route module in the tree yet (M-a)
    s.creditSeq = s.quest ? CreditSeqOf(bot, s.quest) : 0;
    std::uint32_t const xp = bot->GetUInt32Value(PLAYER_XP);
    rs.xpTotal += ai::replan_wire::XpStep(rs.lastLevel, rs.lastXp, cur.level, xp);
    rs.lastLevel = cur.level;
    rs.lastXp = xp;
    s.xp = rs.xpTotal;
    s.kills = 0;   // D10: PlayerbotAI has no kill hook outside CombatRule.ProgressGuard (so a grind target is not fed, R-5)
    s.skillUps = s.kind == ai::replan::TargetKind::Profession ? ProfessionSkillSum(bot) : 0;
    s.unreachable = ai_->ReplanUnreachable();
    bool const combat = bot->IsInCombat();
    // D8: Passive from the facts (scope), never from the supervisor mode; inside the scope it is false.
    s.state = ai::replan_wire::ClockOf(cur.alive, bot->IsTaxiFlying(), bot->GetTransport() != nullptr, !scope, combat,
        !combat && bot->IsSittingDown());
    // OB-10 R-5: without a kill counter a grind target is not fed (the meter idles, counted as grind_nokill=).
    bool const grindNoKill = s.targetKey && ai::replan_wire::GrindNoKill(s.kind);
    if (grindNoKill)
        s.targetKey = 0;
    // OB-10 R-2: from the death edge to the revive edge the clock pauses at the last living target and distance.
    ai::replan_wire::HoldWhileDead(s, rs.held, cur.alive);

    rs.groupCopy = target && target->IsGroupCopy();
    rs.respawnWait = ai::replan_wire::RespawnWait(st.deadWaitSinceMs, st.pick.has, st.pick.destKey, cur.travelDest,
        st.pick.respawnSec, nowMs);

    ai::replan::LineCtx ctx;
    ctx.guid = guid;
    ctx.cohort = ai::cohort::Letter(guid);
    ctx.arbiter = arbiter;
    ctx.shadow = !rs.active;
    ctx.level = cur.level;
    ctx.offmap = ai::replan_wire::OffMap(s.targetKey, s.remaining);   // OB-10 R-6: tagged, out of C1/C2
    std::uint32_t dropped = 0;
    std::uint32_t decided = 0;

    // Relocation first (v5 7.1): it re-bases the best distance without credit, so Feed never credits the jump itself. D5: the death
    // edge, the revive edge, else a jump of a living bot that is no movement hop (D6); none while the key is 0 (D4).
    RelocSource source = RelocSource::Unknown;
    if (s.targetKey && ai::replan_wire::RelocationOf((ev & EvDied) != 0, (ev & EvAlive) != 0, (ev & (EvRelocate | EvMap)) != 0,
            cur.alive, taken, source))
    {
        ai::replan::RelocationEvent e;
        e.source = source;
        e.fromMap = st.prev.map;
        e.fx = st.prev.x;
        e.fy = st.prev.y;
        e.fz = st.prev.z;
        e.toMap = cur.map;
        e.tx = cur.x;
        e.ty = cur.y;
        e.tz = cur.z;
        e.targetKey = s.targetKey;
        e.atMs = nowMs;
        e.remaining = s.remaining;
        e.routeKnown = s.routeKnown;
        e.planned = false;                    // no route executor yet (v5 8.1)
        e.legIndex = ai::replan::NoLeg;
        // OB-10 nit 1: a relocate-triggered Replan or Close (reloc_loop included) is not a give-up (ReplanPolicy.h, OnRelocate).
        ai::replan::Outcome const relocated = rs.meter.OnRelocate(e, *sink);
        decided += relocated.decided ? 1u : 0u;
        EmitReplan(ctx, relocated, dropped);
    }
    ai::replan::Outcome const fed = rs.meter.Feed(s, *sink);
    decided += fed.decided ? 1u : 0u;
    EmitReplan(ctx, fed, dropped);

    ai::replan::Counters k = rs.meter.TakeCounters();
    if (cur.alive && taken.hop && (ev & (EvRelocate | EvMap)))
        ++k.hops;   // OB-10 R-3: a jump that was a movement hop
    if (grindNoKill && !s.targetKey)
        ++k.grindNoKill;   // only when the meter idled for it (the dead hold may have put a held target back)
    if (ctx.offmap)
        k.offMap += decided;
    // The counts wait in the bot's state and reach the cohort's minute (one lock) when the bot's minute changes, or at once with
    // a decided or refused line, as passiveTicks does for [PlanMin]; the first tick of a minute also rolls a quiet cohort.
    ai::replan::Add(rs.pending, k);
    rs.pendingDropped += dropped;
    std::uint32_t const minute = std::uint32_t(time(nullptr)) / 60u;
    if (decided || dropped || minute != rs.minute)
    {
        rs.minute = minute;
        FlushReplan(guid, rs.pending, rs.pendingDropped, ctx.shadow);
        rs.pending = ai::replan::Counters();
        rs.pendingDropped = 0;
    }
}

// The body of Supervisor::PlanTick (file-local, so the planner names never meet the supervisor ones): the supervisor passes
// itself (P-3a: condition 1 and PlannerMode), its planner state, mode, lane owner and last writer.
void PlanTickImpl(ai::supervisor::Supervisor& sup, PlayerbotAI* ai_, PlanState& plan_, Mode mode_, Lane owner_, WriterId lastWriter,
    std::uint32_t nowMs, std::uint32_t arbiter)
{
    if (!sPlayerbotAIConfig.planner)
        return;
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot || !bot->IsInWorld())
        return;
    PlanState& st = plan_;
    std::uint32_t const guid = bot->GetGUIDLow();
    if (!nowMs)
        nowMs = 1;
    // P-3a (AiPlayerbot.Planner = 2, Planner.WriteCohort; default 0): the write cohort and condition 1. A bot that does not write
    // plans as at Planner = 1 with the shadow's 8 memory slots; a writing bot has 16 (section 5.4).
    bool const p3 = sPlayerbotAIConfig.planner == 2;
    bool const inCohort = p3 && ai::planner_write::InWriteCohort(guid);
    bool const writes = p3 && WritingBot(ai_, sup, arbiter);
    std::size_t const memSlots = writes ? kMemorySlots : kShadowMemorySlots;
    if (!writes)
    {
        st.expirePending = 0;
        st.appliedAtMs = 0;
        st.turninQuest = 0;
        st.evNext = 0;
    }

    // ---- the snapshot (every tick, also outside QUEST and GRIND: no old event fires on the return) ----
    ai::TravelTarget* const target = LiveTarget(ai_);
    Snapshot cur;
    cur.valid = true;
    cur.atMs = nowMs;
    cur.level = bot->GetLevel();
    cur.map = bot->GetMapId();
    cur.zone = bot->GetCachedZoneId();
    cur.alive = bot->IsAlive();
    cur.onTransport = bot->IsTaxiFlying() || bot->GetTransport() != nullptr;
    cur.x = bot->GetPositionX();
    cur.y = bot->GetPositionY();
    cur.z = bot->GetPositionZ();
    cur.runSpeed = bot->GetSpeed(MOVE_RUN);
    cur.mode = std::uint8_t(mode_);
    cur.lane = std::uint8_t(owner_);
    if (target)
    {
        cur.travel = PhaseOf(target->GetStatus());
        cur.travelDest = DestKeyOf(target->GetDestination());
    }
    for (std::uint32_t slot = 0; slot < kQuestSlots && slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        std::uint32_t const id = bot->GetQuestSlotQuestId(slot);
        if (!id)
            continue;
        cur.quests[slot].id = id;
        cur.quests[slot].state = StateOfStatus(bot->GetQuestStatus(id));
    }

    PlanMinCounts c;
    bool dirty = false;
    GoneList gone;
    std::uint32_t ev = Diff(st.prev, cur, gone, sPlayerbotAIConfig.plannerRelocMinYards, sPlayerbotAIConfig.plannerRelocSlack);
    // P-3a: events of the choice for this tick (back_to_band: EvMode, section 2.5; writing bots only), and the dwell of the mode
    // the snapshot sees (section 2.6; Planner = 2).
    ev |= st.evNext;
    st.evNext = 0;
    if (p3 && (!st.prev.valid || cur.mode != st.prev.mode))
        st.modeSinceMs = nowMs;

    // Quests that left the log: reward (core status or the planner's note, B2), drop, or gone (repeatable without a note).
    RewardNotes& notes = ai_->PlannerRewardNotes();
    for (std::size_t i = 0; i < gone.n; ++i)
    {
        std::uint32_t const id = gone.ids[i];
        Quest const* const quest = sObjectMgr.GetQuestTemplate(id);
        bool const repeatable = quest && quest->IsRepeatable();
        bool const noted = notes.Take(id);
        std::uint32_t const label = LabelGone(repeatable, bot->GetQuestRewardStatus(id), noted);
        ev |= label;
        // B2: the re-take clock of a repeatable quest starts at its turn-in; without a note at every gone (conservative).
        if (repeatable)
            NoteRepeatReward(st.repeat, kRepeatSlots, id, nowMs);
    }
    if (st.prev.valid)
        for (QuestSlot const& q : cur.quests)
            if (q.id && !HasQuest(st.prev, q.id))
                if (Quest const* const quest = sObjectMgr.GetQuestTemplate(q.id))
                    if (quest->IsRepeatable())
                        NoteRepeatAccept(st.repeat, kRepeatSlots, q.id, nowMs);
    notes.Clear();   // a note of a quest still in the log (or never in it) is stale at the next tick
    UpdateComplete(st.complete, cur, nowMs);

    // Relocation (section 13.4): the source comes from the teleport sites. R9-2 (twow-repo#575, D3): with AiPlayerbot.Replan and
    // Replan.Shadow 0 the P-2b take every tick stays, and as nothing writes the latch then, a jump is "unknown" as before. Wired,
    // the latch is taken at a jump (a same-map relocation or a map change) and dropped once stale otherwise, so a source set before
    // the ack survives until the jump.
    bool const wired = ai_->ReplanWired();
    bool const jumped = (ev & (EvRelocate | EvMap)) != 0;
    ai::replan_wire::Taken taken;
    if (!wired || jumped)
        taken = ai_->TakeLastRelocSource(nowMs);
    else
        ai_->ExpireLastRelocSource(nowMs);
    RelocSource relocSrc = taken.source;
    if (ev & EvRelocate)
    {
        ++c.relocate;
        if (relocSrc == RelocSource::Unknown)
            ++c.relocUnknown;
        dirty = true;
    }
    else
        relocSrc = RelocSource::Unknown;

    // Zone floor (section 13.5, Nachtrag 1): counted at a zone or map change, never blocked.
    ai::band_table::Table const* const table = ai::band_table::Get();
    std::uint32_t const key = BandKey(bot);
    FloorVerdict const floorHere = FloorCheck(ZoneFloor(table, key, cur.zone), cur.level);
    if (table && (ev & (EvZone | EvMap)))
    {
        if (floorHere == FloorVerdict::Below)
            ++c.floorBelow;
        else if (floorHere == FloorVerdict::Unknown)
            ++c.floorUnknown;
        dirty = true;
    }

    // B1 proxy overwritten_shadow: a questbook-lane writer replaced the live target that agreed with the pick inside the bind time.
    std::uint32_t const minBindMs = sPlayerbotAIConfig.plannerMinBindSec * 1000u;
    switch (CheckAgree(st.agree, st.prev, cur, nowMs, minBindMs,
        ai::supervisor::LaneOfWriter(lastWriter).lane == Lane::Questbook))
    {
        case AgreeEnd::Keep:
            break;
        case AgreeEnd::Overwritten:
            ++c.overwrittenShadow;
            dirty = true;
            st.agree = AgreeMark();
            break;
        case AgreeEnd::Ended:
            st.agree = AgreeMark();
            break;
    }

    // ---- P-3a (Planner = 2): fb ticks, overwritten*, turnin_unreached and the pending expire, before the plan gate ----
    if (p3)
    {
        // fb_arbiter / fb_cohort (section 2.6): ticks of a bot with the ArmEnforce facts that does not write; a bot that is
        // Passive by design (real player master, battleground, parked) never counts. Flushed at the first tick of a later minute.
        std::uint32_t const minute = std::uint32_t(time(nullptr)) / 60;
        if ((st.fbArbiterTicks || st.fbCohortTicks) && minute != st.fbMinute)
        {
            c.fbArbiter += st.fbArbiterTicks;
            c.fbCohort += st.fbCohortTicks;
            st.fbArbiterTicks = 0;
            st.fbCohortTicks = 0;
            dirty = true;
        }
        st.fbMinute = minute;
        if (!writes && ai_->InQuestBookScope() && !bot->InBattleGround() && !ai_->IsParked())
            ++(inCohort ? st.fbArbiterTicks : st.fbCohortTicks);
    }
    if (writes && st.appliedAtMs)
    {
        // overwritten / _group / _neutral (section 6.1): inside the bind time a new write generation with another destination
        // replaced the open applied plan target; the lane of the last writer names it (None = neutral). The same destination
        // only moves the generation; a target that was no longer open ended (no count).
        std::uint32_t const gen = target ? target->WriteGen() : st.appliedGen;
        if (nowMs - st.appliedAtMs >= minBindMs)
            st.appliedAtMs = 0;
        else if (gen != st.appliedGen)
        {
            WorldPosition const* const livePos = target ? target->GetPosition() : nullptr;
            std::uint32_t const liveDest = st.appliedGrindZone ? (livePos ? ZoneOfPoint(*livePos) : 0u) : cur.travelDest;
            bool const open = st.prev.valid && (st.prev.travel == TravelPhase::Travel || st.prev.travel == TravelPhase::Work);
            if (liveDest && liveDest == st.appliedDest)
                st.appliedGen = gen;
            else
            {
                if (open)
                {
                    Lane const lane = ai::supervisor::LaneOfWriter(lastWriter).lane;
                    ++(lane == Lane::Questbook ? c.overwritten : (lane == Lane::Group ? c.overwrittenGroup : c.overwrittenNeutral));
                    dirty = true;
                }
                st.appliedAtMs = 0;
            }
        }
    }
    if (writes && st.turninQuest)
    {
        // turnin_unreached (B12): an applied turn-in whose quest is still in the log MaxHoldSec later.
        std::uint32_t const maxHoldMs = sPlayerbotAIConfig.plannerMaxHoldSec * 1000u;
        if (!HasQuest(cur, st.turninQuest))
            st.turninQuest = 0;
        else if (maxHoldMs && nowMs - st.turninAtMs >= maxHoldMs)
        {
            ++c.turninUnreached;
            st.turninQuest = 0;
            dirty = true;
        }
    }
    if (writes && st.expirePending)
    {
        // The pending expire (section 2.3, F8): conditions 1-7 again every tick; only its end counts (expired /
        // expire_after_work) and the once-a-minute gauge expire_pending. It ends with a new pick, the expire, the live target
        // being the pick, or a live target that is no longer open (also WORK -> COOLDOWN).
        if (!st.pick.has || st.pick.key != st.expirePending)
            st.expirePending = 0;
        else
        {
            ExpireGate gate = ExpireGate::Allowed;
            bool sawTravel = false;
            ExpireStep const step = ai::planner_write::Expire(ai_, st.pick, st, nowMs, minBindMs, gate, sawTravel);
            if (step == ExpireStep::Expired)
            {
                ++c.expired;
                if (gate == ExpireGate::WorkOverride)
                    ++c.expireWorkOverride;
                if (st.pendingSawWork && sawTravel)
                    ++c.expireAfterWork;
                NoteExpired(st, c, nowMs);
                dirty = true;
            }
            else if (PendingKeeps(st.expirePending, st.pick.key, step))
            {
                if (step == ExpireStep::DeniedWork)
                    st.pendingSawWork = true;
                std::uint32_t const before = c.expirePending;
                NotePending(st, c);
                dirty = dirty || c.expirePending != before;
            }
            else
                st.expirePending = 0;
        }
    }

    // W8 (section 5): at the objective pick, probe the bot's own grid for a living target (at most every kProbeEveryMs).
    std::uint32_t waitSec = 0, cycles = 0;
    if (st.pick.has && st.pick.kind == Kind::QuestObjective && st.pick.entry && cur.alive && st.pick.map == cur.map &&
        nowMs - st.lastProbeMs >= kProbeEveryMs)
    {
        float const dx = cur.x - st.pick.x, dy = cur.y - st.pick.y;
        if (dx * dx + dy * dy <= kArriveYards * kArriveYards)
        {
            st.lastProbeMs = nowMs;
            float const radius = sPlayerbotAIConfig.plannerDeadProbeYards;
            std::uint32_t living = 0;
            if (st.pick.entry > 0)
            {
                std::list<Creature*> list;
                bot->GetCreatureListWithEntryInGrid(list, std::uint32_t(st.pick.entry), radius);
                for (Creature* creature : list)
                    if (creature && creature->IsAlive())
                        ++living;
            }
            else
            {
                std::list<GameObject*> list;
                bot->GetGameObjectListWithEntryInGrid(list, std::uint32_t(-st.pick.entry), radius);
                for (GameObject* go : list)
                    if (go && go->isSpawned())
                        ++living;
            }
            if (st.deadKey != st.pick.key)
            {
                st.deadKey = st.pick.key;
                st.deadWaitSinceMs = 0;
            }
            std::uint32_t const waitMs = DeadWaitFor(st.pick.respawnSec, sPlayerbotAIConfig.plannerDeadWaitMaxSec);
            switch (DeadProbe(living, st.deadWaitSinceMs, nowMs, waitMs))
            {
                case DeadStep::Alive:
                    st.deadWaitSinceMs = 0;
                    break;
                case DeadStep::Wait:
                    if (!st.deadWaitSinceMs)
                    {
                        st.deadWaitSinceMs = nowMs;
                        ++c.deadWait;
                        dirty = true;
                        ev |= EvDeadTarget;
                    }
                    break;
                case DeadStep::Close:
                {
                    // P-3a (section 5.4): a writing bot keeps 16 slots and a saturating deadCycles (N1); mem_evict counts every
                    // replaced slot that had not expired (N2; printed at Planner = 2 only).
                    bool evicted = false;
                    Close(st.memory, memSlots, st.pick.key, st.pick.questId, std::uint8_t(st.pick.kind), CloseReason::Dead, nowMs,
                        waitMs ? waitMs : 1u, writes, &evicted);
                    if (evicted)
                        ++c.memEvict;
                    cycles = DeadCycles(st.memory, memSlots, st.pick.key);
                    waitSec = (nowMs - st.deadWaitSinceMs) / 1000u;
                    st.deadWaitSinceMs = 0;
                    ++c.deadClose;
                    dirty = true;
                    ev |= EvClosed;
                    break;
                }
            }
        }
    }

    // ---- R9 / R2 (twow-repo#575, R9 v5; AiPlayerbot.Replan / Replan.Shadow, default 0): the progress meter every tick, before the
    // plan gate. Off: no state (D3). ----
    if (wired)
        ReplanTick(ai_, bot, st, cur, target, ev, taken, guid, nowMs, arbiter);
    else
        st.replan.reset();

    // ---- plan only in QUEST or GRIND, on an event or after MaxIdleSec ----
    bool const planning = mode_ == Mode::Quest || mode_ == Mode::Grind;
    if (mode_ == Mode::Passive)
    {
        std::uint32_t const minute = std::uint32_t(time(nullptr)) / 60;
        if (st.passiveTicks && minute != st.passiveMinute)
        {
            c.passiveTicks += st.passiveTicks;
            st.passiveTicks = 0;
            dirty = true;
        }
        st.passiveMinute = minute;
        ++st.passiveTicks;
    }
    std::uint32_t const maxIdleMs = sPlayerbotAIConfig.plannerMaxIdleSec * 1000u;
    bool const due = planning && PlanDue(ev, cur.alive, nowMs, st.lastPlanMs, maxIdleMs);
    st.prev = cur;
    if (!due)
    {
        if (dirty)
            Flush(guid, c, -1);
        return;
    }
    if (!(ev & ~EvTimer) && st.lastPlanMs)
    {
        ev |= EvTimer;
        ++c.timer;
    }
    st.lastPlanMs = nowMs;

    // ---- the plan ----
    std::uint32_t const startMs = WorldTimer::getMSTime();
    std::uint32_t const budgetMs = sPlayerbotAIConfig.plannerBudgetMs;
    bool overBudget = false;
    Weights w;
    w.dangerPenalty = sPlayerbotAIConfig.plannerDangerPenalty;
    w.switchCost = sPlayerbotAIConfig.plannerSwitchCost;
    w.brainBonusMax = sPlayerbotAIConfig.plannerBrainBonusMax;
    w.turnInWorkSec = sPlayerbotAIConfig.plannerTurnInWorkSec;
    float const speed = cur.runSpeed > 0.5f ? cur.runSpeed : 7.0f;
    float const routeFactor = sPlayerbotAIConfig.plannerRouteFactor;
    ai::band_table::Row const* const band = table ? ai::band_table::Find(*table, key, cur.level) : nullptr;
    std::uint32_t const planZone = band ? band->zoneId : cur.zone;
    std::uint32_t const startZoneMin = sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevel;
    if (band && band->r1MinLevel && band->r1MinLevel != startZoneMin)
        ++c.r1Mismatch;
    ai::questbook::Params const qbParams = ai::questbook::ConfigParams();
    std::uint32_t const freeSlots = FreeQuestSlots(cur);

    static thread_local std::vector<Slot> slots;
    slots.clear();
    std::uint32_t cut = 0, repeatCapped = 0, waitingA = 0;
    std::size_t const cap = std::min<std::size_t>(sPlayerbotAIConfig.plannerMaxCandidates, kMaxCandidates);
    WorldPosition const botPos(bot);
    PlayerTravelInfo const info(bot);

    auto addSlot = [&](Slot&& s) -> bool
    {
        if (slots.size() >= cap)
        {
            ++cut;
            return false;
        }
        if (budgetMs && WorldTimer::getMSTimeDiffToNow(startMs) > budgetMs)
        {
            overBudget = true;
            ++cut;
            return false;
        }
        slots.push_back(std::move(s));
        return true;
    };
    auto fillPoint = [&](Slot& s, ai::TravelDestination* dest) -> bool
    {
        WorldPosition const* const point = dest->GetClosestPoint(botPos);
        if (!point)
            return false;
        s.c.mapId = point->getMapId();
        s.c.x = point->getX();
        s.c.y = point->getY();
        s.z = point->getZ();
        s.c.destId = DestKeyOf(dest);
        s.entry = dest->GetEntry();
        s.c.entry = std::uint32_t(s.entry);
        if (s.c.mapId == cur.map)
        {
            float const dx = s.c.x - cur.x, dy = s.c.y - cur.y;
            s.c.routeSec = std::sqrt(dx * dx + dy * dy) / speed * routeFactor;
        }
        else
            s.c.routeSec = 3600.0f;   // another map: no estimate (the route cost of a map change is not known here)
        s.zone = ZoneOfPoint(*point);
        s.c.zoneChange = s.zone && s.zone != cur.zone;
        return true;
    };

    // Turn-ins first, then objectives, then pickups (each by straight distance; the cap cuts the tail).
    std::vector<std::int32_t> completeIds, openIds;
    for (QuestSlot const& q : cur.quests)
    {
        if (q.state == QuestState::Complete)
            completeIds.push_back(std::int32_t(q.id));
        else if (q.state == QuestState::Incomplete)
            openIds.push_back(std::int32_t(q.id));
    }
    auto byDistance = [&](std::vector<ai::TravelDestination*>& list)
    {
        std::vector<std::pair<float, ai::TravelDestination*>> keyed;
        keyed.reserve(list.size());
        for (ai::TravelDestination* d : list)
        {
            WorldPosition const* const p = d->GetClosestPoint(botPos);
            keyed.emplace_back(p ? p->distance(botPos) : 1.0e9f, d);
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < keyed.size(); ++i)
            list[i] = keyed[i].second;
    };

    if (!completeIds.empty())
    {
        std::vector<ai::TravelDestination*> takers = sTravelMgr.GetDestinations(info,
            std::uint32_t(ai::TravelDestinationPurpose::QuestTaker), completeIds, false, kCandidateRadius);
        byDistance(takers);
        for (ai::TravelDestination* dest : takers)
        {
            std::uint32_t const questId = QuestOf(dest);
            Quest const* const quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest || !TakerTakes(dest->GetEntry(), questId))
                continue;
            Slot s;
            s.c.kind = Kind::QuestTurnIn;
            s.c.questId = questId;
            if (!fillPoint(s, dest))
                continue;
            s.c.xp = float(quest->XPValue(bot));
            ai::questbook::Facts f = BaseFacts(bot, quest);
            f.inLog = true;
            f.complete = StateOf(cur, questId) == QuestState::Complete;
            f.canReward = PlannerCanReward(bot, quest);
            f.closed = IsClosed(st.memory, memSlots, TargetKey(std::uint8_t(Kind::QuestTurnIn), questId, s.c.destId), nowMs);
            ai::questbook::Answer const a = ai::questbook::CanTurnIn(f);   // takerSpawned stays Unknown: CheckAtRuntime counts
            s.canRewardTurnIn = f.canReward;
            if (a.IsNo())
                s.c.blockedReason = std::uint8_t(a.reason);
            s.tierA = true;   // a turn-in counts anywhere (P3 (a); switchCost prices a zone change)
            if (!addSlot(std::move(s)))
                break;
        }
    }
    if (budgetMs && WorldTimer::getMSTimeDiffToNow(startMs) > budgetMs)
        overBudget = true;   // the budget is checked between the fetches too, not only per candidate
    if (!openIds.empty() && !overBudget && slots.size() < cap)
    {
        std::vector<ai::TravelDestination*> objectives = sTravelMgr.GetDestinations(info,
            std::uint32_t(ai::TravelDestinationPurpose::QuestAllObjective), openIds, false, kCandidateRadius);
        byDistance(objectives);
        for (ai::TravelDestination* dest : objectives)
        {
            std::uint32_t const questId = QuestOf(dest);
            Quest const* const quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest)
                continue;
            Slot s;
            s.c.kind = Kind::QuestObjective;
            s.c.questId = questId;
            if (!fillPoint(s, dest))
                continue;
            s.c.xp = float(quest->XPValue(bot));
            s.c.workSec = kObjectiveWorkSec;
            if (auto const* objective = dynamic_cast<ai::QuestObjectiveTravelDestination const*>(dest))
                s.respawnSec = objective->GetSourceStats().respawnSec;
            ai::questbook::Facts f = BaseFacts(bot, quest);
            f.inLog = true;
            std::uint32_t const k = TargetKey(std::uint8_t(Kind::QuestObjective), questId, s.c.destId);
            f.closed = IsClosed(st.memory, memSlots, k, nowMs);
            ai::questbook::Answer const a = ai::questbook::CanTarget(f, ai::questbook::Purpose::Objective, qbParams);
            if (a.IsNo())
            {
                s.c.blockedReason = std::uint8_t(a.reason);
                if (f.closed)
                    ++waitingA;   // held back by a close with an end: tier B grinds while it runs
            }
            s.tierA = !s.zone || s.zone == planZone;
            if (!addSlot(std::move(s)))
                break;
        }
    }
    if (budgetMs && WorldTimer::getMSTimeDiffToNow(startMs) > budgetMs)
        overBudget = true;
    // onlyPossible = false: QuestRelationTravelDestination::IsPossible counts [QuestBookMin] target_no_* for a bot with a book
    // (TravelMgr.cpp); the planner gates by itself (CanTakeQuest without a message, CanAccept, CanTarget) and writes no counter.
    if (!overBudget && slots.size() < cap)
    {
        std::vector<ai::TravelDestination*> givers = sTravelMgr.GetDestinations(info,
            std::uint32_t(ai::TravelDestinationPurpose::QuestGiver), {}, false, kPickupRadius);
        byDistance(givers);
        for (ai::TravelDestination* dest : givers)
        {
            std::uint32_t const questId = QuestOf(dest);
            Quest const* const quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
                continue;
            if (!quest->IsRepeatable() && bot->GetQuestRewardStatus(questId))
                continue;
            if (!bot->CanTakeQuest(quest, false))   // msg = false: no packet (read at 8d76c1d1)
                continue;
            Slot s;
            s.c.kind = Kind::QuestPickup;
            s.c.questId = questId;
            if (!fillPoint(s, dest))
                continue;
            if (s.zone && s.zone != planZone)
                continue;   // pickups only in the plan zone (section 2)
            s.c.xp = float(quest->XPValue(bot));
            s.c.workSec = kPickupWorkSec;
            ai::questbook::Facts f = BaseFacts(bot, quest);
            f.freeSlots = freeSlots;
            ai::questbook::Answer a = ai::questbook::CanAccept(f, qbParams);
            if (!a.IsNo())
                a = ai::questbook::CanTarget(f, ai::questbook::Purpose::Giver, qbParams);
            if (a.IsNo())
                s.c.blockedReason = std::uint8_t(a.reason);
            else if (quest->IsRepeatable() &&
                RepeatBlocked(FindRepeat(st.repeat, kRepeatSlots, questId), nowMs, sPlayerbotAIConfig.plannerRepeatableRetakeSec * 1000u,
                    sPlayerbotAIConfig.plannerRepeatableMaxPerHour))
            {
                s.c.blockedReason = std::uint8_t(Reason::RepeatCap);
                ++repeatCapped;
            }
            s.tierA = true;
            if (!addSlot(std::move(s)))
                break;
        }
    }

    // Grind at the hub of the plan zone (tier B) and of the next zone (tier C); no table: no hub (fb=band_table_off).
    auto grindSlot = [&](ai::band_table::Row const* row) -> Slot
    {
        Slot s;
        s.c.kind = Kind::Grind;
        s.c.mapId = row->hubMap;
        s.c.x = row->hubX;
        s.c.y = row->hubY;
        s.z = row->hubZ;
        s.zone = row->zoneId;
        s.c.destId = row->zoneId | kGrindTag;
        s.c.zoneChange = row->zoneId != cur.zone;
        std::uint32_t const gray = MaNGOS::XP::GetGrayLevel(cur.level);
        bool const grey = row->mobLevel && row->mobLevel <= gray;   // GreyByLevel: grey mobs give no XP
        s.c.xp = row->mobLevel && !grey ?
            float(MaNGOS::XP::BaseGain(cur.level, cur.level, row->mobLevel)) * sPlayerbotAIConfig.plannerGrindKillsPerMin : 0.0f;
        s.c.workSec = kGrindWorkSec;
        if (s.c.mapId == cur.map)
        {
            float const dx = s.c.x - cur.x, dy = s.c.y - cur.y;
            s.c.routeSec = std::sqrt(dx * dx + dy * dy) / speed * routeFactor;
        }
        else
            s.c.routeSec = 3600.0f;
        return s;
    };

    // ---- tiers (section 3) ----
    std::uint32_t eligibleA = 0;
    for (Slot const& s : slots)
        if (s.tierA && !s.c.blockedReason)
            ++eligibleA;
    TierInput tin;
    tin.eligibleA = eligibleA;
    tin.waitingA = waitingA + (st.deadWaitSinceMs ? 1u : 0u);
    tin.bandTable = table != nullptr;
    tin.r1Allows = !ai::zone_keep::StartZoneGated(startZoneMin, cur.level);
    ai::band_table::Row const* const nextRow = NextRow(table, key, band, cur.level);
    tin.nextZoneKnown = nextRow != nullptr;
    TierPick const tier = PickTier(tin);

    // Only the tier's candidates are ranked; B and C rank their one grind candidate.
    std::vector<Candidate> pool;
    std::vector<std::size_t> poolSlot;
    if (tier.tier == Tier::A)
    {
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].tierA)
            {
                pool.push_back(slots[i].c);
                poolSlot.push_back(i);
            }
    }
    else
    {
        ai::band_table::Row const* const row = tier.tier == Tier::C ? nextRow : band;
        if (row)
        {
            slots.push_back(grindSlot(row));
            pool.push_back(slots.back().c);
            poolSlot.push_back(slots.size() - 1);
        }
    }

    // Rank without danger, then the danger map for the best DangerTopN on the bot's map (section 2), then rank again.
    Hysteresis h;
    h.marginPct = sPlayerbotAIConfig.plannerMargin;
    h.minBindMs = minBindMs;
    Incumbent inc;
    inc.has = st.pick.has;
    inc.key = st.pick.key;
    inc.sinceMs = st.pick.sinceMs;
    std::vector<Scored> ranked(pool.size() + 1);
    std::size_t nRanked = Rank(pool.data(), pool.size(), w, ranked.data(), ranked.size());
    for (std::size_t i = 0; i < nRanked; ++i)
        if (inc.has && ranked[i].c && TargetKey(std::uint8_t(ranked[i].c->kind), ranked[i].c->questId, ranked[i].c->destId) == inc.key)
        {
            inc.stillEligible = true;
            inc.value = ranked[i].value;
        }
    // P-3a (section 2.5): the back_to_band pick of a writing bot (the forced band hub) is not in the tier's pool; it holds its
    // bind time like an eligible incumbent (then Bind, or Forced for a max_hold), so the EvMode plan does not drop it at once.
    // OB-10 V-1: kept by Bind / Same, it never reaches the mode switch or the expire below (BackToBandHolds, unit-tested).
    if (BackToBandHolds(writes, inc.stillEligible, st.pick.has, st.pick.forced, st.pick.kind == Kind::Grind, st.pick.sinceMs,
            nowMs, minBindMs))
    {
        inc.stillEligible = true;
        inc.value = st.pick.value;
    }
    std::uint32_t skip = 0;
    if (DangerGate(ai_, bot) && nRanked)
    {
        std::uint32_t const topN = sPlayerbotAIConfig.plannerDangerTopN;
        std::uint32_t const cells = sPlayerbotAIConfig.plannerDangerCells ? sPlayerbotAIConfig.plannerDangerCells : 1u;
        std::uint32_t asked = 0;
        for (std::size_t i = 0; i < nRanked && asked < topN; ++i)
        {
            std::size_t const p = std::size_t(ranked[i].c - pool.data());
            Slot& s = slots[poolSlot[p]];
            if (CannotWin(ranked[i].value, inc, h))
            {
                ++skip;
                continue;
            }
            if (s.c.mapId != cur.map)
                continue;
            ai::danger_map::DangerMap::Hit const hit = ai::danger_map::Instance().Query(cur.map, cur.x, cur.y, s.c.x, s.c.y, cur.level,
                std::uint32_t(time(nullptr)),
                ai::danger_map::ConfigParams(ai::cohort::Applies(sPlayerbotAIConfig.dangerMapCountAddDeaths, guid)));
            float const danger = float(hit.cells) / float(cells);
            pool[p].danger = danger < 1.0f ? danger : 1.0f;
            s.c.danger = pool[p].danger;
            s.dangerQueried = true;
            ++asked;
        }
        nRanked = Rank(pool.data(), pool.size(), w, ranked.data(), ranked.size());
        for (std::size_t i = 0; i < nRanked; ++i)
            if (inc.has && ranked[i].c &&
                TargetKey(std::uint8_t(ranked[i].c->kind), ranked[i].c->questId, ranked[i].c->destId) == inc.key)
                inc.value = ranked[i].value;
    }

    // The maximum holding time (BOT-16 point 2): a complete quest with a routable turn-in for MaxHoldSec wins in tier A.
    Scored const* top = nRanked ? &ranked[0] : nullptr;
    Reason reason = top ? (top->c->kind == Kind::QuestTurnIn ? Reason::TurnInFirst : (nRanked == 1 ? Reason::OnlyCandidate : Reason::BestValue)) : Reason::Alternative;
    bool forced = false;
    if (tier.tier == Tier::A)
        for (std::size_t i = 0; i < nRanked; ++i)
        {
            Candidate const* const cand = ranked[i].c;
            if (cand->kind != Kind::QuestTurnIn)
                continue;
            Slot const& s = slots[poolSlot[std::size_t(cand - pool.data())]];
            if (MaxHoldDue(CompleteSince(st.complete, cand->questId), nowMs, sPlayerbotAIConfig.plannerMaxHoldSec * 1000u,
                    s.canRewardTurnIn))
            {
                top = &ranked[i];
                reason = Reason::MaxHold;
                forced = true;
                ++c.maxHold;
                break;
            }
        }
    if (tier.fb == Fallback::R1Hold)
        reason = Reason::R1Hold;
    else if (tier.fb == Fallback::BandTableOff && !top)
        reason = Reason::BandTableOff;
    if (ev & EvClosed)
        reason = top && reason != Reason::MaxHold ? Reason::DeadTarget : reason;

    // ---- the hysteresis (section 4) ----
    Hold hold = Hold::Switch;
    Slot const* pickSlot = nullptr;
    bool wrote = false;         // P-3a: this plan expired the live target ([Plan] write=1)
    bool modeChanged = false;   // P-3a: this plan switched QUEST <-> GRIND (one more [Plan] line, ev=mode)
    if (top)
    {
        Candidate const* const cand = top->c;
        std::uint32_t const newKey = TargetKey(std::uint8_t(cand->kind), cand->questId, cand->destId);
        hold = HoldDecision(inc, newKey, top->value, forced, nowMs, h);
        if (hold == Hold::Margin || hold == Hold::Bind)
        {
            reason = Reason::Incumbent;
            ++c.keeps;
            st.pick.atMs = nowMs;   // the incumbent is confirmed by this plan (fresh for the shadow comparison)
        }
        else
        {
            pickSlot = &slots[poolSlot[std::size_t(cand - pool.data())]];
            if (hold == Hold::Same)
            {
                ++c.keeps;
                st.pick.value = top->value;
                st.pick.atMs = nowMs;
            }
            else
            {
                ++c.switches;
                // B3: a pick switch would expire the live target; COOLDOWN is locked, WORK only for max_hold (P-3 acts).
                if (ExpireCheck(cur.travel, forced) == ExpireGate::WorkOverride)
                    ++c.expireWorkOverrideWould;
                if (pickSlot->c.zoneChange)
                {
                    ++c.zoneChange;
                    if (ev & EvClosed)
                        ++c.zoneChangeDead;
                }
                Pick p;
                p.has = true;
                p.kind = cand->kind;
                p.questId = cand->questId;
                p.destKey = cand->kind == Kind::Grind ? 0 : cand->destId;
                p.key = newKey;
                p.value = top->value;
                p.sinceMs = nowMs;
                p.atMs = nowMs;
                p.zone = pickSlot->zone;
                p.respawnSec = pickSlot->respawnSec;
                p.entry = pickSlot->entry;
                p.map = cand->mapId;
                p.x = cand->x;
                p.y = cand->y;
                p.forced = forced;
                st.pick = p;
                if (writes)
                {
                    // P-3a (section 2.3): the expire of the live target, conditions 2-7 (PlannerWrite.cpp); a denial by WORK,
                    // the fallback block, the lane, the group copy or the thread leaves it pending for this pick.
                    st.expirePending = 0;
                    st.pendingSawWork = false;
                    ExpireGate gate = ExpireGate::Allowed;
                    bool sawTravel = false;
                    ExpireStep const step = ai::planner_write::Expire(ai_, st.pick, st, nowMs, minBindMs, gate, sawTravel);
                    CountExpireStep(c, step, gate, p.kind == Kind::Grind);
                    if (step == ExpireStep::Expired)
                    {
                        NoteExpired(st, c, nowMs);
                        wrote = true;
                    }
                    else if (PendingStarts(step))
                    {
                        st.expirePending = p.key;
                        st.pendingSawWork = step == ExpireStep::DeniedWork;
                        NotePending(st, c);
                    }
                    // P-3a (section 2.6, P3-3): the mode follows only a pick change (switch, or forced: max_hold); a grind pick
                    // of tier C wants GRIND, every other pick QUEST. Decide with Requester::Planner and the dwell decides.
                    // OB-10 V-1: back_to_band never gets here (its pick is set outside the hysteresis, then held by Bind, no
                    // tier C): it switches no mode and expires nothing; the mode line stays QUEST.
                    if (ModeSwitchFrom(hold))
                    {
                        Mode const want = PickWantsGrind(p.kind == Kind::Grind, tier.tier) ? Mode::Grind : Mode::Quest;
                        Mode const from = sup.CurrentMode();
                        if (want != from)
                        {
                            if (sup.PlannerMode(want, nowMs - st.modeSinceMs, arbiter).allowed)
                            {
                                ++(want == Mode::Grind ? c.modeQuestGrind : c.modeGrindQuest);
                                st.modeSinceMs = nowMs;
                                modeChanged = true;
                            }
                            else
                                ++c.modeDenied;
                        }
                    }
                }
            }
        }
    }
    ++c.plans;
    switch (tier.tier)
    {
        case Tier::A: ++c.tierA; break;
        case Tier::B: ++c.tierB; break;
        case Tier::C: ++c.tierC; break;
    }
    c.cut += cut;
    c.skip += skip;
    c.repeatCap += repeatCapped;
    if (overBudget)
        ++c.overBudget;
    std::uint32_t const ms = WorldTimer::getMSTimeDiffToNow(startMs);
    c.msMax = ms;

    // ---- the [Plan] line (own budget) ----
    if (TakePlanLine())
    {
        PlanLine l;
        l.guid = guid;
        l.cohort = ai::cohort::Letter(guid);
        l.arbiter = arbiter;
        l.mode = mode_;
        // The line shows the plan's pick: the incumbent when it stays (reason=incumbent), else the new top.
        Scored const* shownScored = top;
        if (reason == Reason::Incumbent)
            for (std::size_t i = 0; i < nRanked; ++i)
                if (TargetKey(std::uint8_t(ranked[i].c->kind), ranked[i].c->questId, ranked[i].c->destId) == inc.key)
                    shownScored = &ranked[i];
        Candidate const* const lc = shownScored ? shownScored->c : nullptr;
        if (lc)
        {
            l.kind = KindName(lc->kind);
            l.id = lc->questId;
            l.destId = lc->destId;
            l.value = shownScored->value;
            l.xpm = shownScored->xpPerMin;
            l.routeSec = lc->routeSec;
            l.workSec = WorkSec(*lc, w);
            l.restSec = lc->restSec;
            l.danger = lc->danger;
            l.turnIn = lc->kind == Kind::QuestTurnIn;
            l.brain = lc->brainPref;
            l.dangerQueried = slots[poolSlot[std::size_t(lc - pool.data())]].dangerQueried;
        }
        l.reason = ReasonName(reason);
        l.cands = std::uint32_t(slots.size());
        for (std::size_t i = 1; i <= kAlts && i < nRanked; ++i)
        {
            l.alt[i - 1].kind = KindName(ranked[i].c->kind);
            l.alt[i - 1].id = ranked[i].c->questId;
            l.alt[i - 1].value = ranked[i].value;
        }
        std::uint32_t blocked = 0;
        for (Slot const& s : slots)
            if (s.c.blockedReason)
                ++blocked;
        l.blocked = blocked;
        l.budgetMs = budgetMs;
        l.level = cur.level;
        l.zone = cur.zone;
        l.band = band ? band->zoneId : 0;
        l.tier = TierName(tier.tier);
        l.ev = EvName(ev);
        l.route = "est";
        // P-3a (section 2.6): at Planner = 2 a bot outside the write cohort shows fb=cohort, one in it that does not write (arbiter
        // below 2 or Passive) fb=arbiter; Planner 0 / 1 show the tier's fallback as before.
        l.fb = FallbackName(p3 ? (!inCohort ? Fallback::Cohort : (!writes ? Fallback::Arbiter : tier.fb)) : tier.fb);
        l.marginPct = h.marginPct;
        l.cut = cut;
        l.skip = skip;
        l.plannerLimit = "-";
        l.waitSec = waitSec ? waitSec : (st.deadWaitSinceMs ? (nowMs - st.deadWaitSinceMs) / 1000u : 0u);
        l.cycles = cycles;
        l.relocSrc = (ev & EvRelocate) ? ai::replan::SourceName(relocSrc) : "-";
        l.floor = table ? FloorName(floorHere) : "-";
        l.hold = top ? HoldName(hold) : "-";
        l.write = wrote ? 1u : 0u;   // P-3a: 1 = this plan expired the live target (writing bot); the shadow writes nothing
        l.ms = ms;
        char buf[1024];
        FormatPlanLine(buf, sizeof(buf), l);
        sLog.outBasic("%s", buf);
    }
    else
        ++c.planDropped;

    // P-3a (section 2.6): one more [Plan] line per applied mode switch (ev=mode, the new mode; the per-bot hour view of P3-3), from
    // the same budget (plan_dropped when it is used up).
    if (modeChanged)
    {
        if (TakePlanLine())
        {
            PlanLine m;
            m.guid = guid;
            m.cohort = ai::cohort::Letter(guid);
            m.arbiter = arbiter;
            m.mode = sup.CurrentMode();
            m.kind = KindName(st.pick.kind);
            m.id = st.pick.questId;
            m.destId = st.pick.kind == Kind::Grind ? (st.pick.zone | kGrindTag) : st.pick.destKey;
            m.value = st.pick.value;
            m.reason = ReasonName(reason);
            m.budgetMs = budgetMs;
            m.level = cur.level;
            m.zone = cur.zone;
            m.band = band ? band->zoneId : 0;
            m.tier = TierName(tier.tier);
            m.ev = EvName(EvMode);
            m.fb = FallbackName(tier.fb);
            m.marginPct = h.marginPct;
            m.hold = HoldName(hold);
            m.write = wrote ? 1u : 0u;
            m.ms = ms;
            char buf[1024];
            FormatPlanLine(buf, sizeof(buf), m);
            sLog.outBasic("%s", buf);
        }
        else
            ++c.planDropped;
    }

    Flush(guid, c, int(ms));
}
}

namespace ai::planner_shadow
{
std::uint32_t DestKey(ai::TravelDestination const* dest)
{
    return DestKeyOf(dest);
}

void NoteChoice(PlayerbotAI* ai, char const* purpose, ai::TravelDestination const* chosen)
{
    NoteChoiceImpl(ai, purpose, chosen);
}

std::uint32_t QuestOfDest(ai::TravelDestination const* dest)
{
    return QuestOf(dest);
}

std::uint32_t ZoneOf(ai::WorldPosition const* point)
{
    return point ? ZoneOfPoint(*point) : 0u;
}

void Count(std::uint32_t guidLow, PlanMinCounts const& c)
{
    Flush(guidLow, c, -1);
}
}

namespace ai::supervisor
{
// A3 R9 / R2 wiring pair R9-2 (twow-repo#575, R9 v5 3; D13): the sink of the bot's meter. The planner answers with a keep reason it
// knows (group_member, respawn_wait), else Replan planner_choice; the meter enforces the forced rules and the hourly cap. Read only.
ai::replan::Verdict Supervisor::OnStall(ai::replan::StallEvent const&)
{
    return ai::replan_wire::PlannerVerdict(SinkView(plan_, mode_ == Mode::Group || groupHeld_));
}

ai::replan::Verdict Supervisor::OnRelocate(ai::replan::RelocationEvent const&)
{
    return ai::replan_wire::PlannerVerdict(SinkView(plan_, mode_ == Mode::Group || groupHeld_));
}

void Supervisor::PlanTick(std::uint32_t nowMs, std::uint32_t arbiter)
{
    if (!sPlayerbotAIConfig.planner)
        return;
    PlanTickImpl(*this, ai_, plan_, mode_, owner_, lastWriter_, nowMs, arbiter);
}
}
