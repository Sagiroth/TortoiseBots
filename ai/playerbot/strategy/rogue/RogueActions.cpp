
#include "playerbot/playerbot.h"
#include "RogueActions.h"
#include "Maps/PathFinder.h"

using namespace ai;

namespace
{
    float SapGroundZ(Map* map, float x, float y, float refZ)
    {
        float const z = map->GetHeight(x, y, refZ + 2.0f, true);
        return z > INVALID_HEIGHT ? z : refZ;
    }
}

bool ReachSapAction::ApproachAroundPack(Unit* target, float standDist)
{
    using namespace TortoiseBots;

    // Never walk in visible: the trigger's Stealth runs first; with Stealth on
    // cooldown the rogue waits for it.
    if (!bot->HasAuraType(SPELL_AURA_MOD_STEALTH))
        return false;

    Map* map = bot->GetMap();
    uint32 const now = WorldTimer::getMSTime();
    SapPoint const here{bot->GetPositionX(), bot->GetPositionY()};
    SapPoint const targetAt{target->GetPositionX(), target->GetPositionY()};

    bool const waypointReached = plan.found && plan.viaWaypoint && SapDist(here, plan.waypoint) < 2.0f;
    if (planTarget != target->GetObjectGuid() || WorldTimer::getMSTimeDiff(planMs, now) > kSapReplanMs || waypointReached)
    {
        int32 const stealthSkill = bot->GetTotalAuraModifier(SPELL_AURA_MOD_STEALTH) +
            bot->GetTotalAuraModifier(SPELL_AURA_MOD_STEALTH_LEVEL);

        std::vector<SapPackMate> mates;
        float widest = 0.0f;
        for (Unit* unit : ai->GetAllHostileNPCNonPetUnitsAroundWO(target, kSapPackRadius))
        {
            if (unit == target || !unit->IsAlive())
                continue;
            // A stunned unit (a Sapped mate) detects nothing (CanDetectStealthOf).
            if (unit->HasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_PENDING_STUNNED))
                continue;

            int32 const detectorLevel = static_cast<int32>(unit->GetLevelForTarget(bot));
            int32 const levelDiff = detectorLevel - static_cast<int32>(bot->GetLevelForTarget(unit));
            float const detect = SapStealthDetectRange(detectorLevel,
                unit->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_STEALTH_DETECT, 0), stealthSkill, levelDiff);
            SapPackMate mate;
            mate.x = unit->GetPositionX();
            mate.y = unit->GetPositionY();
            mate.clearance = SapStealthClearance(detect,
                unit->GetObjectBoundingRadius() + bot->GetObjectBoundingRadius());
            widest = std::max(widest, mate.clearance + SapDist(SapPoint{mate.x, mate.y}, targetAt));
            mates.push_back(mate);
        }

        float const botZ = bot->GetPositionZ();
        float const targetZ = target->GetPositionZ();
        auto pathFn = [&](SapPoint const& from, SapPoint const& to, SapPath& out)
        {
            bool const fromBot = SapDist(from, here) < 0.01f;
            float const fromZ = fromBot ? botZ : SapGroundZ(map, from.x, from.y, targetZ);
            PathFinder path(bot);
            path.calculate(Vector3(from.x, from.y, fromZ), Vector3(to.x, to.y, SapGroundZ(map, to.x, to.y, targetZ)), false);
            if (path.getPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
                return false;
            for (auto const& p : path.getPath())
                out.push_back(SapPoint{p.x, p.y});
            return !out.empty();
        };

        // The detour ring runs outside the widest clearance of the pack.
        float const waypointRadius = std::max(standDist, widest) + kSapClearanceMargin;
        plan = PlanSapApproach(here, targetAt, standDist, waypointRadius, mates, pathFn);
        planTarget = target->GetObjectGuid();
        planMs = now;

        if (!plan.found)
        {
            sLog.outBasic("TortoiseBots: %s skips Sap on %s (entry %u): no approach stays clear of its %u pack-mates",
                bot->GetName(), target->GetName(), target->GetEntry(), static_cast<uint32>(mates.size()));
            context->GetValue<int32>("manual int", kSapSkipTargetKey)->Set(static_cast<int32>(target->GetObjectGuid().GetCounter()));
            context->GetValue<int32>("manual int", kSapSkipUntilKey)->Set(static_cast<int32>(now + kSapSkipMs));
            planTarget = ObjectGuid();
            return false;
        }
    }

    SapPoint const dest = plan.viaWaypoint ? plan.waypoint : plan.stand;
    return MoveTo(bot->GetMapId(), dest.x, dest.y, SapGroundZ(map, dest.x, dest.y, target->GetPositionZ()));
}
