#include "playerbot/GroupMembers.h"
#include "DungeonActions.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/CombatSpreadPolicy.h"
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

bool FindStep(PlayerbotAI* ai, Player* bot, const WorldPosition& from, float angle, float distance, WorldPosition& out)
{
    out = from + WorldPosition(0, distance * cos(angle), distance * sin(angle), 1.0f);
    out.setZ(out.GetHeight());
    if (!bot->IsWithinLOS(out.getX(), out.getY(), out.getZ() + bot->GetCollisionHeight()))
        return false;
    if (!from.canPathTo(out, bot))
        return false;
    // Do not step toward unpulled hostiles: reuse FleeManager's creature
    // danger sense (possible-targets distance). A candidate landing inside
    // aggro range of a non-victim hostile is rejected, so a 12 yd step-out
    // cannot drag the group into the next pack. LOS-gated and cheap: one
    // cached "possible targets" read, no scan or pathfinding per candidate.
    std::list<ObjectGuid> const& units = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("possible targets")->Get();
    for (ObjectGuid const& guid : units)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit) || unit == bot)
            continue;
        if (unit->GetVictim())
            continue;
        float d = sServerFacade.getDistance2d(unit, out.getX(), out.getY());
        if (sServerFacade.IsDistanceLessThan(d, sPlayerbotAIConfig.aggroDistance))
            return false;
    }
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
                if (FindStep(ai, bot, botPos, away + d, runout + extra, out) &&
                    MoveTo(bot->GetMapId(), out.getX(), out.getY(), out.getZ(), false, IsReaction(), false, true))
                    return true;
            }
        }
        return false;
    }
    // Solo carrier: radial flee still clears melee range.
    float angle = frand(0, M_PI_F * 2.0f);
    return FindStep(ai, bot, botPos, angle, runout, out) &&
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
    // The reaction engine fires this for any stacked ranged bot, but the
    // Onyxia fight strategy also queues it directly: re-check the spread
    // gate here so explicit hold orders and owned bots (live master OR owner
    // record, for offline masters) hold position instead of stepping out.
    if (!ShouldCombatSpread(sServerFacade.IsInCombat(bot), ai->HasRealPlayerMaster(),
        ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT)) ||
        IsSpreadExemptOwned(ai->HasRealPlayerMaster(), ai->IsOwnedBot()))
        return false;
    // One step per cooldown: stacked bots settle after a single step-out
    // instead of ping-ponging toward the next friendly every tick.
    LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");
    if (IsSpreadOnCooldown(WorldTimer::getMSTime(), lastMove.lastSpreadStepMs))
        return false;
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
    if (!nearest || nearestDist >= kSpreadSettledDistance)
        return false;
    const WorldPosition botPos(bot);
    const WorldPosition nearPos(nearest);
    float away = nearPos.GetAngleTo(botPos);
    // Donor "recently flee info" (mod-playerbots MovementAction::CheckLastFlee):
    // first pass skips step-out headings within ~45deg of the last two flee
    // destinations so repeated spreads fan out; the second pass takes vetoed
    // headings so a spread is never blocked outright.
    const float spread = sPlayerbotAIConfig.hazardEvasionDistance;
    const float angles[] = { 0.0f, 0.6f, -0.6f, 1.2f, -1.2f, (float)M_PI };
    WorldPosition out(botPos);
    for (int pass = 0; pass < 2; ++pass)
    {
        bool const vetoSecondPass = (pass == 1);
        for (float d : angles)
        {
            float heading = away + d;
            if (!vetoSecondPass && !IsFleeHeadingFree(heading, lastMove.lastFleeAngles, kFleeAngleSlots))
                continue;
            if (FindStep(ai, bot, botPos, heading, spread, out) &&
                MoveTo(bot->GetMapId(), out.getX(), out.getY(), out.getZ(), false, IsReaction(), false, true))
            {
                lastMove.lastFleeAngles[1] = lastMove.lastFleeAngles[0];
                lastMove.lastFleeAngles[0] = heading;
                if (lastMove.lastFleeAngleCount < 2)
                    ++lastMove.lastFleeAngleCount;
                lastMove.lastSpreadStepMs = WorldTimer::getMSTime();
                return true;
            }
        }
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