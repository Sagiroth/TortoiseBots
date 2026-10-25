#include "playerbot/playerbot.h"
#include "ReachTargetActions.h"
#include "OnyxiasLairDungeonActions.h"
#include "playerbot/OnyxiaAirPolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "Movement/spline/MoveSplineInit.h"

using namespace ai;

bool ReachTargetAction::ReachAirborneOnyxia(Unit* target)
{
    if (!IsOnyxiaAirCombatTarget(ai, target) || !ai->CanMove() || bot->GetTransport() ||
        bot->IsTaxiFlying() || bot->IsFalling() || OnyxiaBreathActive(ai) ||
        bot->IsNonMeleeSpellCasted(false, true, true)) return false;
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto const hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", ai->GetState()) || ai->HasStrategy("follow", ai->GetState()) ||
        ai->HasStrategy("wait for attack", ai->GetState()) ||
        (hold != positions.end() && hold->second.isSet())) return false;
    if (bot->GetCombatDistance(target) <= range && bot->IsWithinLOSInMap(target, true)) return false;
    if (!bot->movespline->Finalized()) return true;

    uint32 const now = WorldTimer::getMSTime();
    if (lastOnyxiaAirRoute && uint32(now - lastOnyxiaAirRoute) < 1000) return false;
    lastOnyxiaAirRoute = now;
    WorldPosition const start(bot);
    if (!OnyxiaSpreadFloor(start.getX(), start.getY(), start.getZ())) return false;
    std::vector<WorldPosition> candidates;
    // Project onto the central floor, not the boss's elevated spline endpoint.
    // The core's spell range uses combat reach, including Onyxia's flight reach.
    for (float x = -53.0f; x <= 3.0f; x += 4.0f)
        for (float y = -235.0f; y <= -195.0f; y += 4.0f)
        {
            WorldPosition point(249, x, y, start.getZ());
            point.setZ(point.GetHeight());
            if (!OnyxiaSpreadFloor(x, y, point.getZ()) || IsHazardNearPosition(point) ||
                point.distance(WorldPosition(target)) - bot->GetCombatReach() - target->GetCombatReach() > range - 1.0f ||
                !bot->IsWithinLOSAtPosition(x, y, point.getZ(), target->GetPositionX(),
                    target->GetPositionY(), target->GetPositionZ(), true, target->GetCollisionHeight())) continue;
            candidates.push_back(point);
        }
    std::sort(candidates.begin(), candidates.end(), [&start](WorldPosition const& a, WorldPosition const& b)
        { return start.distance(a) < start.distance(b); });
    // Bound expensive path searches. A failed route never falls back to chasing
    // an airborne XYZ or starts a generic unreachable/drop-target timer.
    size_t attempts = 0;
    for (WorldPosition const& point : candidates)
    {
        if (++attempts > 8) break;
        std::vector<WorldPosition> const path = start.GetPathTo(point, bot);
        if (path.size() < 2 || !point.isPathTo(path, 1.0f, 1.0f)) continue;
        bool safe = true;
        WorldPosition previous = start;
        for (WorldPosition const& next : path)
        {
            unsigned const steps = std::max(1u, unsigned(std::ceil(previous.distance(next) / 2.0f)));
            for (unsigned i = 0; i <= steps; ++i)
            {
                float const t = float(i) / steps;
                WorldPosition sample(249, previous.getX() + t * (next.getX() - previous.getX()),
                    previous.getY() + t * (next.getY() - previous.getY()),
                    previous.getZ() + t * (next.getZ() - previous.getZ()));
                if (!OnyxiaSpreadFloor(sample.getX(), sample.getY(), sample.getZ()) ||
                    !IsMovingAllowed(249, sample.getX(), sample.getY(), sample.getZ()) || IsHazardNearPosition(sample))
                { safe = false; break; }
            }
            if (!safe) break;
            previous = next;
        }
        if (!safe) continue;
        Movement::PointsArray points;
        for (WorldPosition const& step : path) points.emplace_back(step.getX(), step.getY(), step.getZ());
        Movement::MoveSplineInit init(*bot, "onyxia air reach");
        init.MovebyPath(points);
        init.SetWalk(false);
        init.SetFacingGUID(target->GetObjectGuid().GetRawValue());
        init.Launch();
        return true;
    }
    return false;
}
