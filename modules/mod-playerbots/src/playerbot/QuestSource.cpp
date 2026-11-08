#include "playerbot/playerbot.h"
#include "playerbot/QuestSource.h"
#include "playerbot/QuestBook.h"
#include "playerbot/QuestSourcePolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "Formulas.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ai::questsource
{
namespace
{
// One priced objective point of the merged list.
struct Priced
{
    TravelPoint point;
    float cost = 0.0f;    // route + work, seconds
    float score = 0.0f;   // XP per minute (quest + follow-up + side XP)
    bool named = false;
};
}

void RankQuestSources(PartitionedTravelList& list, PlayerTravelInfo const& info)
{
    if (!info.HasQuestSourceChoice() || list.empty())
        return;

    Params p;
    p.killSec = sPlayerbotAIConfig.questBookKillSeconds;            // read only after the load
    p.singleMargin = sPlayerbotAIConfig.questBookSingleSpawnMargin;
    float const speed = info.GetRunSpeed();
    uint32 const botLevel = info.GetLevel();
    uint32 const guid = info.GetQuestBookGuid();

    std::unordered_map<uint32, PlayerTravelInfo::QuestSourceNeed const*> needs;
    for (PlayerTravelInfo::QuestSourceNeed const& need : info.GetQuestSourceNeeds())
        needs[need.questId] = &need;

    // Walk the list in first-fit order (nearest partition first, list order inside). Points that are no quest objective, or of a
    // quest without a snapshot, keep their place; the objective points are grouped by (quest, objective slot).
    PartitionedTravelList kept;
    std::unordered_map<uint64, std::vector<Priced>> groups;
    std::vector<uint64> groupOrder;
    for (auto& [partition, points] : list)
    {
        for (TravelPoint const& point : points)
        {
            auto const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(std::get<0>(point));
            auto const it = objective ? needs.find(objective->GetQuestId()) : needs.end();
            uint8 const slot = objective ? objective->GetObjective() : 0;
            if (it == needs.end() || slot >= QUEST_OBJECTIVES_COUNT)
            {
                kept[partition].push_back(point);
                continue;
            }
            Stats const& s = objective->GetSourceStats();
            uint32 const needed = s.killTarget ? it->second->needKill[slot] : it->second->needItem[slot];
            float const route = RouteSec(std::get<2>(point), speed, p);
            float const work = WorkSec(needed, s, speed, p);
            uint32 const kills = s.vendor ? 0 : Kills(needed, s.chancePct);
            uint32 const mobXp = s.mobLevel && kills ? MaNGOS::XP::BaseGain(botLevel, botLevel, s.mobLevel) : 0;
            Priced priced;
            priced.point = point;
            priced.cost = route + work;
            priced.score = Score(float(it->second->xp), float(it->second->followUpXp), SideXp(kills, mobXp), route, work);
            priced.named = s.named;
            uint64 const key = (uint64(objective->GetQuestId()) << 2) | slot;
            std::vector<Priced>& group = groups[key];
            if (group.empty())
                groupOrder.push_back(key);
            group.push_back(priced);
        }
    }
    if (groups.empty())
        return;

    // QB1: one source per (quest, objective). The first entry of a group is the old first-fit choice.
    std::vector<Priced> winners;
    std::vector<Priced> losers;
    std::vector<Option> options;
    for (uint64 const key : groupOrder)
    {
        std::vector<Priced> const& group = groups[key];
        options.assign(group.size(), Option{});
        for (std::size_t i = 0; i < group.size(); ++i)
            options[i] = Option{group[i].cost, group[i].named, true};
        int const pick = std::max(0, PickSource(options.data(), options.size(), p));
        questbook::Note(guid, group[pick].named ? questbook::Count::SourceSingle : questbook::Count::SourceCommon);
        if (pick != 0)
            questbook::Note(guid, questbook::Count::SourceRegroup);
        for (std::size_t i = 0; i < group.size(); ++i)
            (int(i) == pick ? winners : losers).push_back(group[i]);
    }

    // QB3: the winners in XP-per-minute order, after what the nearest partition already holds (turn-ins stay first); the other
    // sources go to the farthest partition.
    std::stable_sort(winners.begin(), winners.end(), [](Priced const& a, Priced const& b) { return a.score > b.score; });
    if (winners.size() > 1)
        questbook::Note(guid, questbook::Count::OrderScored);
    uint32 const nearest = list.begin()->first;
    uint32 const farthest = list.rbegin()->first;
    for (Priced const& w : winners)
        kept[nearest].push_back(w.point);
    for (Priced const& l : losers)
        kept[farthest].push_back(l.point);
    list.swap(kept);
}

void NotePick(Player* bot, PlayerbotAI* ai, TravelDestination const* destination, WorldPosition const* position)
{
    if (!sPlayerbotAIConfig.questBookSourceChoice || !bot || !ai || !ai->ActiveQuestBook() ||
        !questbook::Applies(sPlayerbotAIConfig.questBookSourceChoice, bot->GetGUIDLow()))
        return;
    auto const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(destination);
    if (!objective)
        return;
    Stats const& s = objective->GetSourceStats();
    uint32 const guid = bot->GetGUIDLow();
    sLog.outBasic("[QuestSource] bot=%s guid=%u cohort=%c quest=%u obj=%u entry=%d spawns=%u respawn_s=%u chance=%u spacing=%u named=%u "
        "kill=%u vendor=%u mob_level=%u dist=%u", bot->GetName(), guid, cohort::Letter(guid), objective->GetQuestId(),
        uint32(objective->GetObjective()) + 1, objective->GetEntry(), uint32(s.spawns), uint32(s.respawnSec), uint32(s.chancePct),
        uint32(s.spacingYd), s.named ? 1u : 0u, s.killTarget ? 1u : 0u, s.vendor ? 1u : 0u, uint32(s.mobLevel),
        position ? uint32(position->distance(WorldPosition(bot))) : 0u);
}
}
