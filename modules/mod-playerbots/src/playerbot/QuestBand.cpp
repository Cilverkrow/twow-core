#include "playerbot/playerbot.h"
#include "playerbot/QuestBand.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/TravelNode.h"
#include "playerbot/HomeBindPolicy.h"
#include "playerbot/QuestFailureClassPolicy.h"
#include "playerbot/QuestFeasibilityPolicy.h"
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/strategy/actions/ChooseTravelTargetAction.h"
#include "Database/DBCStores.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace ai;

// twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder, OB-00 D9 19.10.2026; S5A-DESIGN.md with the answers of
// OB-00 / OB-10 / OB-30 / CLI-PATH 19.10.2026): the band step - a persistent quest-first roster bot on its own below the max
// level whose zone has no acceptable quest and no open turn-in left (the second empty quest search in a row, EmptyOffer)
// travels to the next quest hub of hub-ladder.tsv. The trip has a wall-clock end decided once at the step (TryBandStep is
// the only SetBandTripEnd site); it ends only in TravelTarget::CheckStatus (arrived / expired / skip) or when another
// destination replaces it. An expiry is no failure; the second expiry of an episode asks for a forward rescue to the hub (at
// most one rescue per bot per hour, idle rescues included). Pure rules and every line format: QuestBandPolicy.h.
//
// Threads: the store is built once at boot on the world thread before any bot update and published last (release store,
// acquire load); it is never changed or freed afterwards (LoadQuestTravelTable runs once). The band episode is the bot
// thread's; the world thread only sets the park reset flag, takes the band rescue and reads the from zone (atomics). The
// minute counters are relaxed atomics. No lock anywhere.

namespace
{
constexpr std::size_t MaxLadderBytes = 1024 * 1024;   // fail closed above 1 MiB ...
constexpr std::uint32_t MaxLadderLines = 2000;         // ... or 2000 lines
constexpr float NodeMatchRange = 10.0f;               // CLI-PATH Q10: the travel nodes within 10 yd on the row's map ...
constexpr float NodeMatchMaxDistance = 2.0f;          // ... the nearest one at most 2 yd away and named like the hub
constexpr std::uint32_t SkipLogSeconds = 600;          // no_rung / log_full lines: once per bot per 10 min (counted always)

// The store (boot only; read only afterwards). A list keeps the point addresses stable for the destinations.
std::list<WorldPosition> hubPoints;
std::vector<std::unique_ptr<HubTravelDestination>> hubs;   // index = rung index = Ladder::rungs index
ai::quest_band::Ladder ladder;
std::atomic<bool> built{ false };
bool loadAttempted = false;

// [QuestBand] minute counters per cohort (0 = A, 1 = B), counted on the map threads, taken by the world thread.
std::atomic<uint32> bandCounts[2][ai::quest_band::EventCount] = {};

void LogLadderOff(char const* reason, std::string const& path)
{
    sLog.outError("%s", ai::quest_band::FormatLadderOff(reason, path).c_str());
}

// Line 1 "# version=1" (followed by the end of the line or a blank / tab), anything else is another format.
bool VersionLine(std::string const& line)
{
    static std::string const prefix = "# version=1";
    if (line.compare(0, prefix.size(), prefix) != 0)
        return false;
    return line.size() == prefix.size() || line[prefix.size()] == ' ' || line[prefix.size()] == '\t' ||
        line[prefix.size()] == '\r';
}

char const* StatusName(TravelStatus status)
{
    switch (status)
    {
        case TravelStatus::TRAVEL_STATUS_NONE: return "none";
        case TravelStatus::TRAVEL_STATUS_PREPARE: return "prepare";
        case TravelStatus::TRAVEL_STATUS_READY: return "ready";
        case TravelStatus::TRAVEL_STATUS_TRAVEL: return "travel";
        case TravelStatus::TRAVEL_STATUS_WORK: return "work";
        case TravelStatus::TRAVEL_STATUS_COOLDOWN: return "cooldown";
        case TravelStatus::TRAVEL_STATUS_EXPIRED: return "expired";
        default: return "none";
    }
}

char FactionOf(Player* bot)
{
    return bot->GetTeam() == ALLIANCE ? 'A' : 'H';
}

// The skip lines (no_rung, log_full) of one bot: at most one per SkipLogSeconds; a clock step back logs again.
bool SkipLogDue(PlayerbotAI::BandEpisode& episode, uint32 now)
{
    if (episode.skipLogAt && now >= episode.skipLogAt && now - episode.skipLogAt < SkipLogSeconds)
        return false;
    episode.skipLogAt = now;
    return true;
}
}

// ---- store ---------------------------------------------------------------------------------------------------------

void ai::quest_band::LoadLadder()
{
    if (!sPlayerbotAIConfig.questFirstProgressionBandLadder)
        return;   // BandLadder 0: no file I/O, no line

    // Boot only: the store is never rebuilt or freed (destination pointers may sit in travel targets).
    if (loadAttempted)
        return;
    loadAttempted = true;

    std::string const path = sPlayerbotAIConfig.questFirstProgressionBandLadderFile;
    if (path.empty())
    {
        LogLadderOff("no_file", path);
        return;
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
    {
        LogLadderOff("unreadable", path);
        return;
    }
    std::string bytes(MaxLadderBytes + 1, '\0');
    file.read(&bytes[0], std::streamsize(bytes.size()));
    if (file.bad())
    {
        LogLadderOff("unreadable", path);
        return;
    }
    bytes.resize(std::size_t(file.gcount()));
    if (bytes.size() > MaxLadderBytes)
    {
        LogLadderOff("too_long", path);
        return;
    }

    // Lines (bounded by MaxLadderLines; the rows are judged after the whole file passed the size gates).
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    while (start < bytes.size())
    {
        if (lines.size() >= MaxLadderLines)
        {
            LogLadderOff("too_long", path);
            return;
        }
        std::string::size_type const end = bytes.find('\n', start);
        lines.push_back(bytes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    if (lines.empty() || !VersionLine(lines[0]))
    {
        LogLadderOff("version", path);
        return;
    }
    // Line 2: "# source=..." - its content (sha tokens and anything after them) is ignored.
    Header header;
    if (lines.size() < 3 || lines[1].compare(0, 9, "# source=") != 0 || !ParseHeader(lines[2], header))
    {
        LogLadderOff("header", path);
        return;
    }

    LoadCounts counts;
    counts.bytes = uint32(bytes.size());
    counts.fnv = Fnv1a(bytes);
    counts.crsz = sPlayerbotAIConfig.customRaceStartZones ? 1u : 0u;
    counts.mode = sPlayerbotAIConfig.questFirstProgressionBandLadder;
    bool const crsz = sPlayerbotAIConfig.customRaceStartZones;
    // Roster bots are random bots: their cheats are rndBotCheatMask (PlayerbotAI::HasCheat). A flight row needs the taxi cheat.
    bool const allowFlight = (sPlayerbotAIConfig.rndBotCheatMask & uint32(BotCheatMask::taxi)) != 0;

    std::set<std::tuple<uint32, uint32, uint32, uint32>> seen;   // (key, wave0, min, rank) of every parsed row
    for (std::size_t i = 3; i < lines.size(); ++i)
    {
        std::string const& line = lines[i];
        if (line.empty() || line == "\r" || line[0] == '#')
            continue;
        uint32 const lineNo = uint32(i + 1);
        ++counts.rows;

        Rung rung;
        std::vector<uint32> taxiPaths;
        RowVerdict const verdict = ParseRow(SplitTabs(line), header, crsz, allowFlight, rung, taxiPaths);
        if (verdict == RowVerdict::SkipParse)
        {
            ++counts.skipParse;
            sLog.outBasic("%s", FormatRowSkipped(false, lineNo, rung, "parse").c_str());
            continue;
        }
        if (!seen.insert(std::make_tuple(rung.keyIndex, rung.wave0, rung.minLevel, rung.rank)).second)
        {
            ++counts.skipDup;
            sLog.outBasic("%s", FormatRowSkipped(false, lineNo, rung, "duplicate").c_str());
            continue;
        }
        switch (verdict)
        {
            case RowVerdict::SkipReliability: ++counts.skipReliability; continue;   // reliability != ok: counter only
            case RowVerdict::SkipWave0: ++counts.skipWave0; continue;
            case RowVerdict::SkipRoute: ++counts.skipRoute; continue;
            case RowVerdict::SkipFromL12: ++counts.skipFromL12; continue;
            case RowVerdict::DegradedTransport:
            case RowVerdict::DegradedNoTaxiCheat:
                ++counts.degraded;
                sLog.outBasic("%s", FormatRowSkipped(true, lineNo, rung, RowVerdictName(verdict)).c_str());
                continue;
            default:
                break;
        }

        // A flight row: every TaxiPath id must exist on this server.
        bool taxiOk = true;
        for (uint32 const pathId : taxiPaths)
            taxiOk = taxiOk && sTaxiPathStore.LookupEntry(pathId) != nullptr;
        if (!taxiOk)
        {
            ++counts.degraded;
            sLog.outBasic("%s", FormatRowSkipped(true, lineNo, rung, "taxi_path").c_str());
            continue;
        }

        // The hub by position (saveNodeStore renumbers node ids; hub_id is for the lines only): the nearest travel node within
        // NodeMatchRange on the row's map (getNodes filters by the position's map, sorted by distance) must lie at most
        // NodeMatchMaxDistance away and carry the hub's name.
        WorldPosition const hubPos(rung.mapId, rung.x, rung.y, rung.z, 0.0f);
        std::vector<TravelNode*> const nodes = sTravelNodeMap.getNodes(hubPos, NodeMatchRange);
        if (nodes.empty() || nodes.front()->getPosition()->distance(hubPos) > NodeMatchMaxDistance ||
            nodes.front()->getName() != rung.hubName)
        {
            ++counts.skipNode;
            sLog.outBasic("%s", FormatRowSkipped(false, lineNo, rung, "no_node").c_str());
            continue;
        }

        // Same granularity as the bot side (bot->GetZoneId()): the ladder zone must be a top-level zone.
        AreaEntry const* zone = AreaEntry::GetById(rung.zoneId);
        if (!zone || !zone->IsZone())
        {
            ++counts.skipZone;
            sLog.outBasic("%s", FormatRowSkipped(false, lineNo, rung, zone ? "zone_not_top" : "zone_unknown").c_str());
            continue;
        }

        uint32 const index = uint32(ladder.rungs.size());
        ladder.Add(rung);
        hubPoints.push_back(hubPos);
        hubs.emplace_back(new HubTravelDestination(index));
        hubs.back()->SetHubPoint(&hubPoints.back());
        ++counts.usable;
    }

    sLog.outBasic("%s", FormatLoaded(path, counts).c_str());
    if (counts.usable)
        built.store(true, std::memory_order_release);
}

bool ai::quest_band::Built()
{
    return built.load(std::memory_order_acquire);
}

ai::quest_band::Ladder const& ai::quest_band::GetLadder()
{
    return ladder;
}

HubTravelDestination* ai::quest_band::GetHub(uint32 rung)
{
    return Built() && rung < hubs.size() ? hubs[rung].get() : nullptr;
}

ai::quest_band::LineHead ai::quest_band::HeadFor(Player* bot, uint32 fromZone, uint32 rung, bool hasRung)
{
    LineHead head;
    head.bot = bot->GetGUIDLow();
    head.level = bot->GetLevel();
    head.cohort = cohort::Letter(bot->GetGUIDLow());
    head.fromZone = fromZone;
    head.toHub = hasRung && Built() && rung < ladder.rungs.size() ? ladder.rungs[rung].hubId : 0;
    return head;
}

bool ai::quest_band::TripScopeOk(Player* bot, uint32 rung, char const** detail)
{
    char const* why = nullptr;
    PlayerbotAI* botAi = bot ? GetBotAI(bot) : nullptr;
    if (!sPlayerbotAIConfig.questFirstProgressionBandLadder || !Built() || rung >= ladder.rungs.size() || !botAi ||
        !Applies(sPlayerbotAIConfig.questFirstProgressionBandLadder, bot->GetGUIDLow()))
        why = "switch";
    else if (botAi->HasRealPlayerMaster())
        why = "master";
    else if (bot->GetGroup())
        why = "group";
    else if (bot->GetLevel() >= sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        why = "max_level";
    else if (!botAi->InQuestBandScope())
        why = "switch";
    else if (bot->GetMapId() != ladder.rungs[rung].mapId)
        why = "map";

    if (detail)
        *detail = why ? why : "none";
    return !why;
}

bool ai::quest_band::GiverTripDoneFor(PlayerbotAI* ai, Player* bot, TravelDestination const* destination)
{
    if (!sPlayerbotAIConfig.questFirstProgressionBandLadder || !ai || !bot || !destination)
        return false;

    QuestRelationTravelDestination const* giver = dynamic_cast<QuestRelationTravelDestination const*>(destination);
    if (!giver || giver->GetRelation() != 0)
        return false;

    // Only where F applies (S5A-DESIGN.md 5.4: E / F are skipped while the bot has focus quests, forceThisQuest): a focus
    // quest's giver trip keeps today's COOLDOWN path and is not counted. The focus list is read only after the filter bit.
    bool const giverFilter = ai->QuestBandGiverFilter() &&
        ai->GetAiObjectContext()->GetValue<focusQuestTravelList>("focus travel target")->Get().empty();
    if (!GiverTripDone(giverFilter, bot->GetQuestStatus(giver->GetQuestId()) == QUEST_STATUS_NONE))
        return false;

    PlayerbotAI::NoteBand(uint8(EvGiverDone), cohort::InB(bot->GetGUIDLow()));
    return true;
}

std::vector<WorldLocation> ai::quest_band::HubRescueTargets(Player* bot, uint32 rung)
{
    if (Built() && rung < ladder.rungs.size() && bot->GetMapId() == ladder.rungs[rung].mapId)
    {
        Rung const& hub = ladder.rungs[rung];
        return { WorldLocation(hub.mapId, hub.x, hub.y, hub.z, 0.0f) };
    }

    PlayerbotAI* botAi = GetBotAI(bot);
    sLog.outBasic("%s", FormatRescue(HeadFor(bot, botAi ? botAi->GetBandTripFromZone() : 0, rung, true), "fallback").c_str());
    return sRandomPlayerbotMgr.QuestRescueTargets(bot);
}

void ai::quest_band::LogRescueTeleport(Player* bot, uint32 rung)
{
    PlayerbotAI* botAi = GetBotAI(bot);
    PlayerbotAI::NoteBand(uint8(EvRescue), cohort::InB(bot->GetGUIDLow()));
    sLog.outBasic("%s", FormatRescue(HeadFor(bot, botAi ? botAi->GetBandTripFromZone() : 0, rung, true), "teleport").c_str());
}

// ---- the band hub destination --------------------------------------------------------------------------------------

bool HubTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& /*info*/) const
{
    return ai::quest_band::TripScopeOk(bot, rung);
}

bool HubTravelDestination::IsIn(const WorldPosition& pos, float radius) const
{
    return OnMap(pos) && DistanceTo(pos) <= (radius ? radius : ai::quest_band::ArrivalRadius);
}

std::string HubTravelDestination::GetTitle() const
{
    if (!ai::quest_band::Built() || rung >= ladder.rungs.size())
        return "band hub";
    return "band hub " + std::to_string(ladder.rungs[rung].hubId) + " " + ladder.rungs[rung].hubName;
}

// ---- the trip (TravelTarget) ---------------------------------------------------------------------------------------

uint8 TravelTarget::CheckBandTrip() const
{
    HubTravelDestination const* hub = dynamic_cast<HubTravelDestination const*>(tDestination);
    ai::quest_band::TripFacts facts;
    facts.hubDestination = hub != nullptr;
    facts.tripEnd = bandTripEnd;
    facts.scopeOk = hub && ai::quest_band::TripScopeOk(bot, hub->GetRungIndex());
    facts.cooldown = m_status == TravelStatus::TRAVEL_STATUS_COOLDOWN;
    facts.now = uint32(time(nullptr));
    // The status clock (SetStatus gave it exactly the time left): a READY / TRAVEL / WORK clock that ran out ends the trip here,
    // so the generic expiry below never ends a band trip without its line (and never calls the work timeout). Near the end it
    // is the end; well before it the clock was shortened from outside (OB-10 option b: clock_shortened, no rescue count).
    // Not for a COOLDOWN (its short clock is min(60 s, time left)): that stays an expired_cooldown (design 4.3 / 9).
    facts.clockRanOut = bandTripEnd && m_status != TravelStatus::TRAVEL_STATUS_COOLDOWN && statusTime != 0 && GetTimeLeft() <= 0;
    return uint8(ai::quest_band::CheckTrip(facts));
}

// The only end of a band trip besides a replacement (NoteBandTripReplaced): one line, one count, the episode, EXPIRED. No
// failure counter, no work timeout, no retry, no extension.
void TravelTarget::EndBandTrip(uint8 how)
{
    namespace qb = ai::quest_band;
    HubTravelDestination const* hub = dynamic_cast<HubTravelDestination const*>(tDestination);
    PlayerbotAI::BandEpisode& episode = ai->GetBandEpisode();
    uint32 const rung = hub ? hub->GetRungIndex() : episode.rung;
    uint32 const now = uint32(time(nullptr));
    bool const cohortB = cohort::InB(bot->GetGUIDLow());
    qb::LineHead const head = qb::HeadFor(bot, ai->GetBandTripFromZone(), rung, hub != nullptr);

    if (how == qb::EndArrived)
    {
        PlayerbotAI::NoteBand(uint8(qb::EvArrived), cohortB);
        sLog.outBasic("%s", qb::FormatArrived(head, episode.stepAt && now >= episode.stepAt ? now - episode.stepAt : 0).c_str());
        ai->OnBandArrived(now);
    }
    else if (how == uint8(qb::TripVerdict::ExpiredEnd) || how == uint8(qb::TripVerdict::ExpiredCooldown))
    {
        bool const cooldown = how == uint8(qb::TripVerdict::ExpiredCooldown);
        float const distLeft = wPosition && wPosition->getMapId() == bot->GetMapId() ?
            std::sqrt(wPosition->sqDistance2d(WorldPosition(bot))) : 0.0f;
        PlayerbotAI::NoteBand(uint8(cooldown ? qb::EvExpiredCooldown : qb::EvExpired), cohortB);
        ai->OnBandExpired(now, rung, cooldown, distLeft);
    }
    else if (how == uint8(qb::TripVerdict::ClockShortened))
    {
        // OB-10 19.10.2026 (option b): an outside caller shortened the status clock (e.g. the focus command) - like a
        // replacement: no expiry, no rescue count, no failure; its own counter shows how often that happens.
        PlayerbotAI::NoteBand(uint8(qb::EvClockShortened), cohortB);
        sLog.outBasic("%s", qb::FormatSkip(head, "replaced", "clock_shortened").c_str());
    }
    else if (how == uint8(qb::TripVerdict::ScopeLost))
    {
        char const* detail = "destination";   // a band end on another destination (fail closed)
        if (hub)
            qb::TripScopeOk(bot, rung, &detail);
        PlayerbotAI::NoteBand(uint8(qb::EvScopeLost), cohortB);
        sLog.outBasic("%s", qb::FormatSkip(head, "scope_lost", detail).c_str());
    }
    else
    {
        PlayerbotAI::NoteBand(uint8(qb::EvOrphan), cohortB);
        sLog.outBasic("%s", qb::FormatSkip(head, "orphan", "no_end").c_str());
    }

    bandTripEnd = 0;
    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
    ai->GetAiObjectContext()->ClearValues("no active travel destinations");
}

// OB-10 Q4 19.10.2026: a replaced band trip (zone escape, death suppression, park, rescue, battleground, a reset, any other
// new destination) is no failure, no expiry and no rescue count - one skip line (park or replaced) and the end. Any thread
// (a park nulls the target on the world thread): only atomics and this target are touched.
void TravelTarget::NoteBandTripReplaced()
{
    namespace qb = ai::quest_band;
    HubTravelDestination const* hub = dynamic_cast<HubTravelDestination const*>(tDestination);
    bool const park = ai->IsParked();
    PlayerbotAI::NoteBand(uint8(park ? qb::EvPark : qb::EvReplaced), cohort::InB(bot->GetGUIDLow()));
    sLog.outBasic("%s", qb::FormatSkip(qb::HeadFor(bot, ai->GetBandTripFromZone(), hub ? hub->GetRungIndex() : 0, hub != nullptr),
        park ? "park" : "replaced", StatusName(m_status)).c_str());
    bandTripEnd = 0;
}

// ---- the step (ChooseTravelTargetAction) ---------------------------------------------------------------------------

bool ChooseTravelTargetAction::TryBandStep(Player* requester, TravelTarget* travelTarget, uint32 searchedStage, bool freshBot,
    bool rescueRequested)
{
    if (!sPlayerbotAIConfig.questFirstProgressionBandLadder || !ai::quest_band::Built())
        return false;
    // P3-a owns the fresh bots; a Z1b rescue asked for in this pass wins; a resumed choice is no empty search.
    if (!ai->InQuestBandScope() || freshBot || rescueRequested || chooseBudgetExceeded)
        return false;

    namespace qb = ai::quest_band;
    uint32 const now = uint32(time(nullptr));
    PlayerbotAI::BandEpisode& episode = ai->GetBandEpisode();
    // A park ended the episode: no older expiry turns into a rescue, the zone memory starts anew (the rescue stamp stays).
    if (ai->TakeBandEpisodeReset())
    {
        episode.expiries = 0;
        episode.leftZones[0] = episode.leftZones[1] = 0;
        episode.leftBandMin = 0;
    }

    // Facts: the offered turn-ins of the request (read before NoteNoRouteTurnIns clears them; written by the turn-in-only
    // request, "future travel handin quests"), the open turn-ins and the group / elite quests of the log (20 slots).
    uint32 offered = 0;
    for (std::string const& questId : split(AI_VALUE2(std::string, "manual string", "future travel handin quests"), ','))
        if (atoi(questId.c_str()) > 0)
            ++offered;
    bool const turnInParking = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures > 0;
    uint32 openTurnIns = 0, groupQuests = 0, eliteQuests = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE && (!turnInParking || travelTarget->IsTurnInOpen(questId)))
            ++openTurnIns;
        if (Quest const* quest = sObjectMgr.GetQuestTemplate(questId))
        {
            if (ai::quest_feasibility::IsGroupQuest(quest->GetType(), quest->GetSuggestedPlayers()))
                ++groupQuests;
            if (quest->GetType() == QUEST_TYPE_ELITE)
                ++eliteQuests;
        }
    }

    qb::StepFacts facts;
    facts.emptyOffer = ai::quest_failure_class::Classify(true, lastRejects, offered, 0).failureClass ==
        ai::quest_failure_class::FailureClass::EmptyOffer;
    facts.dangerMap = lastRejects.dangerMap;
    facts.offered = offered;
    facts.openTurnIns = openTurnIns;
    facts.priorFailures = AI_VALUE2(int, "manual int", "quest route failures");
    facts.freeSlots = AI_VALUE(uint8, "free quest log slots");
    facts.slotGate = AI_VALUE(bool, "can fight equal") ? 5 : 10;

    qb::StepVerdict const verdict = qb::BandStepDue(facts);
    if (verdict == qb::StepVerdict::NotDue)
        return false;

    bool const cohortB = ai::cohort::InB(bot->GetGUIDLow());
    uint32 const level = bot->GetLevel();
    uint32 const zoneId = bot->GetZoneId();
    qb::LineHead head = qb::HeadFor(bot, zoneId, 0, false);
    if (verdict == qb::StepVerdict::SkipLogFull)
    {
        // OB-10 Q3 19.10.2026: skipped and counted, nothing is dropped; the line shows the held group / elite quests.
        PlayerbotAI::NoteBand(uint8(qb::EvSkipLogFull), cohortB);
        if (SkipLogDue(episode, now))
            sLog.outBasic("%s", qb::FormatSkipLogFull(head, facts.freeSlots, groupQuests, eliteQuests).c_str());
        return false;
    }

    // Candidates (at most 4, zone against zone), then the game-side vetoes on the survivors in order: a death-suppressed hub,
    // a hub in an area clearly above the bot (zone escape's margin). The first survivor wins.
    qb::Ladder const& ladder = qb::GetLadder();
    char const faction = FactionOf(bot);
    qb::RungFacts rungFacts;
    rungFacts.race = bot->getRace();
    rungFacts.faction = faction;
    rungFacts.mapId = bot->GetMapId();
    rungFacts.level = level;
    rungFacts.zoneId = zoneId;
    rungFacts.leftZones[0] = episode.leftZones[0];
    rungFacts.leftZones[1] = episode.leftZones[1];
    rungFacts.leftBandMin = episode.leftBandMin;
    rungFacts.x = bot->GetPositionX();
    rungFacts.y = bot->GetPositionY();
    rungFacts.searchedRadius = ai::quest_search::GiverRadius(level, searchedStage);
    std::vector<qb::Candidate> candidates = qb::NextRung(ladder, rungFacts);
    int32 chosen = -1;
    for (qb::Candidate& candidate : candidates)
    {
        if (candidate.reject == qb::Reject::None)
        {
            HubTravelDestination* hub = qb::GetHub(uint32(candidate.index));
            if (!hub)
                candidate.reject = qb::Reject::NoBand;
            else if (travelTarget->IsDestinationDeathSuppressed(hub))
                candidate.reject = qb::Reject::Suppressed;
            else if (homebind::IsZoneClearlyAboveLevel(uint32(std::max<int32>(0, hub->GetHubPoint()->getAreaLevelOrParent())), level))
                candidate.reject = qb::Reject::HubAbove;
        }
        if (candidate.reject == qb::Reject::None)
        {
            chosen = candidate.index;
            break;
        }
    }
    if (chosen < 0)
    {
        // A gap of the ladder (or every hub covered, left, vetoed): today's path runs on (backoff, latch), counted and logged.
        PlayerbotAI::NoteBand(uint8(qb::EvSkipNoRung), cohortB);
        if (SkipLogDue(episode, now))
            sLog.outBasic("%s", qb::FormatSkip(head, "no_rung", qb::RejectName(qb::FirstReject(candidates))).c_str());
        return false;
    }

    // The trip: an ordinary travel target to the hub point with a fixed end (the only SetBandTripEnd), never forced; the quest
    // request's condition never becomes the trip's.
    qb::Rung const& rung = ladder.rungs[std::size_t(chosen)];
    HubTravelDestination* hub = qb::GetHub(uint32(chosen));
    float const dist = qb::Dist2D(rungFacts.x, rungFacts.y, rung.x, rung.y);
    uint32 const tripSeconds = qb::BandTripSeconds(dist);
    TravelTarget band(ai);
    band.SetTarget(hub, hub->GetHubPoint());
    band.SetRelevance(uint32(std::max(0, AI_VALUE2(int, "manual int", "future travel relevance"))));
    band.SetBandTripEnd(now + tripSeconds);
    setNewTarget(requester, &band, travelTarget);
    travelTarget->SetConditions({});
    if (travelTarget->GetDestination() != hub || !travelTarget->GetBandTripEnd())
        return false;   // the choice was refused: no step (fail closed, today's path runs on)

    ai->OnBandStep(now, zoneId, qb::CurrentBandMin(ladder, qb::KeyFor(rungFacts.race, faction, rungFacts.mapId, level), level),
        uint32(chosen));
    // Failures, backoff, transient streak and stage to 0: the latch cannot take the bot after an expiry.
    ai->RequestQuestRouteReset(ai::quest_failure_class::ResetBandStep);
    PlayerbotAI::NoteBand(uint8(qb::EvStep), cohortB);
    head.toHub = rung.hubId;
    sLog.outBasic("%s", qb::FormatStep(head, rung, dist, tripSeconds, rungFacts.searchedRadius, facts.priorFailures).c_str());
    return true;
}

// ---- the episode (PlayerbotAI) -------------------------------------------------------------------------------------

bool PlayerbotAI::InQuestBandScope()
{
    if (!sPlayerbotAIConfig.questFirstProgressionBandLadder || !bot || !ai::quest_band::Built())
        return false;

    ai::quest_band::ScopeFacts facts;
    facts.applies = ai::quest_band::Applies(sPlayerbotAIConfig.questFirstProgressionBandLadder, bot->GetGUIDLow());
    if (!facts.applies)
        return false;   // the other cohort: nothing else is read
    facts.questFirst = sPlayerbotAIConfig.questFirstProgressionEnabled;
    facts.grouped = bot->GetGroup() != nullptr;
    facts.level = bot->GetLevel();
    facts.maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    facts.realMaster = HasRealPlayerMaster();
    facts.rosterMember = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow());
    return ai::quest_band::InScope(facts);
}

bool PlayerbotAI::QuestBandGiverFilter()
{
    if (!InQuestBandScope())
        return false;

    uint32 const race = bot->getRace();
    uint32 const level = bot->GetLevel();
    return ai::quest_band::GetLadder().Covers(ai::quest_band::KeyFor(race, FactionOf(bot), bot->GetMapId(), level), race, level);
}

bool PlayerbotAI::BandTripRunning()
{
    if (!sPlayerbotAIConfig.questFirstProgressionBandLadder || !aiObjectContext)
        return false;

    TravelTarget* target = aiObjectContext->GetValue<TravelTarget*>("travel target")->Get();
    return target && target->GetBandTripEnd() && target->IsActive();
}

void PlayerbotAI::OnBandStep(uint32 now, uint32 fromZone, uint32 bandMin, uint32 rung)
{
    // The zone memory belongs to the band the bot left the zones in; a new band starts it anew (no ping-pong, 2 zones).
    if (bandEpisode.leftBandMin != bandMin)
        bandEpisode.leftZones[0] = bandEpisode.leftZones[1] = 0;
    // A re-step from the zone left last (after an expiry or a refused rescue) keeps both zones: the memory survives
    // mid-route expiries and rescues (S5A-DESIGN.md 5.3), so the older zone is never pushed out by a repeat.
    if (fromZone != bandEpisode.leftZones[0])
    {
        bandEpisode.leftZones[1] = bandEpisode.leftZones[0];
        bandEpisode.leftZones[0] = fromZone;
    }
    bandEpisode.leftBandMin = bandMin;
    bandEpisode.fromZone = fromZone;
    bandEpisode.rung = rung;
    bandEpisode.stepAt = now;
    bandTripFromZone.store(fromZone, std::memory_order_relaxed);
}

void PlayerbotAI::OnBandArrived(uint32 now)
{
    bandEpisode.expiries = 0;
    questProgress.lastChange = now;   // the idle rescue clock starts at the hub
    RequestQuestRouteReset(ai::quest_failure_class::ResetBandStep);
}

void PlayerbotAI::OnBandExpired(uint32 now, uint32 rung, bool cooldown, float distLeft)
{
    namespace qb = ai::quest_band;
    ++bandEpisode.expiries;
    bool const cohortB = ai::cohort::InB(bot->GetGUIDLow());
    qb::LineHead const head = qb::HeadFor(bot, bandTripFromZone.load(std::memory_order_relaxed), rung, true);
    sLog.outBasic("%s", qb::FormatExpired(head, cooldown, bandEpisode.expiries, distLeft).c_str());
    if (!qb::RescueDue(bandEpisode.expiries))
        return;

    // OB-00 Q6: at most one rescue per bot per hour, idle rescues included; the expiries stay for the next try.
    if (!qb::RescueAllowed(std::max(bandEpisode.lastBandRescue, lastQuestRescue.load()), now))
    {
        PlayerbotAI::NoteBand(uint8(qb::EvRescueLimit), cohortB);
        sLog.outBasic("%s", qb::FormatRescue(head, "limit").c_str());
        return;
    }
    // The idle rescue's gate: alive, out of combat, on a continent, not on a taxi or a transport, not teleporting.
    if (!bot->IsAlive() || bot->IsInCombat() || !bot->GetMap() || !bot->GetMap()->IsContinent() || bot->IsTaxiFlying() ||
        bot->GetTransport() || bot->IsBeingTeleported())
    {
        PlayerbotAI::NoteBand(uint8(qb::EvRescueBlocked), cohortB);
        sLog.outBasic("%s", qb::FormatRescue(head, "blocked").c_str());
        return;
    }

    // Stamped before the request: a dropped request still uses the slot (fail closed).
    bandEpisode.lastBandRescue = now;
    bandRescue.store(qb::PackRescue(rung, now), std::memory_order_relaxed);
    questRescueRequested = true;
    bandEpisode.expiries = 0;
    PlayerbotAI::NoteBand(uint8(qb::EvRescueRequested), cohortB);
    sLog.outBasic("%s", qb::FormatRescue(head, "requested").c_str());
}

uint32 PlayerbotAI::TakeBandRescueRung(uint32 now)
{
    uint64 const packed = bandRescue.exchange(0, std::memory_order_relaxed);
    bool dropped = false;
    uint32 const rungPlusOne = ai::quest_band::UnpackRescue(packed, now, dropped);
    if (dropped && bot)
    {
        uint32 const rung = uint32(packed & 0xFFFFFFFFu) - 1;
        sLog.outBasic("%s", ai::quest_band::FormatRescue(ai::quest_band::HeadFor(bot, bandTripFromZone.load(std::memory_order_relaxed),
            rung, true), "dropped").c_str());
    }
    return rungPlusOne;
}

void PlayerbotAI::NoteBand(uint8 event, bool cohortB)
{
    if (event < ai::quest_band::EventCount)
        bandCounts[cohortB ? 1 : 0][event].fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::string> PlayerbotAI::TakeQuestBandMinuteLines()
{
    std::vector<std::string> lines;
    for (int cohortIndex = 0; cohortIndex < 2; ++cohortIndex)
    {
        std::uint32_t counts[ai::quest_band::EventCount] = {};
        for (int event = 0; event < ai::quest_band::EventCount; ++event)
            counts[event] = bandCounts[cohortIndex][event].exchange(0, std::memory_order_relaxed);
        std::string line = ai::quest_band::FormatMinute(cohortIndex ? 'B' : 'A', counts);
        if (!line.empty())
            lines.push_back(line);
    }
    return lines;
}
