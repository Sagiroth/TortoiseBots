#include "playerbot/GroupMembers.h"
#include "DungeonActions.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/PlayerbotAI.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool MoveAwayFromHazard::Execute(Event& event)
{
    const std::list<HazardPosition>& hazards = AI_VALUE(std::list<HazardPosition>, "hazards");

    // Get the closest hazard to move away from
    const HazardPosition* closestHazard = nullptr;
    float closestHazardDistance = 9999.0f;
    for (const HazardPosition& hazard : hazards)
    {
        const WorldPosition& hazardPosition = hazard.first;
        const float distance = bot->GetDistance(hazardPosition.getX(), hazardPosition.getY(), hazardPosition.getZ());
        if (distance < closestHazardDistance)
        {
            closestHazardDistance = distance;
            closestHazard = &hazard;
        }
    }

    if (closestHazard)
    {
        // Check if the bot is inside the closest hazard
        const float hazardRadius = closestHazard->second;
        if (closestHazardDistance <= hazardRadius)
        {
            float angle = 0.0f;
            const WorldPosition initialPosition(closestHazard->first);
            const float distance = frand(hazardRadius, hazardRadius * 1.5f);

            Unit* currentTarget = AI_VALUE(Unit*, "current target");
            if (currentTarget)
            {
                const int8 startDir = urand(0, 1) * 2 - 1;
                const WorldPosition targetPosition(currentTarget);
                angle = targetPosition.GetAngleTo(initialPosition) + (0.5 * M_PI_F * startDir);
            }
            else
            {
                angle = frand(0, M_PI_F * 2.0f);
            }

            const uint8 attempts = 10;
            float angleIncrement = (float)((2 * M_PI) / attempts);

            for (uint8 i = 0; i < attempts; i++)
            {
                WorldPosition point = initialPosition + WorldPosition(0, distance * cos(angle), distance * sin(angle), 1.0f);
                point.setZ(point.GetHeight());

                // Check if the point is not near other hazards
                if (!IsHazardNearby(point, hazards))
                {
                    if (bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()) && initialPosition.canPathTo(point, bot))
                    {
                        if (ai->HasStrategy("debug move", BotState::BOT_STATE_COMBAT))
                        {
                            bot->SummonCreature(15631, point.getX(), point.getY(), point.getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);
                        }

                        if (MoveTo(bot->GetMapId(), point.getX(), point.getY(), point.getZ(), false, IsReaction(), false, true))
                        {
                            if (IsReaction())
                            {
                                WaitForReach(point.distance(initialPosition));
                            }

                            return true;
                        }
                    }
                }

                if (ai->HasStrategy("debug move", BotState::BOT_STATE_COMBAT))
                {
                    bot->SummonCreature(1, point.getX(), point.getY(), point.getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);
                }

                angle += angleIncrement;
            }
        }
    }

    return false;
}

bool MoveAwayFromHazard::isPossible()
{
    if (MovementAction::isPossible())
    {
        return ai->CanMove();
    }

    return false;
}

bool MoveAwayFromHazard::IsHazardNearby(const WorldPosition& point, const std::list<HazardPosition>& hazards) const
{
    for (const HazardPosition& hazard : hazards)
    {
        const float hazardRange = hazard.second;
        const WorldPosition& hazardPosition = hazard.first;
        const float distance = point.distance(hazardPosition);
        if (distance < hazardRange)
        {
            return true;
        }
    }

    return false;
}

bool MoveAwayFromCreature::Execute(Event& event)
{
    // Get the active attacking creatures
    std::list<Creature*> creatures;
    size_t closestCreatureIdx = 0;
    float closestCreatureDistance = 9999.0f;

    // Iterate through the near creatures
    std::list<Unit*> units;
    MaNGOS::AllCreaturesOfEntryInRange u_check(bot, creatureID, range);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(units, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
    for (Unit* unit : units)
    {
        Creature* creature = (Creature*)unit;
        if (creature)
        {
            creatures.push_back(creature);

            // Get the closest creature to the bot
            const float distance = bot->GetDistance(creature);
            if (distance < closestCreatureDistance)
            {
                closestCreatureDistance = distance;
                closestCreatureIdx = creatures.size() - 1;
            }
        }
    }

    if (creatures.empty())
    {
        return false;
    }

    const std::list<HazardPosition>& hazards = AI_VALUE(std::list<HazardPosition>, "hazards");

    // Get the closest creature reference
    auto it = creatures.begin();
    advance(it, closestCreatureIdx);
    Creature* closestCreature = *it;
    // Remove the closest creature from the list to prevent checking it twice
    creatures.erase(it);

    // Generate the initial angle directly behind the bot looking at the closest creature
    const WorldPosition botPosition(bot);
    const WorldPosition creaturePosition(closestCreature);
    float angleLeft = creaturePosition.GetAngleTo(botPosition);
    float angleRight = angleLeft;

    const uint8 attempts = 20;
    const uint8 halfAtempts = (uint8)(attempts * 0.5f);
    float angleIncrement = (float)((M_PI) / halfAtempts);

    const float sizeFactor = bot->GetCombatReach() + closestCreature->GetCombatReach();
    const float distance = (range + sizeFactor);

    for (uint8 i = 0; i < halfAtempts; i++)
    {
        WorldPosition* validPoint = nullptr;

        // Calculate a point to the left and right
        WorldPosition pointLeft = creaturePosition + WorldPosition(0, distance * cos(angleLeft), distance * sin(angleLeft), 1.0f);
        pointLeft.setZ(pointLeft.GetHeight());
        WorldPosition pointRight = creaturePosition + WorldPosition(0, distance * cos(angleRight), distance * sin(angleRight), 1.0f);
        pointRight.setZ(pointRight.GetHeight());

        if (IsValidPoint(pointLeft, creatures, hazards))
        {
            validPoint = &pointLeft;
        }
        else if (IsValidPoint(pointRight, creatures, hazards))
        {
            validPoint = &pointRight;
        }

        if (validPoint)
        {
            if (ai->HasStrategy("debug move", BotState::BOT_STATE_COMBAT))
            {
                bot->SummonCreature(15631, validPoint->getX(), validPoint->getY(), validPoint->getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);
            }

            if (MoveTo(bot->GetMapId(), validPoint->getX(), validPoint->getY(), validPoint->getZ(), false, IsReaction(), false, true))
            {
                if (IsReaction())
                {
                    WaitForReach(validPoint->distance(botPosition));
                }

                return true;
            }
        }

        if (ai->HasStrategy("debug move", BotState::BOT_STATE_COMBAT))
        {
            bot->SummonCreature(1, pointLeft.getX(), pointLeft.getY(), pointLeft.getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);
            bot->SummonCreature(1, pointRight.getX(), pointRight.getY(), pointRight.getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);
        }

        angleLeft += angleIncrement;
        angleRight -= angleIncrement;
    }

    return false;
}

bool MoveAwayFromCreature::isPossible()
{
    if (MovementAction::isPossible())
    {
        return ai->CanMove();
    }

    return false;
}

namespace
{
// Raid anchor: master when present, else the nearest live group member,
// else null (caller falls back to radial flee).
Unit* RaidAnchor(PlayerbotAI* ai, Player* bot)
{
    Player* master = ai->GetMaster();
    if (master && master != bot && master->IsInWorld() && !master->IsBeingTeleported() &&
        sServerFacade.IsAlive(master) && master->GetMapId() == bot->GetMapId())
        return master;
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;
    Unit* nearest = nullptr;
    float nearestDist = FLT_MAX;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !sServerFacade.IsAlive(member))
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        float dist = sServerFacade.getDistance2d(bot, member);
        if (dist < nearestDist)
        {
            nearestDist = dist;
            nearest = member;
        }
    }
    return nearest;
}

bool FindStep(Player* bot, const WorldPosition& from, float angle, float distance, WorldPosition& out)
{
    out = from + WorldPosition(0, distance * cos(angle), distance * sin(angle), 1.0f);
    out.setZ(out.GetHeight());
    if (!bot->IsWithinLOS(out.getX(), out.getY(), out.getZ() + bot->GetCollisionHeight()))
        return false;
    if (!from.canPathTo(out, bot))
        return false;
    return true;
}
} // namespace

bool RaidBombRunoutAction::Execute(Event& event)
{
    (void)event;
    // 30yd clear of the raid anchor: Geddon/Vael/Grobbulus detonations
    // bracket the whole clump otherwise. Keep running while the aura lives;
    // the trigger re-fires each tick until it expires or detonates.
    const float runout = sPlayerbotAIConfig.bombRunoutDistance;
    const WorldPosition botPos(bot);
    WorldPosition out(botPos);
    if (Unit* anchor = RaidAnchor(ai, bot))
    {
        const WorldPosition anchorPos(anchor);
        float away = anchorPos.GetAngleTo(botPos);
        const float angles[] = { 0.0f, 0.5f, -0.5f };
        for (float extra = 0.0f; extra <= 10.0f; extra += 5.0f)
        {
            for (float d : angles)
            {
                if (FindStep(bot, botPos, away + d, runout + extra, out) &&
                    MoveTo(bot->GetMapId(), out.getX(), out.getY(), out.getZ(), false, IsReaction(), false, true))
                    return true;
            }
        }
        return false;
    }
    // Solo carrier: radial flee still clears melee range.
    float angle = frand(0, M_PI_F * 2.0f);
    return FindStep(bot, botPos, angle, runout, out) &&
           MoveTo(bot->GetMapId(), out.getX(), out.getY(), out.getZ(), false, IsReaction(), false, true);
}

bool DragonFlankAction::Execute(Event& event)
{
    (void)event;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !sServerFacade.IsAlive(target))
        return false;
    const WorldPosition bossPos(target);
    const WorldPosition botPos(bot);
    float facing = target->GetOrientation();
    // Flank = 90 degrees off the facing axis; pick the nearer side.
    float toBot = bossPos.GetAngleTo(botPos);
    float side = (toBot - facing) > 0 ? (facing + M_PI_F / 2.0f) : (facing - M_PI_F / 2.0f);
    const float dist = std::max(botPos.distance(bossPos), 8.0f);
    for (int i = 0; i < 4; ++i)
    {
        WorldPosition point = bossPos + WorldPosition(0, dist * cos(side), dist * sin(side), 1.0f);
        point.setZ(point.GetHeight());
        if (bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()) &&
            bossPos.canPathTo(point, bot) &&
            MoveTo(bot->GetMapId(), point.getX(), point.getY(), point.getZ(), false, IsReaction(), false, true))
            return true;
        side += M_PI_F / 2.0f;
    }
    return false;
}

bool RaidSpreadAction::Execute(Event& event)
{
    (void)event;
    // Step 12yd directly away from the nearest stacked friendly.
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    Player* nearest = nullptr;
    float nearestDist = FLT_MAX;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !sServerFacade.IsAlive(member))
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        float dist = sServerFacade.getDistance2d(bot, member);
        if (dist < nearestDist)
        {
            nearestDist = dist;
            nearest = member;
        }
    }
    if (!nearest || nearestDist >= 10.0f)
        return false;
    const WorldPosition botPos(bot);
    const WorldPosition nearPos(nearest);
    float away = nearPos.GetAngleTo(botPos);
    const float spread = sPlayerbotAIConfig.hazardEvasionDistance;
    const float angles[] = { 0.0f, 0.6f, -0.6f };
    WorldPosition out(botPos);
    for (float d : angles)
    {
        if (FindStep(bot, botPos, away + d, spread, out) &&
            MoveTo(bot->GetMapId(), out.getX(), out.getY(), out.getZ(), false, IsReaction(), false, true))
            return true;
    }
    return false;
}

bool DragonTankFaceAwayAction::Execute(Event& event)
{
    (void)event;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !sServerFacade.IsAlive(target))
        return false;
    // Drag the boss through the bot so its head points away from the raid
    // anchor: destination is the far side of the bot from the anchor.
    Unit* anchor = RaidAnchor(ai, bot);
    if (!anchor)
        return false;
    const WorldPosition botPos(bot);
    const WorldPosition anchorPos(anchor);
    float away = anchorPos.GetAngleTo(botPos);
    const WorldPosition bossPos(target);
    float pullDist = std::min(botPos.distance(bossPos) + 6.0f, 20.0f);
    WorldPosition point = botPos + WorldPosition(0, pullDist * cos(away), pullDist * sin(away), 1.0f);
    point.setZ(point.GetHeight());
    if (!bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()))
        return false;
    if (!botPos.canPathTo(point, bot))
        return false;
    return MoveTo(bot->GetMapId(), point.getX(), point.getY(), point.getZ(), false, IsReaction(), false, true);
}

bool MoveAwayFromCreature::IsValidPoint(const WorldPosition& point, const std::list<Creature*>& creatures, const std::list<HazardPosition>& hazards)
{
    // Check if the point is not near other game objects
    if (!HasCreaturesNearby(point, creatures) && !IsHazardNearby(point, hazards))
    {
        if (bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()))
        {
            const WorldPosition botPosition(bot);
            return botPosition.canPathTo(point, bot);
        }
    }

    return false;
}

bool MoveAwayFromCreature::HasCreaturesNearby(const WorldPosition& point, const std::list<Creature*>& creatures) const
{
    for (const Creature* creature : creatures)
    {
        const float distance = creature->GetDistance(point.getX(), point.getY(), point.getZ());
        if (distance <= range)
        {
            return true;
        }
    }

    return false;
}

bool MoveAwayFromCreature::IsHazardNearby(const WorldPosition& point, const std::list<HazardPosition>& hazards) const
{
    for (const HazardPosition& hazard : hazards)
    {
        const float hazardRange = hazard.second;
        const WorldPosition& hazardPosition = hazard.first;
        const float distance = point.distance(hazardPosition);
        if (distance < hazardRange)
        {
            return true;
        }
    }

    return false;
}