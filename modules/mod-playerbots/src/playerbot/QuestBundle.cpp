#include "playerbot/playerbot.h"
#include "playerbot/QuestBundle.h"
#include "playerbot/QuestBook.h"
#include "playerbot/QuestBundlePolicy.h"
#include "playerbot/QuestSourcePolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace ai::questbundle
{
namespace
{
// A quest giver point with a position: its quest giver destination, otherwise null.
QuestRelationTravelDestination const* GiverOf(TravelDestination const* destination, WorldPosition const* position)
{
    if (!destination || !position || destination->GetPurpose() != TravelDestinationPurpose::QuestGiver)
        return nullptr;
    return dynamic_cast<QuestRelationTravelDestination const*>(destination);
}

Point PointOf(WorldPosition const& position, uint32 questId)
{
    return Point{position.getMapId(), position.getX(), position.getY(), questId};
}

// One giver site of the merged list: its position, the nearest distance of its points and the points in first-fit order.
struct Site
{
    Point at;
    float distance = 0.0f;
    std::vector<TravelPoint> points;
};
}

void RankQuestBundles(PartitionedTravelList& list, PlayerTravelInfo const& info)
{
    if (!info.HasQuestBundle() || list.empty())
        return;

    float const radius = float(sPlayerbotAIConfig.questBookBundleRadius);   // read only after the load
    float const speed = info.GetRunSpeed();
    uint32 const guid = info.GetQuestBookGuid();
    questsource::Params const p{};

    // Walk the list in first-fit order (nearest partition first, list order inside). Points that are no quest giver keep their place;
    // the giver points are grouped by site. Past MaxSites a new site is not ranked: its points follow the ranked ones in their partition.
    PartitionedTravelList kept;
    std::vector<Point> givers;
    std::vector<Site> sites;
    std::vector<std::pair<uint32, TravelPoint>> unranked;
    for (auto& [partition, points] : list)
    {
        for (TravelPoint const& point : points)
        {
            auto const* giver = GiverOf(std::get<0>(point), std::get<1>(point));
            if (!giver)
            {
                kept[partition].push_back(point);
                continue;
            }
            Point const at = PointOf(*std::get<1>(point), giver->GetQuestId());
            givers.push_back(at);
            auto site = std::find_if(sites.begin(), sites.end(), [&at](Site const& s) { return SameSite(s.at, at); });
            if (site == sites.end())
            {
                if (sites.size() >= MaxSites)
                {
                    unranked.emplace_back(partition, point);
                    continue;
                }
                sites.push_back(Site{at, std::get<2>(point), {}});
                site = std::prev(sites.end());
            }
            site->distance = std::min(site->distance, std::get<2>(point));
            site->points.push_back(point);
        }
    }
    // One site or none: nothing changes.
    if (sites.size() < 2)
        return;

    std::vector<float> scores;
    scores.reserve(sites.size());
    for (Site const& s : sites)
        scores.push_back(Score(BundleSize(s.at, givers, radius), questsource::RouteSec(s.distance, speed, p)));
    std::vector<std::size_t> const order = Order(scores);
    questbook::Note(guid, questbook::Count::BundleRanked);
    if (order.front() != 0)
        questbook::Note(guid, questbook::Count::BundleRegroup);
    if (!unranked.empty())
        questbook::Note(guid, questbook::Count::BundleCapped);

    // The sites in score order, after what the nearest partition already holds (turn-ins and objectives stay first).
    uint32 const nearest = list.begin()->first;
    for (std::size_t const i : order)
        for (TravelPoint const& point : sites[i].points)
            kept[nearest].push_back(point);
    for (auto const& [partition, point] : unranked)
        kept[partition].push_back(point);
    list.swap(kept);
}

void NotePick(Player* bot, PlayerbotAI* ai, TravelDestination const* destination, WorldPosition const* position,
    PartitionedTravelList const& choices)
{
    if (!sPlayerbotAIConfig.questBookBundle || !bot || !ai || !ai->ActiveQuestBook() ||
        !questbook::Applies(sPlayerbotAIConfig.questBookBundle, bot->GetGUIDLow()))
        return;
    auto const* giver = GiverOf(destination, position);
    if (!giver)
        return;

    // The bundle recounted from the choice list with the same rule (QuestBundlePolicy.h).
    float const radius = float(sPlayerbotAIConfig.questBookBundleRadius);
    Point const site = PointOf(*position, giver->GetQuestId());
    std::vector<Point> givers;
    std::vector<Point> sites;
    for (auto const& [partition, points] : choices)
    {
        for (TravelPoint const& point : points)
        {
            auto const* other = GiverOf(std::get<0>(point), std::get<1>(point));
            if (!other)
                continue;
            Point const at = PointOf(*std::get<1>(point), other->GetQuestId());
            givers.push_back(at);
            if (sites.size() < MaxSites &&
                std::none_of(sites.begin(), sites.end(), [&at](Point const& s) { return SameSite(s, at); }))
                sites.push_back(at);
        }
    }

    uint32 const guid = bot->GetGUIDLow();
    sLog.outBasic("[QuestBundle] bot=%s guid=%u cohort=%c quest=%u entry=%d bundle=%u sites=%u dist=%u", bot->GetName(), guid,
        cohort::Letter(guid), giver->GetQuestId(), giver->GetEntry(), BundleSize(site, givers, radius), uint32(sites.size()),
        uint32(position->distance(WorldPosition(bot))));
}
}
