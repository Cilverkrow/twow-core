
#include "playerbot.h"
#include "FleeManager.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Group/Group.h"
#include "strategy/values/MoveStyleValue.h"
#include "playerbot/ServerFacade.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <unordered_map>

using namespace ai;

namespace
{
    // twow-repo#541 (audit A01, AiPlayerbot.Perf.FleeMemo): outcome of one flee candidate inside ONE
    // calculatePossibleDestinations call. x/y use maxAllowedDistance (not dist), so a candidate
    // depends only on its angle; the dist rings repeat the same float angles bit for bit.
    // stampSecond = time(0) read BEFORE the evaluation: "possible targets no los" (a
    // CalculatedValue, checkInterval 2) can only recompute when time(0) moves to a new second,
    // so an entry is reused only within the same second it was computed in.
    struct FleeMemoEntry
    {
        time_t stampSecond = 0;
        bool accepted = false;   // false = rejected (edge, forceMaxDistance, water, target LOS, minDistance)
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float minDistance = 0.0f;
        float sumDistance = 0.0f;
    };
}

void FleeManager::calculateDistanceToCreatures(FleePoint *point)
{
    point->minDistance = -1.0f;
    point->sumDistance = 0.0f;
    std::list<ObjectGuid> units = *GetBotAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("possible targets no los");
	for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); ++i)
    {
		Unit* unit = GetBotAI(bot)->GetUnit(*i);
		if (!unit)
		    continue;

        // do not count non-LOS mobs
        if (!unit->IsWithinLOS(point->x, point->y, point->z + unit->GetCollisionHeight(), true))
            continue;

		float d = sServerFacade.GetDistance2d(unit, point->x, point->y);
        point->sumDistance += d;
        if (point->minDistance < 0 || point->minDistance > d) point->minDistance = d;
	}
}

bool intersectsOri(float angle, std::list<float>& angles, float angleIncrement)
{
    for (std::list<float>::iterator i = angles.begin(); i != angles.end(); ++i)
    {
        float ori = *i;
        if (abs(angle - ori) < angleIncrement) return true;
    }

    return false;
}

void FleeManager::calculatePossibleDestinations(std::list<FleePoint*> &points)
{
    Unit *target = *GetBotAI(bot)->GetAiObjectContext()->GetValue<Unit*>("current target");

    float botPosX = startPosition.getX();
    float botPosY = startPosition.getY();
    float botPosZ = startPosition.getZ();
    
    FleePoint start(GetBotAI(bot), botPosX, botPosY, botPosZ);
    calculateDistanceToCreatures(&start);

    std::list<float> enemyOri;
    std::list<ObjectGuid> units = *GetBotAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("possible targets no los");
    for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); ++i)
    {
        Unit* unit = GetBotAI(bot)->GetUnit(*i);
        if (!unit)
            continue;

        float ori = bot->GetAngle(unit);
        enemyOri.push_back(ori);
    }

    // twow-repo#541 (audit A01): call-local memo, never shared, never static (see FleeMemoEntry).
    const bool fleeMemo = sPlayerbotAIConfig.perfFleeMemo;
    std::unordered_map<std::uint32_t, FleeMemoEntry> memo;
    if (fleeMemo)
        memo.reserve(128);

    float distIncrement = std::max(sPlayerbotAIConfig.followDistance, (maxAllowedDistance - sPlayerbotAIConfig.tooCloseDistance) / 10.0f);
    for (float dist = maxAllowedDistance; dist >= sPlayerbotAIConfig.tooCloseDistance; dist -= distIncrement)
    {
        float angleIncrement = std::max(M_PI / 20, M_PI / 4 / (1.0 + dist - sPlayerbotAIConfig.tooCloseDistance));
        for (float add = 0.0f; add < M_PI / 4 + angleIncrement; add += angleIncrement)
        {
            for (float angle = add; angle < add + 2 * M_PI_F + angleIncrement; angle += M_PI_F / 4)
            {
                if (intersectsOri(angle, enemyOri, angleIncrement)) continue;

                // twow-repo#541 (audit A01): intersectsOri above stays per iteration (it depends on this
                // ring's angleIncrement); everything below depends only on the angle bits.
                FleeMemoEntry* memoSlot = nullptr;
                if (fleeMemo)
                {
                    const time_t memoNow = time(0);
                    std::uint32_t memoKey;
                    static_assert(sizeof(memoKey) == sizeof(angle), "flee memo key must hold the exact float bits");
                    std::memcpy(&memoKey, &angle, sizeof(memoKey));
                    auto memoIt = memo.find(memoKey);
                    if (memoIt != memo.end() && memoIt->second.stampSecond == memoNow)
                    {
                        const FleeMemoEntry& memoHit = memoIt->second;
                        if (memoHit.accepted)
                        {
                            FleePoint *point = new FleePoint(GetBotAI(bot), memoHit.x, memoHit.y, memoHit.z);
                            point->minDistance = memoHit.minDistance;
                            point->sumDistance = memoHit.sumDistance;
                            points.push_back(point);
                        }
                        continue;
                    }
                    memoSlot = &memo[memoKey];
                    *memoSlot = FleeMemoEntry();
                    memoSlot->stampSecond = memoNow;
                }

                float x = botPosX + cos(angle) * maxAllowedDistance, y = botPosY + sin(angle) * maxAllowedDistance, z = botPosZ + CONTACT_DISTANCE;
                if (MoveStyleValue::CheckForEdges(GetBotAI(bot)) && isTooCloseToEdge(x, y, z, angle)) continue;

                if (forceMaxDistance && sServerFacade.IsDistanceLessThan(sServerFacade.GetDistance2d(bot, x, y), maxAllowedDistance - sPlayerbotAIConfig.tooCloseDistance))
                    continue;

                bot->UpdateAllowedPositionZ(x, y, z);

                const TerrainInfo* terrain = startPosition.getTerrain();
                if (terrain && terrain->IsInWater(x, y, z))
                    continue;

                if (/*!bot->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true) || */(target && !target->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true)))
                    continue;

                FleePoint *point = new FleePoint(GetBotAI(bot), x, y, z);
                calculateDistanceToCreatures(point);

                if (sServerFacade.IsDistanceGreaterOrEqualThan(point->minDistance - start.minDistance, sPlayerbotAIConfig.followDistance))
                {
                    if (memoSlot)
                    {
                        memoSlot->accepted = true;
                        memoSlot->x = point->x;
                        memoSlot->y = point->y;
                        memoSlot->z = point->z;
                        memoSlot->minDistance = point->minDistance;
                        memoSlot->sumDistance = point->sumDistance;
                    }
                    points.push_back(point);
                }
                else
                    delete point;
            }
        }
	}
}

bool FleeManager::isTooCloseToEdge(float x, float y, float z, float angle)
{
    Map* map = bot->GetMap();
    const TerrainInfo* terrain = map->GetTerrain();
    for (double a = angle; a <= angle + 2*M_PI; a += M_PI / 4)
    {
        float dist = sPlayerbotAIConfig.followDistance;
        float tx = x + cos(a) * dist;
        float ty = y + sin(a) * dist;
        float tz = z;
        bot->UpdateAllowedPositionZ(tx, ty, tz);

        if (terrain && terrain->IsInWater(tx, ty, tz))
            return true;

        if (!bot->IsWithinLOS(tx, ty, tz))
            return true;
    }

    return false;
}

void FleeManager::cleanup(std::list<FleePoint*> &points)
{
	for (std::list<FleePoint*>::iterator i = points.begin(); i != points.end(); i++)
    {
		FleePoint* point = *i;
		delete point;
	}
	points.clear();
}

bool FleeManager::isBetterThan(FleePoint* point, FleePoint* other)
{
    return point->sumDistance - other->sumDistance > 0;
}

FleePoint* FleeManager::selectOptimalDestination(std::list<FleePoint*> &points)
{
	FleePoint* best = NULL;
	for (std::list<FleePoint*>::iterator i = points.begin(); i != points.end(); i++)
    {
		FleePoint* point = *i;
        if (!best || isBetterThan(point, best))
            best = point;
	}

	return best;
}

bool FleeManager::CalculateDestination(float* rx, float* ry, float* rz)
{
    std::list<FleePoint*> points;
	calculatePossibleDestinations(points);

    FleePoint* point = selectOptimalDestination(points);
    if (!point)
    {
        cleanup(points);
        return false;
    }

	*rx = point->x;
	*ry = point->y;
	*rz = point->z;

    cleanup(points);
	return true;
}

bool FleeManager::isUseful()
{
    // It the bot is a victim of an aoe attack it should move no matter the target attack distance
    bool const inAoe = GetBotAI(bot)->GetAiObjectContext()->GetValue<bool>("has area debuff", "self target")->Get();
    if (!inAoe)
    {
        std::list<ObjectGuid> units = *GetBotAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("possible targets no los");
        for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); ++i)
        {
            Unit* unit = GetBotAI(bot)->GetUnit(*i);
            if (unit)
            {
                float const distanceSquared = startPosition.sqDistance(WorldPosition(unit));
                float attackDistanceSquared = unit->GetAttackDistance(bot);
                attackDistanceSquared *= attackDistanceSquared;
                if (distanceSquared < attackDistanceSquared)
                {
                    return true;
                }

                //float d = sServerFacade.GetDistance2d(unit, bot);
                //if (sServerFacade.IsDistanceLessThan(d, sPlayerbotAIConfig.aggroDistance)) return true;
            }
        }

        return false;
    }

    return true;
}
