
#include "playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage, not Player::GetPlayerbotAI
#include "FleeManager.h"
#include "CombatSpreadPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Group/Group.h"
#include "strategy/values/MoveStyleValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/PoolFleePolicy.h"

using namespace ai;

IdleMobVeto::IdleMobVeto(PlayerbotAI* ai, Player* bot, std::list<ObjectGuid> const& units, Unit* ignore, float stepLength)
{
    for (ObjectGuid const& guid : units)
    {
        if (count >= kMaxSleepers)
            break;
        Unit* unit = ai->GetUnit(guid);
        if (!unit || unit == ignore || !sServerFacade.IsAlive(unit) || unit->IsInCombat())
            continue;
        Creature* creature = unit->ToCreature();
        if (!creature || !unit->IsHostileTo(bot))
            continue;
        float const radius = creature->GetAttackDistance(bot);
        float const toBot = sServerFacade.getDistance2d(unit, bot);
        if (radius <= 0.0f || !PoolFleeMobInReach(toBot, radius, stepLength))
            continue;
        sleepers[count++] = { unit, radius, toBot };
    }
}

bool IdleMobVeto::Wakes(float x, float y) const
{
    for (int i = 0; i < count; ++i)
    {
        Sleeper const& sleeper = sleepers[i];
        if (PoolFleeLandingWakesMob(sServerFacade.getDistance2d(sleeper.unit, x, y), sleeper.toBot, sleeper.radius))
            return true;
    }
    return false;
}

void FleeManager::calculateDistanceToCreatures(FleePoint *point)
{
    point->minDistance = -1.0f;
    point->sumDistance = 0.0f;
    std::list<ObjectGuid> units = *PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("possible targets no los");
	for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); ++i)
    {
		Unit* unit = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(*i);
		if (!unit)
		    continue;

        // do not count non-LOS mobs
        if (!unit->IsWithinLOS(point->x, point->y, point->z + unit->GetCollisionHeight(), true))
            continue;

		float d = sServerFacade.getDistance2d(unit, point->x, point->y);
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
    Unit *target = *PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext()->GetValue<Unit*>("current target");

    float botPosX = startPosition.getX();
    float botPosY = startPosition.getY();
    float botPosZ = startPosition.getZ();

    FleePoint start(PlayerbotAIStorage::Instance().GetAI(bot), botPosX, botPosY, botPosZ);
    calculateDistanceToCreatures(&start);

    std::list<float> enemyOri;
    std::list<ObjectGuid> units = *PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("possible targets no los");
    for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); ++i)
    {
        Unit* unit = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(*i);
        if (!unit)
            continue;

        float ori = bot->GetAngle(unit);
        enemyOri.push_back(ori);
    }

    float distIncrement = std::max(sPlayerbotAIConfig.followDistance, (maxAllowedDistance - sPlayerbotAIConfig.tooCloseDistance) / 10.0f);
    for (float dist = maxAllowedDistance; dist >= sPlayerbotAIConfig.tooCloseDistance; dist -= distIncrement)
    {
        float angleIncrement = std::max(M_PI / 20, M_PI / 4 / (1.0 + dist - sPlayerbotAIConfig.tooCloseDistance));
        for (float add = 0.0f; add < M_PI / 4 + angleIncrement; add += angleIncrement)
        {
            for (float angle = add; angle < add + 2 * M_PI_F + angleIncrement; angle += M_PI_F / 4)
            {
                if (intersectsOri(angle, enemyOri, angleIncrement)) continue;

                float x = botPosX + cos(angle) * maxAllowedDistance, y = botPosY + sin(angle) * maxAllowedDistance, z = botPosZ + CONTACT_DISTANCE;
                if (MoveStyleValue::CheckForEdges(PlayerbotAIStorage::Instance().GetAI(bot)) && isTooCloseToEdge(x, y, z, angle)) continue;

                if (forceMaxDistance && sServerFacade.IsDistanceLessThan(sServerFacade.getDistance2d(bot, x, y), maxAllowedDistance - sPlayerbotAIConfig.tooCloseDistance))
                    continue;

                bot->UpdateAllowedPositionZ(x, y, z);

                const TerrainInfo* terrain = startPosition.getTerrain();
                if (terrain && terrain->IsInWater(x, y, z))
                    continue;

                if (/*!bot->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true) || */(target && !target->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true)))
                    continue;

                if (idleMobVeto && idleMobVeto->Wakes(x, y))
                    continue;

                FleePoint *point = new FleePoint(PlayerbotAIStorage::Instance().GetAI(bot), x, y, z);
                calculateDistanceToCreatures(point);

                if (sServerFacade.IsDistanceGreaterOrEqualThan(point->minDistance - start.minDistance, sPlayerbotAIConfig.followDistance))
                    points.push_back(point);
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

FleePoint* FleeManager::selectOptimalDestination(std::list<FleePoint*> &points,
    FleeFailureMemory const* failures, uint32 nowMs)
{
    FleePoint* best = nullptr;
    FleePoint* fallback = nullptr;
    for (FleePoint* point : points)
    {
        if (!fallback || isBetterThan(point, fallback))
            fallback = point;
        float const heading = startPosition.GetAngleTo(
            WorldPosition(startPosition.GetMapId(), point->x, point->y, point->z));
        if (failures && !failures->IsHeadingFree(heading, nowMs))
            continue;
        if (!best || isBetterThan(point, best))
            best = point;
    }
    // Failure memory must never leave a bot with no escape at all.
    return best ? best : fallback;
}

bool FleeManager::CalculateDestination(float* rx, float* ry, float* rz,
    FleeFailureMemory const* failures, uint32 nowMs)
{
    std::list<FleePoint*> points;
    calculatePossibleDestinations(points);

    FleePoint* point = selectOptimalDestination(points, failures, nowMs);
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
    bool const inAoe = PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext()->GetValue<bool>("has area debuff", "self target")->Get();
    if (!inAoe)
    {
        std::list<ObjectGuid> units = *PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("possible targets no los");
        for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); ++i)
        {
            Unit* unit = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(*i);
            if (unit)
            {
                float const distanceSquared = startPosition.sqDistance(WorldPosition(unit));
                float attackDistanceSquared = unit->GetCombatReach();
                attackDistanceSquared *= attackDistanceSquared;
                if (distanceSquared < attackDistanceSquared)
                {
                    return true;
                }

                //float d = sServerFacade.getDistance2d(unit, bot);
                //if (sServerFacade.IsDistanceLessThan(d, sPlayerbotAIConfig.aggroDistance)) return true;
            }
        }

        return false;
    }

    return true;
}
