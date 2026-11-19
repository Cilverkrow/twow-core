#include "playerbot/playerbot.h"
#include "playerbot/PlannerShadow.h"
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
#include <mutex>
#include <vector>

// A3 step 3 planner P-2b (A3-S3-PLANER-ENTWURF.md v0.3, frozen d6e51878, Nachtrag 1; AiPlayerbot.Planner, default 0): the planner
// in the shadow. Supervisor::PlanTick is the last statement of Supervisor::Tick (the bot's map thread): it compares a small
// snapshot of the bot every tick (PlannerShadowPolicy.h Diff), plans on an event or after MaxIdleSec in QUEST or GRIND, and keeps
// its own pick with the hysteresis. It writes nothing of the bot - no travel target, no value, no strategy: [Plan] lines (own
// budget, 300 per minute), [PlanMin] per cohort and minute, and its own state (Supervisor::plan_). Planner = 2 is read as 1 until
// the pair P-3. The pure rules are in PlannerPolicy.h and PlannerShadowPolicy.h (unit tests t/planner_policy_tests.cpp,
// t/planner_shadow_policy_tests.cpp); the shadow comparison after SetBestTarget is ai::planner_shadow::NoteChoice (OB-10 B1).
namespace
{
using namespace ai::planner;
using ai::supervisor::Lane;
using ai::supervisor::Mode;
using ai::supervisor::WriterId;
using ai::supervisor::PlanLine;
using ai::supervisor::FormatPlanLine;

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
        char buf[1280];
        FormatPlanMin(buf, sizeof(buf), i ? 'B' : 'A', out.minute, out.sums[i]);
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
        if (BackToBandAllowed(st.lastBackMs, nowMs, sPlayerbotAIConfig.plannerMinBindSec * 1000u))
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

// The body of Supervisor::PlanTick (file-local, so the planner names never meet the supervisor ones): the supervisor passes its
// planner state, mode, lane owner and last writer.
void PlanTickImpl(PlayerbotAI* ai_, PlanState& plan_, Mode mode_, Lane owner_, WriterId lastWriter, std::uint32_t nowMs,
    std::uint32_t arbiter)
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

    // Relocation (section 13.4): the source comes from the teleport sites (R9-2); until then "unknown".
    RelocSource relocSrc = ai_->TakeLastRelocSource();
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
                    Close(st.memory, kMemorySlots, st.pick.key, st.pick.questId, std::uint8_t(st.pick.kind), CloseReason::Dead, nowMs,
                        waitMs ? waitMs : 1u);
                    for (MemorySlot const& m : st.memory)
                        if (m.key == st.pick.key)
                            cycles = m.deadCycles;
                    waitSec = (nowMs - st.deadWaitSinceMs) / 1000u;
                    st.deadWaitSinceMs = 0;
                    ++c.deadClose;
                    dirty = true;
                    ev |= EvClosed;
                    break;
            }
        }
    }

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
            f.closed = IsClosed(st.memory, kMemorySlots, TargetKey(std::uint8_t(Kind::QuestTurnIn), questId, s.c.destId), nowMs);
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
            f.closed = IsClosed(st.memory, kMemorySlots, k, nowMs);
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
                st.pick = p;
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
        l.fb = FallbackName(arbiter != 2 && sPlayerbotAIConfig.planner == 2 ? Fallback::Arbiter : tier.fb);
        l.marginPct = h.marginPct;
        l.cut = cut;
        l.skip = skip;
        l.plannerLimit = "-";
        l.waitSec = waitSec ? waitSec : (st.deadWaitSinceMs ? (nowMs - st.deadWaitSinceMs) / 1000u : 0u);
        l.cycles = cycles;
        l.relocSrc = (ev & EvRelocate) ? RelocSourceName(relocSrc) : "-";
        l.floor = table ? FloorName(floorHere) : "-";
        l.hold = top ? HoldName(hold) : "-";
        l.write = 0;   // P-2: the shadow writes nothing
        l.ms = ms;
        char buf[1024];
        FormatPlanLine(buf, sizeof(buf), l);
        sLog.outBasic("%s", buf);
    }
    else
        ++c.planDropped;

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
}

namespace ai::supervisor
{
void Supervisor::PlanTick(std::uint32_t nowMs, std::uint32_t arbiter)
{
    if (!sPlayerbotAIConfig.planner)
        return;
    PlanTickImpl(ai_, plan_, mode_, owner_, lastWriter_, nowMs, arbiter);
}
}
