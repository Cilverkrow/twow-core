#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

// A3 step 2, quest book bundle scoring for accepting quests (QB-B; ADR-0041, OB-00 order 08.11.2026; AiPlayerbot.QuestBook.Bundle,
// default 0, acts only where the quest book is active). Plan: ws-10/cli-quest-organic/a3/step2-questbook/QB-BUILD-PLAN-20261107.md,
// section 11. Pure rules: numbers in, numbers out. No game types, no config reads, no locks, no log calls; the async quest fetch
// reads them over the bot-thread snapshot, the pick line on the bot thread over the choice list.
//
// A giver destination is one (quest, giver entry), so a giver spawn with three offerable quests is three points at one position.
// The first-fit choice takes the nearest giver point, whatever it offers. QB-B orders the giver sites by quests per second instead:
// the bundle of a site (the distinct quests among all giver points on its map within BundleRadius, 2D) over the route to it plus
// AcceptSeconds per quest. A lone giver at 100 yd and three quests at 300 yd tie (0.029); three quests at 250 yd win (0.031).
namespace ai::questbundle
{
constexpr float AcceptSeconds = 20.0f;          // talk + accept, per quest
constexpr std::uint32_t RadiusMin = 30;         // AiPlayerbot.QuestBook.BundleRadius, yards
constexpr std::uint32_t RadiusMax = 300;
constexpr std::uint32_t RadiusDefault = 100;
constexpr std::size_t MaxSites = 256;           // sites ranked per fetch, first-fit order; the rest follow unranked (bundle_capped)

inline std::uint32_t ClampRadius(std::int32_t value)
{
    if (value < std::int32_t(RadiusMin))
        return RadiusMin;
    if (value > std::int32_t(RadiusMax))
        return RadiusMax;
    return std::uint32_t(value);
}

// One giver point: its position and quest (0 = none, never counted).
struct Point
{
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t quest = 0;
};

// The same site: the points of one spawn share map and position exactly.
inline bool SameSite(Point const& a, Point const& b)
{
    return a.map == b.map && a.x == b.x && a.y == b.y;
}

// Within the bundle radius of the site: same map, 2D distance at most the radius.
inline bool Within(Point const& site, Point const& p, float radius)
{
    if (site.map != p.map)
        return false;
    float const dx = p.x - site.x;
    float const dy = p.y - site.y;
    float const r = radius > 0.0f ? radius : 0.0f;
    return dx * dx + dy * dy <= r * r;
}

// The bundle of a site: the distinct quests among the points within the radius.
inline std::uint32_t BundleSize(Point const& site, std::vector<Point> const& points, float radius)
{
    std::vector<std::uint32_t> quests;
    for (Point const& p : points)
        if (p.quest && Within(site, p, radius))
            quests.push_back(p.quest);
    std::sort(quests.begin(), quests.end());
    return std::uint32_t(std::unique(quests.begin(), quests.end()) - quests.begin());
}

// Quests per second: bundle / (route + AcceptSeconds x bundle). An empty bundle scores 0; a negative or NaN route counts as 0.
inline float Score(std::uint32_t bundle, float routeSec)
{
    if (!bundle)
        return 0.0f;
    float const route = routeSec > 0.0f ? routeSec : 0.0f;
    return float(bundle) / (route + AcceptSeconds * float(bundle));
}

// The site order: indices by score, highest first, stable on the first-fit order (equal scores keep it).
inline std::vector<std::size_t> Order(std::vector<float> const& scores)
{
    std::vector<std::size_t> order(scores.size());
    std::iota(order.begin(), order.end(), std::size_t(0));
    std::stable_sort(order.begin(), order.end(), [&scores](std::size_t a, std::size_t b) { return scores[a] > scores[b]; });
    return order;
}
}
