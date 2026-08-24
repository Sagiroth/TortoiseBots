
#include "playerbot/playerbot.h"
#include "playerbot/PerformanceMonitor.h"
#include "MovementActions.h"
#include "Movement/MotionMaster.h"
#include "Movement/MovementGenerator.h"
#include "playerbot/FleeManager.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/Stances.h"
#include "Movement/TargetedMovementGenerator.h"
#include "playerbot/TravelMgr.h"
#include "Transports/Transport.h"
#include "playerbot/strategy/generic/CombatStrategy.h"

using namespace ai;

void MovementAction::CreateWp(Player* wpOwner, float x, float y, float z, float o, uint32 entry, bool important)
{
    float dist = wpOwner->getDistance(x, y, z);
    float delay = 1000.0f * dist / wpOwner->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.reactDelay;

    //if(!important)
    //    delay *= 0.25;

    Creature* wpCreature = wpOwner->SummonCreature(entry, x, y, z - 1, o, TEMPSPAWN_TIMED_DESPAWN, delay);
    ai->AddAura(wpCreature, 246);

    if (!important)
        wpCreature->SetObjectScale(0.5f);
}

bool MovementAction::isPossible()
{
    return ai->CanMove();
}

bool MovementAction::isUseful()
{
    return !ai->HasStrategy("stay", ai->GetState());
}

bool MovementAction::MoveNear(uint32 mapId, float x, float y, float z, float distance)
{
    float angle = GetFollowAngle();
    return MoveTo(mapId, x + cos(angle) * distance, y + sin(angle) * distance, z);
}

bool MovementAction::MoveNear(WorldObject* target, float distance)
{
    if (!target)
        return false;

#ifdef MANGOS
    distance += target->getObjectBoundingRadius();
#endif

    float x = target->getPositionX();
    float y = target->getPositionY();
    float z = target->getPositionZ();
    float followAngle = GetFollowAngle();
    for (float angle = followAngle; angle <= followAngle + 2 * M_PI; angle += M_PI_F / 4.0f)
    {
#ifdef CMANGOS
        float dist = distance + target->GetObjectBoundingRadius();
        target->GetNearPoint(bot, x, y, z, bot->GetObjectBoundingRadius(), std::min(dist, ai->GetRange("follow")), angle);
#endif
#ifdef MANGOS
        float x = target->getPositionX() + cos(angle) * distance,
             y = target->getPositionY()+ sin(angle) * distance,
             z = target->getPositionZ();
#endif
        if (!bot->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true))
            continue;
        bool moved = MoveTo(target->GetMapId(), x, y, z);
        if (moved)
            return true;
    }

    //ai->TellError("All paths not in LOS");
    return false;
}

bool MovementAction::FlyDirect(const WorldPosition &startPosition, const WorldPosition &endPosition, WorldPosition& movePosition, TravelPath movePath)
{
    //Fly directly.
    return false;
}

bool MovementAction::UseTaxi(PlayerbotAI* ai, uint32 entry, bool needNpc)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();

    TaxiPathEntry const* tEntry = sTaxiPathStore.LookupEntry(entry);

    if (!tEntry)
        return false;

    Creature* unit = nullptr;

    if (needNpc)
    {
        std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
        for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
        {
            unit = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_FLIGHTMASTER);
            if (unit)
                break;
        }

        if (!unit)
        {
            return false;
        }

        if (unit && !bot->m_taxi.IsTaximaskNodeKnown(tEntry->from))
        {
            bot->GetSession()->SendLearnNewTaxiNode(unit);

            unit->SetFacingTo(unit->GetAngle(bot));
        }
    }

    uint32 botMoney = bot->GetMoney();
    if (ai->HasCheat(BotCheatMask::gold) || ai->HasCheat(BotCheatMask::taxi))
    {
        bot->SetMoney(botMoney + tEntry->price);
    }

    bot->OnTaxiFlightEject(true);

    ai->Unmount();

    bool goTaxi = bot->ActivateTaxiPathTo({tEntry->from, tEntry->to}, unit, 1);

    if (!goTaxi)
        bot->SetMoney(botMoney);

    return goTaxi;
}

bool MovementAction::MoveOnTransport(PlayerbotAI* ai, GenericTransport* transport, bool doTeleport)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    WorldPosition botPos(bot);

    uint32 radius = 20;

    GenericTransport* botTrans = bot->GetTransport();

    std::vector<WorldPosition> path;

    WorldPosition transPos = botPos.RandomPointOnTrans(transport, 20.0f, doTeleport ? nullptr : bot, path);

    if (!transPos)
        return false;

    if (doTeleport)
    {
        bot->GetMap()->PlayerRelocation(bot, transPos.getX(), transPos.getY(), transPos.getZ(), bot->getOrientation());
        transport->AddPassenger(bot, true);
        // Boarding is the one leg of travel nothing recorded. setNewTarget writes an
        // event for every destination a bot picks - taker, giver, objective - but the
        // vessel it needs to get there was invisible in the log, so "do bots use boats
        // at all" could only be argued, never counted. It fires once per boarding, so
        // it costs nothing next to the rest of bot_events.csv.
        sPlayerbotAIConfig.logEvent(ai, "BoardTransport", transport->GetName(), std::to_string(transport->GetEntry()));
        bot->SendHeartBeat();
        return true;
    }

    bot->SetTransport(botTrans);

    if (path.empty())
    {
        path = WorldPosition(transport).GetPathStepFrom(botPos, bot);

        if (path.empty())
            return false;
    }
    else
    {
        transport->AddPassenger(bot, true);
        sPlayerbotAIConfig.logEvent(ai, "BoardTransport", transport->GetName(), std::to_string(transport->GetEntry()));

        ai->StopMoving();

        if (!bot->GetMotionMaster()->empty())
            if (MovementGenerator* movgen = bot->GetMotionMaster()->top())
                movgen->Interrupt(*bot);

        bot->SendHeartBeat();

        if (!bot->GetMotionMaster()->empty())
            if (MovementGenerator* movgen = bot->GetMotionMaster()->top())
                movgen->Reset(*bot);
    }

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        for (auto& p : path)
        {
            Creature* wpCreature = bot->SummonCreature(2334, p.getX(), p.getY(), p.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000.0f);

            transport->AddPassenger(wpCreature, true);

            wpCreature->NearTeleportTo(p.getX(), p.getY(), p.getZ(), wpCreature->getOrientation());

            ai->AddAura(wpCreature, 246);

            if (p == path.back())
                ai->AddAura(wpCreature, 1130);
        }
    }

    bot->GetMotionMaster()->Clear();

    std::vector<G3D::Vector3> pointPath = transPos.toPointsArray(path);
    bot->GetMotionMaster()->MovePath(pointPath, FORCED_MOVEMENT_RUN, false, false);

    return true;
}

bool MovementAction::MoveOffTransport(PlayerbotAI* ai, WorldPosition exitPos, bool doTeleport)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    WorldPosition botPos(bot);

    if (!bot->GetTransport())
    {
        return false;
    }

    GenericTransport* transport = bot->GetTransport();

    transport->RemovePassenger(bot);

    if (doTeleport)
    {
        bot->TeleportTo(exitPos.GetMapId(), exitPos.getX(), exitPos.getY(), exitPos.getZ(), exitPos.getO(), 0);
        return true;
    }

    bot->NearTeleportTo(bot->m_movementInfo.pos.x, bot->m_movementInfo.pos.y, bot->m_movementInfo.pos.z, bot->m_movementInfo.pos.o);

    std::vector<WorldPosition> path = WorldPosition(bot).GetPathStepFrom(exitPos, bot, false);

    if (path.empty())
    {
        return false;
    }

    if (exitPos.sqDistance(path.back()) > 5.0f)
    {
        return false;
    }

    bot->GetMotionMaster()->Clear();

    std::vector<G3D::Vector3> pointPath = exitPos.toPointsArray(path);
    bot->GetMotionMaster()->MovePath(pointPath, FORCED_MOVEMENT_RUN, false, false);

    return true;
}

bool MovementAction::UseTransport(PlayerbotAI* ai, uint32 entry, WorldPosition dockPosition, WorldPosition exitPosition, bool doTeleport)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    WorldPosition botPos(bot);

    GenericTransport* transport = bot->GetTransport();

    if (transport)
    {
        GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(transport->GetEntry());
        std::string transportName = transport->GetName();
        if (transportName.empty())
            transportName = data->name;

        if (dockPosition.mapId == bot->GetMapId() && dockPosition.sqDistance2d(transport) < INTERACTION_DISTANCE * INTERACTION_DISTANCE)
        {
            MoveOffTransport(ai, exitPosition, doTeleport);
            ai->TellDebug(ai->GetMaster(), "Leaving transport " + transportName, "debug move");
            return true;
        }

        if (urand(0, 50))
            MoveOnTransport(ai, transport, doTeleport);

        ai->TellDebug(ai->GetMaster(), "Waiting ontop of transport " + transportName + " at " + std::to_string((uint32)dockPosition.fDist(transport)) + "y from docking.", "debug move");

        return false;
    }

    float minDist = 0;

    std::string transportName;

    for (auto& trans : dockPosition.GetTransports(entry))
    {
        float distance = dockPosition.sqDistance2d(trans);

        if (minDist && distance > minDist)
            continue;

        transport = trans;
        minDist = distance;

        GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(transport->GetEntry());
        transportName = transport->GetName();
        if (transportName.empty())
            transportName = data->name;
    }

    if (transport && dockPosition.mapId == bot->GetMapId() && dockPosition.sqDistance2d(transport) < INTERACTION_DISTANCE * INTERACTION_DISTANCE)
    {
        MoveOnTransport(ai, transport, doTeleport);

        return true;
    }

    if (transportName.empty())
        ai->TellDebug(ai->GetMaster(), "Waiting for transport on different map.", "debug move");
    else
        ai->TellDebug(ai->GetMaster(), "Waiting for transport " + std::string(transportName) + " at " + std::to_string((uint32)sqrt(minDist)) + "y from docking.", "debug move");

    return false;
}

bool MovementAction::MinimalMove(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.enableMinimalMove)
        return false;

    auto pmo1 = sPerformanceMonitor.start(PERF_MON_ACTION, "minimalMove", ai);

    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

    if (bot->IsTaxiFlying())
        return false;

    if (lastMove.lastPath.empty())
        return false;

    time_t now = time(0);

    if (lastMove.nextTeleport > now)
        return false;

    lastMove.nextTeleport = now + sPlayerbotAIConfig.passiveDelay/1000; //For teleports/transports/ect

    std::vector<PathNodePoint>& path = lastMove.lastPath.GetPath();

    auto nextStep = path.begin();

    bool doDelay = true;

    //Taxi handling: Start taxi and remove path until it ends.
    if (nextStep->type == PathNodeType::NODE_FLIGHTPATH)
    {
        if (nextStep->point.sqDistance(bot) > INTERACTION_DISTANCE * INTERACTION_DISTANCE)
        {
            bot->TeleportTo(nextStep->point);

            return true;
        }

        bool didTaxi = UseTaxi(ai, nextStep->entry, false);

        for (auto& step : path)
        {
            if (step.type == PathNodeType::NODE_FLIGHTPATH && step.entry == nextStep->entry)
                continue;

            lastMove.lastPath.cutTo(step, false); //Remove path until next walk or taxi.
            break;
        }

        return true;
    }

   //Transport handling: If not on transport wait for transport and teleport on it when it's near (and cut to last transport point). If on transport wait until it is near exit and teleport to exit.
    if (nextStep != path.end() && nextStep->type == PathNodeType::NODE_TRANSPORT)
    {
        auto exitStep = std::next(nextStep);

        WorldPosition exitPos = (exitStep != path.end()) ? exitStep->point : nextStep->point;

        bool didTransport = UseTransport(ai, nextStep->entry, nextStep->point, exitPos, true);

        if (!didTransport) //We did not board yet or are on the transport so just wait.
        {
            return true;
        }

        if (bot->GetTransport()) //Just boarded
        {
            PathNodePoint lastStep = *nextStep;

            for (auto& step : path)
            {
                if (step.type == PathNodeType::NODE_TRANSPORT && step.entry == nextStep->entry)
                {
                    lastStep = step;
                    continue;
                }

                break;
            }

            lastMove.lastPath.cutTo(lastStep, false); //Remove path up to last transport point.

            return true;
        }

        //Ready to exit
        lastMove.lastPath.cutTo(*nextStep, true); //Removing boarding point.

        nextStep = path.begin();

        bot->TeleportTo(nextStep->point);

        return true;
    }

    //Skip over stuff we don't walk.
    if (!nextStep->IsWalkable())
    {
        auto it = std::find_if(std::next(nextStep), path.end(), [](const auto& step) {
            return step.isWalkable();
        });

        if (it != path.end())
        {
            nextStep = it;
            doDelay = true;
        }
    }

    if (!nextStep->IsWalkable())
        return false;

    if (ai->HasPlayerNearby(nextStep->point, sWorld.getConfig(CONFIG_FLOAT_LISTEN_RANGE_YELL)))
        return true;

    bot->TeleportTo(nextStep->point);

    if (std::next(nextStep) == path.end())
    {
        lastMove.lastPath.clear();
        return true;
    }

    uint32 time = 0;

    for (auto it = std::next(nextStep); it != path.end(); ++it)
    {
        time += (nextStep->point.distance(bot) / bot->GetSpeedInMotion()) * 1000;

        nextStep = it;

        if (!it->IsWalkable() || time > sPlayerbotAIConfig.passiveDelay)
            break;
    }

    lastMove.nextTeleport = now + (time / 1000);

    lastMove.lastPath.cutTo(*nextStep, false);

    return true;
}

bool MovementAction::WaitForTransport()
{
    LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

    // Check if we need to resume transport journey
    if (!lastMove.lastTransportEntry)
        return false;

    GenericTransport* transport = bot->GetTransport();

    if (!transport || transport->GetEntry() != lastMove.lastTransportEntry || lastMove.lastPath.GetPath().front().type != PathNodeType::NODE_TRANSPORT || lastMove.lastPath.GetPath().front().entry != lastMove.lastTransportEntry)
    {
        lastMove.lastTransportEntry = 0;
        return false;
    }

    TravelPath path = lastMove.lastPath;

    if(!path.UpcommingSpecialMovement(bot, 0.0f, bot->GetTransport()))
        return false;

    PathNodePoint dockPoint = path.GetPath().front();
    PathNodePoint telePoint = *std::next(path.GetPath().begin());

    if (!UseTransport(ai, dockPoint.entry, dockPoint.point, telePoint.point, sPlayerbotAIConfig.transportTeleportType > 0))
        return true;

    lastMove.lastTransportEntry = 0;
    return false;
}

TravelPath MovementAction::ResolveMovePath(const WorldPosition& startPosition, const WorldPosition& endPosition, Unit* mover, LastMovement& lastMove)
{
    float totalDistance = startPosition.distance(endPosition);
    float maxDistChange = totalDistance * 0.1f;

    // Last long path still leads to roughly the same destination.
    if (!lastMove.lastPath.empty() && lastMove.lastPath.GetBack().distance(endPosition) < maxDistChange)
    {
        return lastMove.lastPath;
    }

    bool needsLongPath = false;

    if (startPosition.GetMapId() != endPosition.GetMapId())
        needsLongPath = true;
    else if (totalDistance > sPlayerbotAIConfig.sightDistance)
        needsLongPath = true;

    TravelPath outMovePath;

    if (needsLongPath && !sTravelNodeMap.GetNodes().empty() && !bot->InBattleGround())
    {
        outMovePath = sTravelNodeMap.GetFullPath(startPosition, endPosition, bot); //Pathfind using nodes.
    }
    else
    {
        std::vector<WorldPosition> path = startPosition.GetPathTo(endPosition, bot); //Navemesh pathfinding only.

        outMovePath.addPath(path);
    }

    if (!lastMove.lastPath.empty() && !outMovePath.empty() && lastMove.lastPath.GetBack().distance(endPosition) <= outMovePath.GetBack().distance(endPosition))
        outMovePath = lastMove.lastPath;

    if (outMovePath.empty())
        outMovePath.addPoint(endPosition);

    return outMovePath;
}

bool MovementAction::HandleSpecialMovement(TravelPath& path)
{
    PathNodePoint currentPoint = path.GetPath().front();
    PathNodePoint nextPoint;
    if (path.GetPath().size() > 1)
        nextPoint = *std::next(path.GetPath().begin());

    //Game object portals
    if (currentPoint.type == PathNodeType::NODE_STATIC_PORTAL && currentPoint.entry)
    {
        GameObjectInfo const* goInfo = sGOStorage.LookupEntry<GameObjectInfo>(currentPoint.entry);
        if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SPELLCASTER)
            return false;

        uint32 spellId = goInfo->spellcaster.spellId;
        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);

        if (pSpellInfo->EffectTriggerSpell[0])
            pSpellInfo = sServerFacade.LookupSpellInfo(pSpellInfo->EffectTriggerSpell[0]);

        bool hasTeleportEffect = pSpellInfo->Effect[0] == SPELL_EFFECT_TELEPORT_UNITS || pSpellInfo->Effect[1] == SPELL_EFFECT_TELEPORT_UNITS || pSpellInfo->Effect[2] == SPELL_EFFECT_TELEPORT_UNITS;
        if (!hasTeleportEffect)
            return false;

        if (bot->IsMounted())
        {
            if (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f)
                return false;
            ai->Unmount();
        }

        std::list<ObjectGuid> gos = *context->GetValue<std::list<ObjectGuid>>("nearest game objects");
        for (auto i = gos.begin(); i != gos.end(); ++i)
        {
            GameObject* go = ai->GetGameObject(*i);
            if (!go || go->GetEntry() != currentPoint.entry)
                continue;

            if (!bot->GetGameObjectIfCanInteractWith(go->getObjectGuid(), MAX_GAMEOBJECT_TYPE))
                continue;

            std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
            *packet << *i;
            bot->GetSession()->QueuePacket(packet.release());
            return true;
        }

        return false;
    }

    if (currentPoint.type == PathNodeType::NODE_AREA_TRIGGER)
    {
        if (currentPoint.entry)
            AI_VALUE(LastMovement&, "last area trigger").lastAreaTrigger = currentPoint.entry;
        else
            return bot->TeleportTo(nextPoint.point.GetMapId(), nextPoint.point.getX(), nextPoint.point.getY(), nextPoint.point.getZ(), nextPoint.point.getO(), 0) ? true : false;
    }

    //We are getting 'on' transport.
    if (nextPoint.type == PathNodeType::NODE_TRANSPORT)
    {
        bool usedTransport = UseTransport(ai, nextPoint.entry, nextPoint.point, WorldPosition(), sPlayerbotAIConfig.transportTeleportType > 0);

        uint32 lastTransportEntry = 0;

        if (usedTransport)
            AI_VALUE(LastMovement&, "last movement").lastTransportEntry = nextPoint.entry;

        WaitForReach(1000.0f);
        return true;
    }

    if (currentPoint.type == PathNodeType::NODE_TRANSPORT)
    {
        bool usedTransport = UseTransport(ai, currentPoint.entry, currentPoint.point, nextPoint.point, sPlayerbotAIConfig.transportTeleportType > 0);

        uint32 lastTransportEntry = 0;

        if (!usedTransport)
        {
            if (bot->GetTransport())
                lastTransportEntry = nextPoint.entry;
        }
        else
        {
            if (!bot->GetTransport())
                return bot->TeleportTo(nextPoint.point.GetMapId(), nextPoint.point.getX(), nextPoint.point.getY(), nextPoint.point.getZ(), nextPoint.point.getO(), 0) ? true : false;

            lastTransportEntry = nextPoint.entry;
        }

        if (lastTransportEntry)
            AI_VALUE(LastMovement&, "last movement").lastTransportEntry = lastTransportEntry;

        WaitForReach(1000.0f);
        return true;
    }

    if (nextPoint.type == PathNodeType::NODE_FLIGHTPATH && nextPoint.entry)
        return UseTaxi(ai, nextPoint.entry, true) ? true : false;

    if (nextPoint.type == PathNodeType::NODE_TELEPORT && nextPoint.entry)
    {
        bool canCastNow = !bot->IsFlying() || WorldPosition(bot).currentHeight() < 10.0f;

        if (nextPoint.entry == 8690) // Hearthstone
        {
            if (AI_VALUE2(bool, "action useful", "hearthstone") && canCastNow)
                return ai->DoSpecificAction("hearthstone", Event("move action"), true) ? true : false;
        }
        else if (sServerFacade.IsSpellReady(bot, nextPoint.entry) && canCastNow && AI_VALUE2(uint32, "has reagents for", nextPoint.entry) > 0)
        {
            if (AI_VALUE2(uint32, "current mount speed", "self target"))
            {
                ai->Unmount();
            }

            ai->RemoveShapeshift();

            if (ai->DoSpecificAction("cast", Event("rpg action", chat->formatWorldobject(bot) + " " + std::to_string(nextPoint.entry)), true))
                return true;
        }

        AI_VALUE(LastMovement&, "last movement").setPath(TravelPath());
        return false;
    }

    return false;
}

void MovementAction::UpdateFlyingState(
    WorldPosition& movePosition,
    float totalDistance,
    float originalZ,
    float maxDist,
    bool isWalking)
{
}

void MovementAction::DispatchMovement(TravelPath movePath, bool generatePath, bool masterWalking)
{
    MotionMaster& mm = *bot->GetMotionMaster();

    mm.Clear();

    ForcedMovement moveMode = masterWalking ? FORCED_MOVEMENT_WALK : FORCED_MOVEMENT_RUN;

    std::vector<WorldPosition> path = movePath.GetPointPath();

    if (!generatePath || !bot->IsFreeFlying())
    {
        WorldPosition movePosition = path.back();

        // Tortoise's MovePoint signature is (id, x, y, z, options, speed, orientation),
        // NOT cmangos's (id, x, y, z, ForcedMovement, bool generatePath). The ported call
        // below used to pass `moveMode` into `options` and the `generatePath` bool into the
        // `speed` float — so generatePath==true set the velocity to 1.0 yd/s, making bots
        // crawl slower than walking. Translate the intent into proper MoveOptions instead and
        // leave speed at its default so it is derived from the run/walk movement flags.
        uint32 moveOptions = (moveMode == FORCED_MOVEMENT_WALK) ? MOVE_WALK_MODE : MOVE_RUN_MODE;
        if (generatePath)
            moveOptions |= MOVE_PATHFINDING;
        mm.MovePoint(movePosition.GetMapId(),
            movePosition.getX(),
            movePosition.getY(),
            movePosition.getZ(),
            moveOptions);
    }

    GeneratePathAvoidingHazards(path);

    std::vector<G3D::Vector3> pointPath = WorldPosition().toPointsArray(path);
    float size = WorldPosition().GetPathLength(path);

    bool usePath = true;

    if (usePath)
    {
        bool normalizeZ = true;

        for (auto& p : pointPath)
        {
            if (bot->GetTransport())
                bot->GetTransport()->CalculatePassengerPosition(p.x, p.y, p.z);
            bot->UpdateAllowedPositionZ(p.x, p.y, p.z);
            if (bot->GetTransport())
                bot->GetTransport()->CalculatePassengerOffset(p.x, p.y, p.z);
        }

        mm.MovePath(pointPath, moveMode, false, false);
    }
    else
    {
        WorldPosition movePosition = path.back();

        // Tortoise's MovePoint signature is (id, x, y, z, options, speed, orientation),
        // NOT cmangos's (id, x, y, z, ForcedMovement, bool generatePath). The ported call
        // below used to pass `moveMode` into `options` and the `generatePath` bool into the
        // `speed` float — so generatePath==true set the velocity to 1.0 yd/s, making bots
        // crawl slower than walking. Translate the intent into proper MoveOptions instead and
        // leave speed at its default so it is derived from the run/walk movement flags.
        uint32 moveOptions = (moveMode == FORCED_MOVEMENT_WALK) ? MOVE_WALK_MODE : MOVE_RUN_MODE;
        if (generatePath)
            moveOptions |= MOVE_PATHFINDING;
        mm.MovePoint(movePosition.GetMapId(),
            movePosition.getX(),
            movePosition.getY(),
            movePosition.getZ(),
            moveOptions);
    }
    WaitForReach(size);
}


Unit* MovementAction::GetMover(Player* bot)
{
    return bot;
}

bool MovementAction::MoveTo2(const WorldPosition& endPos, bool idle, bool react, bool noPath, bool ignoreEnemyTargets)
{
    if (!endPos.isValid())
        return false;

    UpdateMovementState();

    if (!ai->CanMove())
        return false;

    Unit* mover = GetMover(bot);

    LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

    bool detailedMove = ai->AllowActivity(DETAILED_MOVE_ACTIVITY, true);
    if (!detailedMove && lastMove.nextTeleport)
    {
        time_t now = time(0);
        if (lastMove.nextTeleport > now)
        {
            SetDuration((lastMove.nextTeleport - now) * 1000);
            return true;
        }
    }
    else
        lastMove.nextTeleport = 0;

    if (WaitForTransport())
        return true;

    WorldPosition startPos(bot);
    float totalDistance = startPos.distance(endPos);
    float maxDistChange = totalDistance * 0.1f;

    if (totalDistance < sPlayerbotAIConfig.targetPosRecalcDistance)
    {
        if (!lastMove.lastPath.empty() && lastMove.lastPath.GetBack().distance(endPos) <= totalDistance)
            lastMove.clear();

        if (mover == bot)
            ai->StopMoving();
        else
            mover->StopMoving();

        return false;
    }

    WorldPosition flyMovePosition;
    if (FlyDirect(startPos, endPos, flyMovePosition, lastMove.lastPath))
        return true;


    bool isWalking = false;

    TravelPath movePath = ResolveMovePath(startPos, endPos, mover, lastMove);

    lastMove.setPath(movePath);

    if (movePath.empty())
        return false;


    if (!bot->GetTransport())
        movePath.makeShortCut(startPos, sPlayerbotAIConfig.reactDistance, bot);

    if (movePath.empty())
    {
        lastMove.setPath(movePath);
        return true; // Path collapsed — will rebuild next tick.
    }


    TravelNodePathType pathType = TravelNodePathType::none;
    uint32 entry = 0;
    WorldPosition telePosition;
    bool specialMovement = movePath.UpcommingSpecialMovement(startPos, sPlayerbotAIConfig.reactDistance,bot->GetTransport());

    if (specialMovement)
        return HandleSpecialMovement(movePath);

    if (bot->GetTransport()) //Transports needed to be handled before now.
        return false;

    if (!movePath.empty())
    {
        lastMove.moveEvent = ai->GetLastEvent();
        lastMove.setPath(movePath);
    }

    movePath.ClipPath(ai, mover, ignoreEnemyTargets);

    if(ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        for (auto& p : movePath.GetPath())
        {
            Creature* wpCreature = bot->SummonCreature(2334, p.point.getX(), p.point.getY(), p.point.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000.0f);
            ai->AddAura(wpCreature, 246);
            if (p.point == movePath.GetBack())
                ai->AddAura(wpCreature, 1130);
        }
    }

    if (movePath.empty())
    {
        return false;
    }

    if (movePath.GetFront().GetMapId() == endPos.GetMapId() && !endPos.isUnderWater())
    {
        for (auto& p : movePath.GetPath())
        {
            if (p.point.isUnderWater())
            {
                p.point.setAtWaterSurface();
            }
        }
    }

    if (!react)
    {
        float fullPathDist = startPos.GetPathLength(movePath.GetPointPath());
        float waitDist = (totalDistance > sPlayerbotAIConfig.reactDistance) ? fullPathDist - 10.0f : fullPathDist;
        WaitForReach(waitDist);
    }

    if (bot == mover)
    {
        bot->HandleEmoteState(0);
        if (!bot->IsStandState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        if (bot->IsNonMeleeSpellCasted(true, false, true))
            ai->InterruptSpell(false);
    }

    if (totalDistance > sPlayerbotAIConfig.reactDistance && !detailedMove)
    {
        WorldPosition teleportPosition = movePath.GetBack();
        // Only skip the walk animation via a hard teleport if nobody is
        // watching from either end -- checking just the destination let a
        // bot teleport away in full view of a player standing at its
        // current position, which reads as the bot "zooming" instantly to
        // its new spot instead of walking there.
        if (!ai->HasPlayerNearby(teleportPosition) && !ai->HasPlayerNearby(startPos))
        {
            time_t now = time(0);
            lastMove.nextTeleport = now + (time_t)MoveDelay(startPos.distance(teleportPosition));
            return bot->TeleportTo(teleportPosition.GetMapId(),
                teleportPosition.getX(),
                teleportPosition.getY(),
                teleportPosition.getZ(),
                startPos.GetAngleTo(teleportPosition));
        }
    }

    bool masterWalking = false;
    if (sPlayerbotAIConfig.walkDistance)
    {
        if (Unit* master = ai->GetMaster())
        {
            if (sServerFacade.IsFriendlyTo(bot, master) && master->m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE) && sServerFacade.getDistance2d(bot, master) < sPlayerbotAIConfig.walkDistance
                && ai->GetState() != BotState::BOT_STATE_COMBAT)
            {
                masterWalking = true;
            }
        }
    }

    bool generatePath = !noPath && !bot->IsFlying() && !bot->HasMovementFlag(MOVEFLAG_SWIMMING) && !bot->IsInWater() && !sServerFacade.IsUnderwater(bot);



    // DEBUG: Check for Ironforge AH roof climbing bug
    // IF Auction House is at approx -4900, -950, 500 (Military Ward)
    const float IF_AH_X = -4900.0f;
    const float IF_AH_Y = -950.0f;
    const float IF_AH_Z = 500.0f;
    const float IF_AH_MAP = 0.0f;  // Eastern Kingdoms
    const float CHECK_RADIUS = 150.0f;

    if (bot->GetMapId() == (uint32)IF_AH_MAP &&
        startPos.sqDistance2d(WorldPosition(0, IF_AH_X, IF_AH_Y, 0)) < CHECK_RADIUS * CHECK_RADIUS &&
        !movePath.empty())
    {
        // Calculate total XY distance and total Z change in path
        float totalXY = 0.0f;
        float totalZ = 0.0f;
        float maxZDelta = 0.0f;
        WorldPosition prevPos = startPos;

        for (const auto& point : movePath.GetPointPath())
        {
            float dXY = sqrtf(prevPos.sqDistance2d(WorldPosition(0, point.x, point.y, 0)));
            float dZ = fabs(point.z - prevPos.z);
            totalXY += dXY;
            totalZ += dZ;
            if (dZ > maxZDelta) maxZDelta = dZ;
            prevPos = point;
        }

        // Check if Z change is abnormally large compared to XY (climbing through roof)
        // Normal walking should have Z/X ratio < 0.5, roof climbing can be > 2.0
        bool isAbnormalClimb = (totalZ > 5.0f && totalXY > 0.1f && totalZ / totalXY > 1.5f) || maxZDelta > 50.0f;

        if (isAbnormalClimb)
        {
            bool isFromLastPath = (!lastMove.lastPath.empty() && lastMove.lastPath.GetPointPath().size() == movePath.GetPointPath().size());

            sLog.outError("[BOT PATH BUG] %s near IF AH - abnormal upward path detected!", bot->GetName());
            sLog.outError("[BOT PATH BUG] Bot pos: %.1f,%.1f,%.1f (map %d). Target: %.1f,%.1f,%.1f",
                bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->GetMapId(),
                movePath.GetBack().x, movePath.GetBack().y, movePath.GetBack().z);
            sLog.outError("[BOT PATH BUG] Path stats: %u points, XY=%.1f, Z_total=%.1f, maxZ_delta=%.1f, ratio=%.2f",
                (uint32)movePath.GetPointPath().size(), totalXY, totalZ, maxZDelta, totalXY > 0 ? totalZ/totalXY : 0);
            sLog.outError("[BOT PATH BUG] Route type: %s (lastPath empty: %s, detailedMove: %s)",
                isFromLastPath ? "REUSED from lastPath" : "FRESH route",
                lastMove.lastPath.empty() ? "yes" : "no",
                detailedMove ? "yes" : "no");

            // Log first few path points
            char pathBuf[512];
            snprintf(pathBuf, sizeof(pathBuf), "[BOT PATH BUG] Path points: ");
            int logCount = std::min((int)movePath.GetPointPath().size(), 5);
            for (int i = 0; i < logCount; i++)
            {
                const auto& p = movePath.GetPointPath()[i];
                char pointBuf[64];
                snprintf(pointBuf, sizeof(pointBuf), "[#%d: %.1f,%.1f,%.1f] ", i, p.x, p.y, p.z);
                strncat(pathBuf, pointBuf, sizeof(pathBuf) - strlen(pathBuf) - 1);
            }
            sLog.outError("%s", pathBuf);
        }
    }
    // END DEBUG

    DispatchMovement(movePath, generatePath, masterWalking);

    if (!idle)
        ClearIdleState();

    return true;
}

bool MovementAction::MoveTo(uint32 mapId, float x, float y, float z, bool idle, bool react, bool noPath, bool ignoreEnemyTargets)
{
    return MoveTo2(WorldPosition(mapId, x, y, z), idle, react, noPath, ignoreEnemyTargets);
}


bool MovementAction::MoveTo(Unit* target, float distance)
{
    if (!target || !target->IsInWorld())
    {
        //ai->TellError("Seems I am stuck");
        return false;
    }

    float bx = bot->getPositionX(), by = bot->getPositionY(), bz = bot->getPositionZ();
    float tx = target->getPositionX(), ty = target->getPositionY(), tz = target->getPositionZ();

    if (sServerFacade.IsHostileTo(bot, target))
    {
        Stance* stance = AI_VALUE(Stance*, "stance");
        WorldLocation loc = stance->GetLocation();
        if (Formation::IsNullLocation(loc) || loc.mapId == -1)
        {
            //ai->TellError("Nowhere to move");
            return false;
        }

        tx = loc.x;
        ty = loc.y;
        tz = loc.z;
    }

    float distanceToTarget = sServerFacade.getDistance2d(bot, tx, ty);
    if (sServerFacade.IsDistanceGreaterThan(distanceToTarget, sPlayerbotAIConfig.targetPosRecalcDistance))
    {
        /*
        float angle = bot->GetAngle(tx, ty);
        float needToGo = distanceToTarget - distance;

        float maxDistance = ai->GetRange("spell");
        if (needToGo > 0 && needToGo > maxDistance)
            needToGo = maxDistance;
        else if (needToGo < 0 && needToGo < -maxDistance)
            needToGo = -maxDistance;

        float dx = cos(angle) * needToGo + bx;
        float dy = sin(angle) * needToGo + by;
        float dz = bz + (tz - bz) * needToGo / distanceToTarget;
        */

        float dx = tx;
        float dy = ty;
        float dz = tz;
        return MoveTo(target->GetMapId(), dx, dy, dz);
    }

    return true;
}

float MovementAction::GetFollowAngle()
{
    Player* master = GetMaster();
    Group* group = master ? master->GetGroup() : bot->GetGroup();
    if (!group || group->GetMembersCount() == 1)
        return 0.0f;

    int index = 1;
    for (GroupReference *ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        if( ref->GetSource() == master)
            continue;

        if( ref->GetSource() == bot)
            return 2 * M_PI / (group->GetMembersCount() -1) * index;

        index++;
    }
    return 0;
}

bool MovementAction::IsMovingAllowed(Unit* target)
{
    if (!target)
        return false;

    if (bot->GetMapId() != target->GetMapId())
        return false;

    float distance = sServerFacade.getDistance2d(bot, target);
    if (!bot->InBattleGround() && distance > sPlayerbotAIConfig.reactDistance)
        return false;

    return ai->CanMove();
}

bool MovementAction::IsMovingAllowed(uint32 mapId, float x, float y, float z)
{
    float distance = bot->getDistance(x, y, z);
    if (!bot->InBattleGround() && distance > sPlayerbotAIConfig.reactDistance)
        return false;

    return ai->CanMove();
}

bool MovementAction::Follow(Unit* target, float distance)
{
    if (!distance)
        distance = ai->GetRange("follow");
    return Follow(target, distance, GetFollowAngle());
}

void MovementAction::UpdateMovementState()
{
    if (bot->IsInWater() || sServerFacade.IsUnderwater(bot))
    {
		bot->m_movementInfo.AddMovementFlag(MOVEFLAG_SWIMMING);
        bot->UpdateSpeed(MOVE_SWIM, true);
    }
    else
    {
		bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_SWIMMING);
        bot->UpdateSpeed(MOVE_SWIM, true);
    }

}

bool MovementAction::Follow(Unit* target, float distance, float angle)
{
    if (!ai->IsSafe(target))
        return MoveTo2(target);

    MotionMaster &mm = *bot->GetMotionMaster();

    distance = distance <= target->GetObjectBoundingRadius() ? 0 : distance - target->GetObjectBoundingRadius();

    UpdateMovementState();

    if (FollowOnTransport(target))
        return true;

    WorldPosition botPos(bot);
    WorldPosition tarPos(target);

    //Move to target corpse if alive.
    if (!target->IsAlive() && bot->IsAlive() && target->getObjectGuid().IsPlayer())
    {
        Player* pTarget = (Player*)target;

        Corpse* corpse = pTarget->GetCorpse();

        if (corpse)
        {
            WorldPosition cPos(corpse);

            if(botPos.fDist(cPos) > sPlayerbotAIConfig.spellDistance)
                return MoveTo(cPos.GetMapId(),cPos.getX(),cPos.getY(), cPos.getZ());
            return false;
        }
    }

    float tDist = botPos.fDist(tarPos);

    if (tDist > sPlayerbotAIConfig.sightDistance || (target->IsFlying() && !bot->IsFreeFlying()) || target->IsTaxiFlying())
    {
        if (target->getObjectGuid().IsPlayer())
        {
            Player* player = (Player*)target;

            if (ai->IsSafe(player))
            {
                if (PlayerbotAIStorage::Instance().GetAI(player)) //Try to move to where the bot is going if it is closer and in the same direction.
                {
                    WorldPosition longMove = PAI_VALUE(WorldPosition, "last long move");

                    if (longMove)
                    {
                        return MoveTo(longMove.GetMapId(), longMove.getX(), longMove.getY(), longMove.getZ());
                    }
                }
            }

            if (player->IsTaxiFlying()) //Move to where the player is flying to.
            {
                const Taxi::Map tMap = player->GetTaxiPathSpline();
                if (!tMap.empty())
                {
                    auto tEnd = tMap.back();

                    if (tEnd)
                        return MoveTo(tEnd->mapId, tEnd->x, tEnd->y, tEnd->z);
                }
            }
        }
        if (!target->IsTaxiFlying()/* || bot->GetTransport()*/)
           return MoveTo(target, ai->GetRange("follow"));
    }

    // Handle water transition
    {
        bool targetInWater = (tarPos.isInWater() || tarPos.isUnderWater()) && !botPos.isInWater() && !botPos.isUnderWater();
        bool selfInWater = (botPos.isInWater() || botPos.isUnderWater()) && !tarPos.isInWater() && !tarPos.isUnderWater();
        bool targetOnSurface = botPos.isUnderWater() && tarPos.isInWater() && !tarPos.isUnderWater();
        bool selfOnSurface = tarPos.isUnderWater() && botPos.isInWater() && !botPos.isUnderWater();
        if ((targetInWater || selfInWater || targetOnSurface || selfOnSurface) && !(tarPos.isUnderWater() && botPos.isUnderWater()))
        {
            // in or out of water
            WorldPosition moveToPos = (targetInWater || selfOnSurface) ? tarPos : botPos;
            Unit* targetToCheck = (targetInWater || selfOnSurface) ? target : bot;
            if (const TerrainInfo* terrain = moveToPos.GetTerrain())
            {
                float bottom = terrain->GetHeightStatic(moveToPos.getX(), moveToPos.getY(), moveToPos.getZ());
                float waterLevel = terrain->GetWaterOrGroundLevel(moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), &bottom, true);
                bool canSwimToTarget = selfOnSurface && botPos.IsInLineOfSight(tarPos);
                moveToPos.setZ(waterLevel);
                if (waterLevel > -200000.0f && waterLevel > bottom)
                {
                    PathFinder pathfinder(bot);
                    //Use standard pathfinder to find a route.
                    WorldPosition prevPoint = botPos;
                    pathfinder.calculate(moveToPos.GetVector3(), tarPos.GetVector3());
                    Movement::PointsArray const& pathPoints = pathfinder.GetPath();
                    if (pathPoints.size() >= 2)
                    {
                        for (uint32 i = 1; i < pathPoints.size() - 1; i++)
                        {
                            WorldPosition pathPoint(bot->GetMapId(), pathPoints[i].x, pathPoints[i].y, pathPoints[i].z);
                            if (selfInWater)
                            {
                                if (pathPoint.isInWater())
                                {
                                    prevPoint = pathPoint;
                                    continue;
                                }
                                if (!MoveTo(prevPoint))
                                {
                                    return MoveTo(pathPoint);
                                }
                                return true;
                            }
                        }
                    }
                    moveToPos = tarPos;
                    return MoveTo(moveToPos);
                }
            }
        }
    }

    bot->HandleEmoteState(0);
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    if (bot->IsNonMeleeSpellCasted(true))
    {
        bot->CastStop();
        ai->InterruptSpell();
    }

    AI_VALUE(LastMovement&, "last movement").Set(target);
    ClearIdleState();


    // Find jump shortcut
    if (ai->HasStrategy("follow jump", BotState::BOT_STATE_NON_COMBAT) && ai->AllowActivity())
    {
        WorldPosition botPos(bot);
        if (!botPos.isInWater())
        {
            bool tryJump = ai->DoSpecificAction("jump::follow", Event(), true);
            if (tryJump)
                return true;
        }
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        Unit* currentTarget = sServerFacade.GetChaseTarget(bot);
        // The Penqle generator intentionally keeps its requested angle and
        // offset private. The public target/current-motion contract is enough
        // to let the native generator continue; repeatedly replacing it while
        // it is moving causes jitter and defeats the follow dead-zone.
        if (currentTarget && currentTarget->getObjectGuid() == target->getObjectGuid() && !bot->IsStopped())
            return false;
    }

    mm.MoveFollow(target, distance, angle, true, sPlayerbotAIConfig.boostFollow);
    return true;
}

WorldPosition CalculatePerpendicularPoint(const WorldPosition& A, const WorldPosition& B, float offset, bool left = true)
{
    WorldPosition direction = (B - A);
    if (direction)
        direction = direction/direction.size();

    WorldPosition perpendicularDirection(0,-direction.getY(), direction.getX(), direction.getZ());

    if (!left)
    {
        perpendicularDirection.setX(-perpendicularDirection.getX());
        perpendicularDirection.setY(-perpendicularDirection.getY());
    }

    return B + (perpendicularDirection * offset);
}

bool MovementAction::ChaseTo(WorldObject* obj, float distance, float angle)
{
    if (!ai->CanMove())
    {
        sLog.outDetail("[BOT CHASE] %s: CanMove() blocked", bot->GetName());
        return false;
    }

    if (!ai->IsSafe(obj))
    {
        sLog.outDetail("[BOT CHASE] %s: IsSafe() blocked", bot->GetName());
        return false;
    }


    if (ai->HasStrategy("behind", BotState::BOT_STATE_COMBAT))
        angle = GetFollowAngle() / 3 + obj->getOrientation() + M_PI;

    UpdateMovementState();

    bot->HandleEmoteState(0);
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);


    // Calculate the chase position
    const WorldPosition botPosition(bot);
    const WorldPosition targetPosition(obj);
    const Vector3 botPoint = botPosition.GetVector3();
    const Vector3 targetPoint = targetPosition.GetVector3();

    const float distanceToTarget = botPosition.distance(targetPosition);

    if (distanceToTarget > sPlayerbotAIConfig.sightDistance)
        return MoveTo(targetPosition.GetMapId(), targetPosition.getX(), targetPosition.getY(), targetPosition.getZ());

    const Vector3 directionToTarget = (targetPoint - botPoint).directionOrZero();
    const Vector3 endPoint = botPoint + (directionToTarget * std::min(distance, distanceToTarget));
    WorldPosition endPosition(obj->GetMapId(), endPoint.x, endPoint.y, endPoint.z);
    endPosition.setZ(endPosition.GetHeight());

    // Check if the end position is inside a hazard
    HazardPosition hazardPosition;
    if (IsHazardNearPosition(endPosition, &hazardPosition))
    {
        // Try to generate a nearby position outside the hazard
        const Vector3 hazardPoint = hazardPosition.first.GetVector3();
        const float hazardRangeOffset = hazardPosition.second * 1.5f;

        // Generate point translated to the left
        Vector3 possibleEndPoint = CalculatePerpendicularPoint(endPoint, hazardPoint, hazardRangeOffset, true).GetVector3();

        // Check if point is valid
        WorldPosition possibleEndPosition(bot->GetMapId(), possibleEndPoint.x, possibleEndPoint.y, possibleEndPoint.z);
        if (IsValidPosition(possibleEndPosition, botPosition))
        {
            endPosition.x = possibleEndPoint.x;
            endPosition.y = possibleEndPoint.y;
            endPosition.z = possibleEndPoint.z;
        }
        else
        {
            // Generate point translated to the right
            possibleEndPoint = CalculatePerpendicularPoint(endPoint, hazardPoint, hazardRangeOffset, false).GetVector3();

            endPosition.x = possibleEndPoint.x;
            endPosition.y = possibleEndPoint.y;
            endPosition.z = possibleEndPoint.z;
        }
    }

    MotionMaster& mm = *bot->GetMotionMaster();

    // Prevent moving if requested to move into a hazard
    if (IsValidPosition(endPosition, botPosition))
    {
        std::vector<WorldPosition> path = botPosition.GetPathTo(endPosition,bot);
        if (GeneratePathAvoidingHazards(path))
        {
            float distance = botPosition.GetPathLength(path);
            mm.Clear(false, true);

            std::vector<G3D::Vector3> pointsArray = WorldPosition().toPointsArray(path);
            mm.MovePath(pointsArray, FORCED_MOVEMENT_RUN, false, false);
            sLog.outDetail("[BOT CHASE] %s -> %s: dist=%.1f target=%.1f pts=%u wait=%.2f (MovePath)",
                bot->GetName(), obj->GetName(), distanceToTarget, distance, (uint32)path.size(), distance / bot->GetSpeed(MOVE_RUN));
            WaitForReach(distance);
            return true;
        }
        sLog.outDetail("[BOT CHASE] %s -> %s: dist=%.1f no hazard-avoidance path (no hazards or unroutable), using MoveChase", bot->GetName(), obj->GetName(), distanceToTarget);
    }
    else
    {
        sLog.outDetail("[BOT CHASE] %s -> %s: dist=%.1f endPos invalid, falling back", bot->GetName(), obj->GetName(), distanceToTarget);
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
    {
        if (!bot->IsStopped() &&
            sServerFacade.GetChaseTarget(bot) == obj)
        {
            bot->SetTarget(obj); //Needed to keep chase going in combat.
            bot->Attack((Unit*)obj, false); //Needed to keep chase going in combat.
            return true;
        }
    }

    // charge
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == EFFECT_MOTION_TYPE && !bot->IsStopped())
    {
        return false;
    }

    if (!ai->IsSafe(obj)) return false;

    // Find jump shortcut
    if (ai->HasStrategy("follow chase", BotState::BOT_STATE_NON_COMBAT) && ai->AllowActivity())
    {
        bool tryJump = ai->DoSpecificAction("jump::chase", Event(), true);
        if (tryJump)
            return true;
    }

    if (!endPosition.isValid()) return false;
    if (angle > 20) angle = 0;

    bot->SetTarget(obj); //Needed to keep chase going in combat.
    bot->Attack((Unit*)obj, false); //Needed to keep chase going in combat.

    mm.MoveChase((Unit*)obj, distance, angle);
    float dist = sServerFacade.getDistance2d(bot, obj);
    float distDiff = dist > distance ? dist - distance : 0.f;
    sLog.outDetail("[BOT CHASE] %s -> %s: dist=%.1f target=%.1f wait=%.2f (MoveChase)",
        bot->GetName(), obj->GetName(), dist, distance, distDiff / bot->GetSpeed(MOVE_RUN));
    WaitForReach(distDiff);

    return true;
}

float MovementAction::MoveDelay(float distance)
{
    return distance / bot->GetSpeed(MOVE_RUN);
}

bool MovementAction::FollowOnTransport(Unit* target)
{
    bool const onDifferentTransports = bot->m_movementInfo.t_guid != target->m_movementInfo.t_guid;
    if (onDifferentTransports && sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.getDistance2d(bot, target), sPlayerbotAIConfig.sightDistance))
    {
        ai->StopMoving();
        bool sendHeartbeat = false;

        if (GenericTransport* pMyTransport = bot->GetTransport())
        {
            sendHeartbeat = true;
            pMyTransport->RemovePassenger(bot);
            bot->Relocate(target->getPositionX(), target->getPositionY(), target->getPositionZ());
        }

        if (GenericTransport* pHisTransport = target->GetTransport())
        {
            sendHeartbeat = true;
            bot->Relocate(target->getPositionX(), target->getPositionY(), target->getPositionZ());
            pHisTransport->AddPassenger(bot);
        }

        if (sendHeartbeat)
            bot->SendHeartBeat();

        return true;
    }

    return false;
}


void MovementAction::WaitForReach(float distance)
{
    float duration = 1000.0f * MoveDelay(distance) + sPlayerbotAIConfig.reactDelay;
    if (duration > sPlayerbotAIConfig.maxWaitForMove)
        duration = sPlayerbotAIConfig.maxWaitForMove;

    /*Unit* target = *ai->GetAiObjectContext()->GetValue<Unit*>("current target");
    Unit* player = *ai->GetAiObjectContext()->GetValue<Unit*>("enemy player target");
    if ((player || target) && duration > sPlayerbotAIConfig.globalCoolDown)
        duration = sPlayerbotAIConfig.globalCoolDown;*/

    if (duration < 0.0f)
        duration = 0.0f;

    SetDuration(duration);
}

void MovementAction::WaitForReach(const Movement::PointsArray& path)
{
    float distance = 0.0f;
    if(!path.empty())
    {
        const Vector3* previousPoint = &path[0];
        for (auto it = path.begin() + 1; it != path.end(); ++it)
        {
            const Vector3& pathPoint = (*it);
            distance += (*previousPoint - pathPoint).length();
            previousPoint = &pathPoint;
        }
    }

    WaitForReach(distance);
}

bool MovementAction::Flee(Unit *target)
{
    Player* master = GetMaster();
    if (!target)
        target = master;

    if (!target)
        return false;

    if (!sPlayerbotAIConfig.fleeingEnabled)
        return false;

    if (!ai->CanMove())
    {
        ai->TellError(GetMaster(), "I am stuck while fleeing");
        return false;
    }

    HostileReference* ref = sServerFacade.GetThreatManager(target).getCurrentVictim();
    const bool isTarget = ref && ref->getTarget() == bot;

    time_t lastFlee = AI_VALUE(LastMovement&, "last movement").lastFlee;
    time_t now = time(0);
    uint32 fleeDelay = urand(2, sPlayerbotAIConfig.returnDelay / 1000);

    // let hunter kite mob
    if (isTarget && bot->GetClass() == CLASS_HUNTER)
    {
        fleeDelay = 1;
    }

    if (lastFlee && sServerFacade.isMoving(bot))
    {
        if ((now - lastFlee) <= fleeDelay)
        {
            return true;
        }
    }

    const bool isHealer = ai->IsHeal(bot);
    const bool isTank = ai->IsTank(bot);
    const bool isDps = !isHealer && !isTank;
    const bool isRanged = ai->IsRanged(bot);
    const bool needHealer = !isHealer && AI_VALUE2(uint8, "health", "self target") < 50;

    Unit* fleeTarget = nullptr;
    Group* group = bot->GetGroup();
    if (group)
    {
        Unit* spareTarget = nullptr;
        std::vector<Unit*> possibleTargets;
        const float minFleeDistance = 5.0f;
        const float maxFleeDistance = isTarget ? 40.0f : ai->GetRange("spell") * 1.5;
        const float minRangedTargetDistance = ai->GetRange("spell") / 2 + ai->GetRange("follow");

        for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* groupMember = gref->GetSource();

            // Ignore group member if is not alive or on a different zone
            if (!groupMember || groupMember->IsBeingTeleported() || groupMember == bot || groupMember == master || !sServerFacade.IsAlive(groupMember) || bot->GetMapId() != groupMember->GetMapId())
                continue;

            // Don't flee to group member if too close or too far
            float const distanceToGroupMember = sServerFacade.getDistance2d(bot, groupMember);
            if (distanceToGroupMember < minFleeDistance || distanceToGroupMember > maxFleeDistance)
                continue;

            if (PlayerbotAI* groupMemberBotAi = PlayerbotAIStorage::Instance().GetAI(groupMember))
            {
                // Ignore if the group member is affected by an aoe spell
                if (groupMemberBotAi->GetAiObjectContext()->GetValue<bool>("has area debuff", "self target")->Get())
                    continue;
            }

            // If the bot is currently being targeted
            if(isTarget)
            {
                // Try to flee to tank
                if (ai->IsTank(groupMember))
                {
                    float distanceToTank = sServerFacade.getDistance2d(bot, groupMember);
                    float distanceToTarget = sServerFacade.getDistance2d(bot, target);
                    if (distanceToTank > minFleeDistance && distanceToTank < maxFleeDistance)
                    {
                        possibleTargets.push_back(groupMember);
                    }
                }
            }
            else
            {
                // Try to flee to healers (group healers together or approach a healer if needed)
                if ((isHealer && ai->IsHeal(groupMember)) || needHealer)
                {
                    const float distanceToTarget = sServerFacade.getDistance2d(groupMember, target);
                    if (distanceToTarget > minRangedTargetDistance && (needHealer || groupMember->IsWithinLOSInMap(target, true)))
                    {
                        possibleTargets.push_back(groupMember);
                    }
                }
                // Try to flee to ranged (group ranged together)
                else if (isRanged && ai->IsRanged(groupMember))
                {
                    const float distanceToTarget = sServerFacade.getDistance2d(groupMember, target);
                    if (distanceToTarget > minRangedTargetDistance && distanceToTarget < ai->GetRange("spell") &&
                        groupMember->IsWithinLOSInMap(target, true))
                    {
                        possibleTargets.push_back(groupMember);
                    }
                }
            }
        }

        if (!possibleTargets.empty())
        {
            fleeTarget = possibleTargets[urand(0, possibleTargets.size() - 1)];
        }
        else
        {
            // If nothing was found, let's try the master
            if (master && sServerFacade.IsAlive(master) && master->IsWithinLOSInMap(target, true))
            {
                // Don't flee to group member if too close or too far
                float const distanceToMaster = sServerFacade.getDistance2d(bot, master);
                if (distanceToMaster > minFleeDistance && distanceToMaster < maxFleeDistance)
                {
                    if(isRanged)
                    {
                        const float distanceToTarget = sServerFacade.getDistance2d(master, target);
                        if (distanceToTarget > minRangedTargetDistance && distanceToTarget < ai->GetRange("spell"))
                        {
                            fleeTarget = master;
                        }
                    }
                    else
                    {
                        fleeTarget = master;
                    }
                }
            }
        }
    }

    bool succeeded = false;
    if (fleeTarget)
    {
        succeeded = MoveNear(fleeTarget);
    }

    if (!ai->HasRealPlayerMaster() && !ai->IsRealPlayer(target))
    {
        bool fullDistance = false;
        if (target->IsPlayer())
            fullDistance = true;
        if (WorldPosition(bot).isOverworld())
            fullDistance = true;

        float distance = fullDistance ? (ai->GetRange("flee") * 2) : ai->GetRange("flee");

        MotionMaster* mm = bot->GetMotionMaster();

        if (mm->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        {
            if (sServerFacade.GetChaseTarget(bot) == target && !bot->IsStopped())
                return true;
        }

        mm->MoveChase(target, distance, WorldPosition(bot).GetAngleTo(target));
        return true;
    }

    // Generate a position to flee
    if(!succeeded)
    {
        if (lastFlee && bot->GetGroup())
        {
            if (!lastFlee)
            {
                AI_VALUE(LastMovement&, "last movement").lastFlee = now;
            }
            else
            {
                if ((now - lastFlee) > fleeDelay)
                {
                    AI_VALUE(LastMovement&, "last movement").lastFlee = 0;
                }
                else
                {
                    succeeded = false;
                }
            }
        }
        bool fullDistance = false;
        if (target->IsPlayer())
            fullDistance = true;
        if (WorldPosition(bot).isOverworld())
            fullDistance = true;

        FleeManager manager(bot, fullDistance ? (ai->GetRange("flee") * 2) : ai->GetRange("flee"), bot->GetAngle(target) + M_PI);
        if (!manager.isUseful())
        {
            return false;
        }

        if (!urand(0, 50) && ai->HasStrategy("emote", BotState::BOT_STATE_NON_COMBAT))
        {
            std::vector<uint32> sounds;
            sounds.push_back(304); // guard
            sounds.push_back(306); // flee
            ai->PlayEmote(sounds[urand(0, sounds.size() - 1)]);
        }

        float rx, ry, rz;
        if (!manager.CalculateDestination(&rx, &ry, &rz))
        {
            ai->TellError(GetMaster(), "Nowhere to flee");
            return false;
        }

        if(MoveTo(target->GetMapId(), rx, ry, rz))
        {
            LastMovement& lm = AI_VALUE(LastMovement&, "last movement");
            // Count successive flee dispatches so FleeMultiplier can detect a flee loop and
            // hand priority back to spellcasting. A dispatch within the return window of the
            // previous one is "subsequent" (loop); otherwise it's a fresh flee episode.
            if (lm.lastFleeAttempt && (now - lm.lastFleeAttempt) <= (time_t)(sPlayerbotAIConfig.returnDelay / 1000))
                lm.fleeCount++;
            else
                lm.fleeCount = 1;
            lm.lastFleeAttempt = now;
            lm.lastFlee = time(0);
            succeeded = true;
        }
    }

    return succeeded;
}

void MovementAction::ClearIdleState()
{
    context->GetValue<time_t>("stay time")->Set(0);
    context->GetValue<ai::PositionMap&>("position")->Get()["random"].Reset();
}

bool MovementAction::IsValidPosition(const WorldPosition& position, const WorldPosition& visibleFromPosition)
{
    const WorldPosition botPosition(bot);
    return botPosition.canPathTo(position, bot) &&
           MaNGOS::IsValidMapCoord(position.getX(), position.getY(), position.getZ(), 0.0f) &&
           position.IsInLineOfSight(visibleFromPosition, bot->GetCollisionHeight()) &&
           !IsHazardNearPosition(position);
}

bool MovementAction::IsHazardNearPosition(const WorldPosition& position, HazardPosition* outHazard)
{
    AiObjectContext* context = PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext();
    std::list<HazardPosition> hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    if (!hazards.empty())
    {
        for (const HazardPosition& hazard : hazards)
        {
            const WorldPosition& hazardPosition = hazard.first;
            const float hazardRange = hazard.second;
            const float distance = position.distance(hazardPosition);
            if (distance <= hazardRange)
            {
                if (outHazard)
                {
                    *outHazard = hazard;
                }

                return true;
            }
        }
    }

    return false;
}

bool MovementAction::GeneratePathAvoidingHazards(std::vector<WorldPosition>& movePath)
{
    std::list<HazardPosition> hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    if (hazards.empty())
        return false;

    std::vector<WorldPosition> collidingHazards;
    bool pathModified = false;

    // Start the iteration on the second point (the first and last points can't be modified)
    bool firstPoint = true;
    uint8 pointsInserted = 0;
    const uint8 maxPointsInserted = 20;
    WorldPosition previousPosition = movePath.front();
    for (uint32 i = 1; i < movePath.size() - 1; i++)
    {
        bool pointInserted = false;
        WorldPosition pathPoint = movePath[i];
        for (auto& [hazardPosition, hazardRange] : hazards)
        {
            const float hazardRangeOffset = hazardRange * 1.5f;

            // Check if the path point is inside a hazard
            {
                const float distanceToHazard = pathPoint.distance(hazardPosition);
                if (distanceToHazard <= hazardRange)
                {
                    collidingHazards.push_back(hazardPosition);

                    // Move the point out of the hazard range in perpendicular from previous point
                    // Generate point translated to the left
                    WorldPosition possiblePathPoint = CalculatePerpendicularPoint(previousPosition, hazardPosition, hazardRangeOffset, true);

                    // Check if point is valid
                    WorldPosition possiblePathPosition(bot->GetMapId(), possiblePathPoint.getX(), possiblePathPoint.getY(), possiblePathPoint.getZ());
                    if (IsValidPosition(possiblePathPosition, previousPosition))
                    {
                        pathModified = true;
                        pathPoint = possiblePathPoint;
                    }
                    else
                    {
                        // Generate point translated to the right
                        possiblePathPoint = CalculatePerpendicularPoint(previousPosition, hazardPosition, hazardRangeOffset, false);

                        // Check if point is valid
                        WorldPosition possiblePathPosition(bot->GetMapId(), possiblePathPoint.getX(), possiblePathPoint.getY(), possiblePathPoint.getZ());
                        if (IsValidPosition(possiblePathPosition, previousPosition))
                        {
                            pathModified = true;
                            pathPoint = possiblePathPoint;
                        }
                    }
                }
            }

            // Check if the line between the previous point and the current point goes through a hazard
            // Don't check for the line between the first point and second
            if (!firstPoint && (pointsInserted < maxPointsInserted))
            {
                WorldPosition directionFromPreviousPoint = (pathPoint - previousPosition);
                const float distanceToPreviousPoint = std::max(directionFromPreviousPoint.size(), 0.0001f);
                directionFromPreviousPoint = directionFromPreviousPoint / distanceToPreviousPoint;
                WorldPosition inBetweenPathPoint = previousPosition + (directionFromPreviousPoint * distanceToPreviousPoint * 0.5f);

                // Check if the point between path points is inside a hazard
                WorldPosition directionFromHazard = (inBetweenPathPoint - hazardPosition);
                const float distanceToHazard = std::max(directionFromHazard.size(), 0.0001f);
                if (distanceToHazard <= hazardRange)
                {
                    collidingHazards.push_back(hazardPosition);

                    // If so generate a new path point to go around it
                    inBetweenPathPoint = hazardPosition + ((directionFromHazard / distanceToHazard) * hazardRangeOffset);

                    // Check if the point is valid
                    WorldPosition inBetweenPathPosition(bot->GetMapId(), inBetweenPathPoint.getX(), inBetweenPathPoint.getY(), inBetweenPathPoint.getZ());
                    if (IsValidPosition(inBetweenPathPosition, previousPosition))
                    {
                        // Insert the new point to the path (before current point)
                        pathModified = true;
                        pointInserted = true;
                        movePath.insert(movePath.begin() + i, inBetweenPathPoint);
                        pointsInserted++;
                        continue;
                    }
                }
            }
        }

        if (pointInserted)
        {
            // Go back one step to validate the inserted point and move to next loop
            i--;
        }
        else
        {
            firstPoint = false;
            previousPosition.x = pathPoint.getX();
            previousPosition.y = pathPoint.getY();
            previousPosition.z = pathPoint.getZ();
        }
    }

    if (pathModified && !movePath.empty())
    {
        if (ai->HasStrategy("debug move", BotState::BOT_STATE_COMBAT))
        {
            for (auto& pathPoint : movePath)
            {
                bot->SummonCreature(1, pathPoint.getX(), pathPoint.getY(), pathPoint.getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);
            }

            for (auto& hazards : collidingHazards)
            {
                bot->SummonCreature(15631, hazards.getX(), hazards.getY(), hazards.getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);
            }
        }

        return true;
    }

    return false;
}

bool FleeAction::Execute(Event& event)
{
    return Flee(AI_VALUE(Unit*, "current target"));
}

bool FleeWithPetAction::Execute(Event& event)
{
    Pet* pet = bot->GetPet();
    if (pet)
    {
        UnitAI* creatureAI = ((Creature*)pet)->AI();
        if (creatureAI)
        {
            creatureAI->SetReactState(REACT_PASSIVE);
            pet->AttackStop();
        }
    }

    return Flee(AI_VALUE(Unit*, "current target"));
}

bool RunAwayAction::Execute(Event& event)
{
    return Flee(AI_VALUE(Unit*, "master target"));
}

bool MoveToLootAction::Execute(Event& event)
{
    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (!loot.IsLootPossible(bot))
    {
        sLog.outDebug("[BOT LOOT] %s: MoveToLoot abort guid=%lu (IsLootPossible=false)",
            bot->GetName(), loot.guid.GetRawValue());
        if (ai->HasStrategy("debug loot", BotState::BOT_STATE_NON_COMBAT))
        {
            WorldObject* wo = loot.GetWorldObject(bot);

            if (!wo)
            {
                ai->TellPlayerNoFacing(GetMaster(), "Can not move to loot " + std::to_string(loot.guid) +  " because it no longer exists.");
            }
            else
            {
                ai->TellPlayerNoFacing(GetMaster(), "Can not move to loot " + ChatHelper::formatWorldobject(wo) + " because it is not possible to loot.");
            }
        }

        return false;
    }

    WorldObject *wo = loot.GetWorldObject(bot);

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("debug loot", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "Moving to loot " << ChatHelper::formatWorldobject(wo);
        ai->TellPlayerNoFacing(GetMaster(), out);
    }

    bool los = sServerFacade.IsWithinLOSInMap(bot, wo);
    float dist = sServerFacade.getDistance2d(bot, wo);
    bool moved = los ? MoveNear(wo, sPlayerbotAIConfig.contactDistance) : MoveTo(WorldPosition(wo));
    sLog.outDebug("[BOT LOOT] %s: MoveToLoot guid=%lu dist=%.1f los=%d via=%s result=%d",
        bot->GetName(), loot.guid.GetRawValue(), dist, los ? 1 : 0, los ? "MoveNear" : "MoveTo", moved ? 1 : 0);
    return moved;
}

bool MoveOutOfEnemyContactAction::Execute(Event& event)
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;

    return MoveTo(target, sPlayerbotAIConfig.contactDistance);
}

bool MoveOutOfEnemyContactAction::isUseful()
{
    return MovementAction::isUseful() && AI_VALUE2(bool, "inside target", "current target");
}

bool SetFacingTargetAction::Execute(Event& event)
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;

    if (bot->IsTaxiFlying())
        return true;

    sServerFacade.SetFacingTo(bot, target);
    //SetDuration(sPlayerbotAIConfig.globalCoolDown);
    return true;
}

bool SetFacingTargetAction::isUseful()
{
    return !AI_VALUE2(bool, "facing", "current target");
}

bool SetFacingTargetAction::isPossible()
{
    if (sServerFacade.IsFrozen(bot) || bot->IsPolymorphed() ||
        (sServerFacade.UnitIsDead(bot) && !bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)) ||
        bot->IsBeingTeleported() ||
        bot->HasAuraType(SPELL_AURA_MOD_CONFUSE) || sServerFacade.IsCharmed(bot) ||
        bot->HasAuraType(SPELL_AURA_MOD_STUN) || bot->IsTaxiFlying() ||
        bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
        return false;

    return true;
}

bool SetBehindTargetAction::Execute(Event& event)
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;

    float angle = GetFollowAngle() / 3 + target->getOrientation() + M_PI / 2.0f;

    float distance = bot->GetCombinedCombatReach(target, true) * 0.8f;
    float x = target->getPositionX() + cos(target->getOrientation()) * -1.0f * distance,
        y = target->getPositionY() + sin(target->getOrientation()) * -1.0f * distance,
        z = target->getPositionZ();
    bot->UpdateGroundPositionZ(x, y, z);

    // prevent going into terrain
    float ox, oy, oz;
    target->GetPosition(ox, oy, oz);
    target->GetMap()->GetHitPosition(ox, oy, oz + bot->GetCollisionHeight(), x, y, z, -0.5f);

    const bool isLos = target->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true);
    bool moved = MoveTo(bot->GetMapId(), x, y, z);
    if (!moved && !isLos)
    {
        distance = sPlayerbotAIConfig.contactDistance;
        x = target->getPositionX() + cos(angle) * distance;
        y = target->getPositionY() + sin(angle) * distance;
        z = target->getPositionZ();
        bot->UpdateGroundPositionZ(x, y, z);
        moved = MoveTo(bot->GetMapId(), x, y, z);
    }

    return moved;
}

bool SetBehindTargetAction::isUseful()
{
    if(!MovementAction::isUseful())
        return false;

    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && !bot->IsFacingTargetsBack(target))
    {
        // Don't move behind if the target is too far away
        const float distance = bot->getDistance(target, false);
        return distance <= 15.0f;
    }

    return false;
}

bool SetBehindTargetAction::isPossible()
{
    if(MovementAction::isPossible())
    {
        // Check if the target is targeting the bot
        Unit* target = AI_VALUE(Unit*, "current target");
        if (target)
        {
            // If the target is a player
            Player* playerTarget = dynamic_cast<Player*>(target);
            if(playerTarget)
            {
                return bot->getObjectGuid() != playerTarget->GetSelectionGuid();
            }
            // If the target is a NPC
            else
            {
                return !(target->GetVictim() && (target->GetVictim()->getObjectGuid() == bot->getObjectGuid()));
            }
        }
    }

    return false;
}

bool MoveOutOfCollisionAction::Execute(Event& event)
{
    WorldPosition botPos(bot);
    float gx, gy, gz;
    gx = botPos.getX();
    gy = botPos.getY();
    gz = botPos.getZ();

    uint32 tries = 1;
    for (; tries < 10; ++tries)
    {
        gx = botPos.getX();
        gy = botPos.getY();
        gz = botPos.getZ();
        if (bot->GetMap()->GetReachableRandomPointOnGround(gx, gy, gz, ai->GetRange("follow")))
        {
            return MoveTo(bot->GetMapId(), gx, gy, gz);
        }
    }

    // old style
    float angle = M_PI * 2000 / (float)urand(1, 1000);
    float distance = ai->GetRange("follow");
    return MoveTo(bot->GetMapId(), bot->getPositionX() + cos(angle) * distance, bot->getPositionY() + sin(angle) * distance, bot->getPositionZ());
}

bool MoveOutOfCollisionAction::isUseful()
{
    if(!MovementAction::isUseful())
        return false;


    return AI_VALUE2(bool, "collision", "self target") && ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get().size() < 15 &&
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest non bot players")->Get().size() > 0;
}

bool MoveRandomAction::Execute(Event& event)
{
    //uint32 randnum = bot->GetGUIDLow();                            //Semi-random but fixed number for each bot.
    //uint32 cycle = floor(WorldTimer::getMSTime() / (1000*60));     //Semi-random number adds 1 each minute.

    //randnum = ((randnum + cycle) % 1000) + 1;

    uint32 randnum = urand(1, 2000);

    float angle = M_PI  * (float)randnum / 1000; //urand(1, 1000);
    float distance = urand(20,200);

    return MoveTo(bot->GetMapId(), bot->getPositionX() + cos(angle) * distance, bot->getPositionY() + sin(angle) * distance, bot->getPositionZ());
}

bool MoveRandomAction::isUseful()
{
    return !ai->HasRealPlayerMaster() && ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get().size() > urand(25, 100);
}

bool MoveToAction::Execute(Event& event)
{
    std::list<GuidPosition> guidList = AI_VALUE_SAFE(std::list<GuidPosition>, getQualifier());

    if (guidList.empty())
        return false;

    GuidPosition guid = guidList.front();

    return MoveTo(guid.GetMapId(), guid.getX(), guid.getY(), guid.getZ());
}

bool JumpAction::isUseful()
{
    return bot->IsInWorld() && ai->HasPlayerNearby() && !ai->IsJumping();
}

bool JumpAction::Execute(ai::Event &event)
{
    // don't jump while casting without real player command
    if (!event.GetOwner() && bot->IsNonMeleeSpellCasted(false, false, true))
        return false;

    std::string param = event.GetParam();
    std::string qualify = getQualifier();
    std::string options = !param.empty() ? param : !qualify.empty() ? qualify : "";
    bool jumpInPlace = false;
    bool jumpBackward = false;
    bool showLanding = false;
    bool toPosition = false;

    // only show landing
    if (options.find("show") != std::string::npos && options.size() > 5)
    {
        options = options.substr(5);
        showLanding = true;
    }
    // to position
    if (options.find("position") != std::string::npos && options.size() > 9)
    {
        options = options.substr(9);
        toPosition = true;
    }
    // handle options
    if (options.empty() || options == "i" || options == "inplace")
    {
        jumpInPlace = true;
    }
    if (options == "r" || options == "random")
    {
        jumpInPlace = frand(0.0f, 1.0f) < sPlayerbotAIConfig.jumpInPlaceChance;
        jumpBackward = frand(0.0f, 1.0f) < sPlayerbotAIConfig.jumpBackwardChance;
        if (sServerFacade.isMoving(bot) || bot->IsMounted())
        {
            jumpInPlace = false;
        }
        if (jumpInPlace)
            jumpBackward = false;
    }
    if (options == "b" || options == "back")
    {
        jumpBackward = true;
        jumpInPlace = false;
    }
    if (options == "f" || options == "forward")
    {
        jumpInPlace = false;
        jumpBackward = false;
    }
    if (ai->HasStrategy("stay", ai->GetState()))
    {
        if (!jumpInPlace && !showLanding)
            return false;
    }

    // find jump position
    if (options == "tome" || options == "follow" || options == "chase" || toPosition)
    {
        if (options == "follow" && !(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
            return false;

        WorldPosition const src = WorldPosition(bot);
        WorldPosition dest = WorldPosition();
        WorldPosition jumpPoint = WorldPosition();
        WorldPosition possibleLanding = WorldPosition();
        float requiredSpeed = 0.f;
        float distanceTo = 30.f;
        float distanceFrom = 30.f;

        if (options == "tome")
        {
            if (!event.GetOwner())
                return false;

            dest = WorldPosition(event.GetOwner());
        }

        if (options == "follow")
        {
            if (!ai->HasRealPlayerMaster())
                return false;

            Unit* followTarget = AI_VALUE(Unit*, "follow target");
            if (!followTarget || !ai->IsSafe(followTarget))
                return false;

            if ((bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE ||
            bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE) &&
            (bot->GetMotionMaster()->GetCurrent()->GetCurrentTarget() != followTarget ||
            /*bot->InBattleGround() ||*/
            bot->GetTransport()))
                return false;

            // do not try if very close
            Formation* formation = AI_VALUE(Formation*, "formation");
            if (formation)
            {
                WorldLocation loc = formation->GetLocation();
                if (!Formation::IsNullLocation(loc) && loc.mapId != -1)
                {
                    if (sServerFacade.getDistance2d(bot, loc.x, loc.y) < ai->GetRange("follow") && fabs(src.getZ() - loc.z) < ai->GetRange("follow"))
                        return false;
                }
            }

            dest = WorldPosition(followTarget);
            distanceTo = 30.f;
            distanceFrom = 40.f;
        }

        if (options == "chase")
        {
            Unit* chaseTarget = AI_VALUE(Unit*, "current target");
            if (!chaseTarget || !ai->IsSafe(chaseTarget))
                return false;

            if ((bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE ||
            bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE) &&
            (bot->GetMotionMaster()->GetCurrent()->GetCurrentTarget() != chaseTarget ||
            /*bot->InBattleGround() ||*/
            bot->GetTransport()))
                return false;

            dest = WorldPosition(chaseTarget);
            distanceTo = 30.f;
            distanceFrom = sPlayerbotAIConfig.sightDistance;
        }

        if (toPosition)
        {
            ai::PositionMap& posMap = context->GetValue<ai::PositionMap&>("position")->Get();
            ai::PositionEntry pos = context->GetValue<ai::PositionMap&>("position")->Get()[options];
            if (!pos.isSet())
                return false;

            dest = WorldPosition(pos.Get());
            distanceTo = 50.0f;
            distanceFrom = 50.0f;
        }

        // try nearby random points
        if (!jumpPoint && ai->AllowActivity())
            jumpPoint = GetPossibleJumpStartForInRange(src, dest, possibleLanding, bot, requiredSpeed, distanceTo, distanceFrom);
        // try with pathfinder
        if (!jumpPoint)
            jumpPoint = GetPossibleJumpStartFor(src, dest, possibleLanding, bot, requiredSpeed, distanceTo, distanceFrom);
        if (jumpPoint && requiredSpeed > 0.f)
        {
            // check if jumping is much faster
            if (options == "follow" || options == "chase" || toPosition)
            {
                if (!IsJumpFasterThanWalking(src, dest, possibleLanding, bot))
                    return false;
            }
            sLog.outDebug("%s: GetPossibleJumpStartFor success! Jump speed: %f", bot->GetName(), requiredSpeed);

            // move to jump point
            if (src != jumpPoint && src.distance(jumpPoint) > sPlayerbotAIConfig.contactDistance)
            {
                if (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
                {
                    std::string text = "Moving to jumping position!";
                    bot->Say(text, (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                }

                if (showLanding)
                {
                    Creature* wpCreature = bot->SummonCreature(2334, jumpPoint.getX(), jumpPoint.getY(), jumpPoint.getZ() - 1, bot->getOrientation(), TEMPSPAWN_TIMED_DESPAWN, 3000);
                    PlayerbotAI::AddAura(wpCreature, 246);

                    float pointAngle = src.GetAngleTo(jumpPoint);
                    sServerFacade.SetFacingTo(bot, pointAngle, true);
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                    SetDuration(sPlayerbotAIConfig.reactDelay);
                    return true;
                }

                return MoveTo(jumpPoint.GetMapId(), jumpPoint.getX(), jumpPoint.getY(), jumpPoint.getZ());
            }
            else // jump from current position
            {
                if (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
                {
                    std::string text = "Jumping to you!";
                    bot->Say(text, (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                }

                if (showLanding)
                {
                    Creature* wpCreature = bot->SummonCreature(2334, possibleLanding.getX(), possibleLanding.getY(), possibleLanding.getZ() - 1, bot->getOrientation(), TEMPSPAWN_TIMED_DESPAWN, 3000);
                    PlayerbotAI::AddAura(wpCreature, 246);

                    float pointAngle = src.GetAngleTo(possibleLanding);
                    sServerFacade.SetFacingTo(bot, pointAngle, true);
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                    SetDuration(sPlayerbotAIConfig.reactDelay);
                    return true;
                }

                float pointAngle = jumpPoint.GetAngleTo(possibleLanding ? possibleLanding : dest);
                sServerFacade.SetFacingTo(bot, pointAngle, true);
                bool success = JumpTowards(jumpPoint, possibleLanding ? possibleLanding : dest, bot, requiredSpeed, possibleLanding);

                return success;
            }
        }
        return false;
    }

    float angle = bot->getOrientation();
    if (jumpBackward)
        angle += M_PI_F;

    float jumpSpeed;
    if (jumpInPlace)
        jumpSpeed = 0.f;
    else
    {
        jumpSpeed = jumpBackward ? bot->GetSpeed(MOVE_WALK) : bot->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed;

        if (options == "r" || options == "random")
        {
            // slow jump
            if (urand(0, 1))
                jumpSpeed = bot->GetSpeed(MOVE_WALK);
        }
    }

    float timeToLand, distToLand, maxHeight;
    bool goodLanding = true;
    std::vector<WorldPosition> path;
    WorldPosition jumpLanding = JumpAction::CalculateJumpParameters(WorldPosition(bot), bot, angle, sPlayerbotAIConfig.jumpVSpeed, jumpSpeed, timeToLand, distToLand, maxHeight, goodLanding, path);
    if (jumpLanding)
    {
        // not jump randomly in the water
        if ((options == "r" || options == "random") && !event.GetOwner() && (jumpLanding.isInWater() || jumpLanding.isUnderWater()))
        {
            sLog.outDetail("%s: Jump random fail: landing in water!", bot->GetName());
            return false;
        }

        // only show landing
        if (showLanding || ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            Creature* wpCreature = bot->SummonCreature(2334, jumpLanding.getX(), jumpLanding.getY(), jumpLanding.getZ() - 1, bot->getOrientation(), TEMPSPAWN_TIMED_DESPAWN, 3000);
            PlayerbotAI::AddAura(wpCreature, 246);
            if (showLanding)
            {
                WorldPosition botPos = WorldPosition(bot);
                float pointAngle = botPos.GetAngleTo(jumpLanding);
                sServerFacade.SetFacingTo(bot, pointAngle, true);
                bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                SetDuration(sPlayerbotAIConfig.reactDelay);
                return true;
            }
        }

        // set highest jump point to relocate
        WorldPosition highestPoint = jumpLanding;
        for (auto& point : path)
        {
            if (point.getZ() > highestPoint.getZ())
                highestPoint = point;
        }

        return DoJump(jumpLanding, highestPoint, angle, sPlayerbotAIConfig.jumpVSpeed, jumpSpeed, timeToLand, distToLand, maxHeight, goodLanding, jumpInPlace, jumpBackward, showLanding);
    }
    return false;
}

WorldPosition JumpAction::CalculateJumpParameters(const WorldPosition& src, Unit* jumper, float angle, float vSpeed, float hSpeed, float &timeToLand, float &distanceToLand, float &maxHeight, bool &goodLanding, std::vector<WorldPosition>& path, float maxJumpHeight)
{
    if (!jumper)
        return WorldPosition();

    // static data
    float const m_gravity = 19.2911f;
    float const timeForMaxHeight = vSpeed / m_gravity;
    float velocity = sqrt(vSpeed * vSpeed + hSpeed * hSpeed);
    double jumpVerticalAngle = 48.f * M_PI / 180; // approximate
    maxHeight = vSpeed * timeForMaxHeight - m_gravity * timeForMaxHeight * timeForMaxHeight / 2;

    // jump in place
    if (hSpeed == 0.f)
    {
        timeToLand = timeForMaxHeight * 2;
        distanceToLand = 0.f;

        // calculate collision up
        float ox, oy, oz;
        ox = src.getX();
        oy = src.getY();
        oz = src.getZ() + 0.5f;
        float fx = ox;
        float fy = oy;
        float fz = oz + maxHeight + jumper->GetCollisionHeight();
        if (jumper->GetMap()->GetHitPosition(ox, oy, oz, fx, fy, fz, -0.5f))
        {
            // hit object above
            goodLanding = false;
            timeToLand = JumpAction::CalculateJumpTime(fz - oz, vSpeed, true);
            maxHeight = fz;
            return WorldPosition(src.GetMapId(), fx, fy, fz - CONTACT_DISTANCE - jumper->GetCollisionHeight());
        }
        else
        {
            // can jump full height and back
            goodLanding = true;
            maxHeight = fz;
            return src;
        }
    }

    float vsin = sin(angle);
    float vcos = cos(angle);

    // calculate approximate distance on ideal surface
    float rough_distance = 2 * timeForMaxHeight * hSpeed;

    // calculate collision
    float const path_part = rough_distance / 10.0f;
    float check_dist = path_part;
    float ox, oy, oz;
    ox = src.getX();
    oy = src.getY();
    oz = src.getZ() + 0.5f;
    bool foundCollision = false;
    for (auto i = 1; i <= 30; i++)
    {
        // not found - calculate very far
        if (i >= 25 && check_dist > (rough_distance + path_part))
        {
            sLog.outDetail("%s: Jump checks too many, possible no collision!", jumper->GetName());
            check_dist += rough_distance;
        }

        float fx = src.getX() + check_dist * vcos;
        float fy = src.getY() + check_dist * vsin;
        float fz = src.getZ() + 0.5f + float((check_dist * tan(jumpVerticalAngle)) - (m_gravity * check_dist * check_dist)/(2 * velocity * velocity * cos(jumpVerticalAngle) * cos(jumpVerticalAngle)));

        // add collision height while ascending
        bool ascending = fz > src.getZ() && check_dist < (rough_distance / 2);
        if (ascending)
            fz += jumper->GetCollisionHeight();

        // add some collision distances
        fx += jumper->GetCollisionWidth() * vcos;
        fy += jumper->GetCollisionWidth() * vsin;

        foundCollision = jumper->GetMap()->GetHitPosition(ox, oy, oz, fx, fy, fz, -0.5f);

        if (!foundCollision)
        {
            // check distanct collision
            if (ascending)
                fz += jumper->GetCollisionHeight();

            fx += jumper->GetCollisionWidth() * vcos;
            fy += jumper->GetCollisionWidth() * vsin;

            foundCollision = jumper->GetMap()->GetHitPosition(ox, oy, oz + 0.5f, fx, fy, fz, -0.5f);

            if (!foundCollision)
            {
                fx -= jumper->GetCollisionWidth() * vcos;
                fy -= jumper->GetCollisionWidth() * vsin;
                if (ascending)
                    fz -= jumper->GetCollisionHeight();
            }
        }

        path.push_back(WorldPosition(src.GetMapId(), fx, fy, fz));

        // vmaps collision not found - check maps (terrain or water)
        if (!foundCollision && !ascending)
        {
            // check ground below current previous point
            float prevGroundZ = oz;
            float nextGroundZ = fz;
            jumper->UpdateAllowedPositionZ(ox, oy, prevGroundZ);
            jumper->UpdateAllowedPositionZ(fx, fy, nextGroundZ);
            // calculated point is lower than terrain - land on terrain
            if (fz < nextGroundZ && oz > prevGroundZ)
            {
                foundCollision = true;
                fx = ox;
                fy = oy;
                fz = prevGroundZ;
            }
        }

        if (maxJumpHeight > 0.f && fabs(src.getZ() - fz) > maxJumpHeight)
            return WorldPosition();

        // vmaps collision found
        if (foundCollision)
        {
            // hit something while ascending
            if (ascending)
            {
                goodLanding = false;
                // reduce landing height by collision height
                float fz_mod = fz - CONTACT_DISTANCE - jumper->GetCollisionHeight();
                jumper->GetMap()->GetHitPosition(fx, fy, fz, fx, fy, fz_mod, -0.5f);
                fz = fz_mod;
                //fz = fz - CONTACT_DISTANCE - jumper->GetCollisionHeight();
            }

            WorldPosition destination = WorldPosition(src.GetMapId(), fx, fy ,fz);
            if (!IsJumpSafe(src, destination, jumper))
                return WorldPosition();

            distanceToLand = sqrtf(src.sqDistance2d(destination));
            timeToLand = CalculateJumpTime(fz - (src.getZ() + 0.5f), vSpeed, ascending);

            // some error in time calculations - cancel the jump
            if (timeToLand == 0.f)
                return WorldPosition();

            // maybe hit a wall while descending
            if (goodLanding)
            {
                float groundZ = destination.getZ() + 0.5f;
                if (!destination.isInWater())
                    jumper->UpdateAllowedPositionZ(destination.getX(), destination.getY(), groundZ);
                // set to fall after land if not at the ground
                if (groundZ < destination.getZ() && fabs(oz - destination.getZ()) > 5.0f)
                {
                    goodLanding = false;
                }
            }

            maxHeight = fz;
            return destination;
        }

        ox = fx;
        oy = fy;
        oz = fz/* + 0.5f*/;

        check_dist += path_part;
    }

    sLog.outDetail("%s: Jump collision fail to calculate!", jumper->GetName());
    timeToLand = 0.f;
    distanceToLand = 0.f;
    return WorldPosition();
}

float JumpAction::CalculateJumpTime(float srcZ, float destZ, float vSpeed, float hSpeed, float distance)
{
    double jumpVerticalAngle = 48.6717 * M_PI / 180;
    float m_gravity = 19.2911f;
    float timeForMaxHeight = vSpeed / m_gravity;
    float rough_distance = 2 * timeForMaxHeight * hSpeed;
    bool ascending = destZ > srcZ && distance < (rough_distance / 2);
    float jumpTime = 0.f;
    float sqroot = vSpeed * vSpeed - (m_gravity * 2 * (destZ - srcZ));
    // some collision error allowing jump above max height
    if (sqroot < 0.f)
    {
        sLog.outDetail("Jump above max height! srcZ: %f, destZ: %f, distance: %f", srcZ, destZ, distance);
        return 0.f;
    }

    if (ascending)
    {
        jumpTime = (vSpeed - sqrtf(sqroot)) / m_gravity;
    }
    else
        jumpTime = (vSpeed + sqrtf(sqroot)) / m_gravity;

    return jumpTime;
}

float JumpAction::CalculateJumpTime(float z_diff, float vSpeed, bool ascending)
{
    float m_gravity = 19.2911f;
    float jumpTime = 0.f;
    float sqroot = vSpeed * vSpeed - (m_gravity * 2 * (z_diff));
    // some collision error allowing jump above max height
    if (sqroot < 0.f)
    {
        sLog.outDetail("Jump above max height!");
        return 0.f;
    }

    if (ascending)
    {
        jumpTime = (vSpeed - sqrtf(sqroot)) / m_gravity;
    }
    else
        jumpTime = (vSpeed + sqrtf(sqroot)) / m_gravity;

    return jumpTime;
}

bool JumpAction::IsJumpSafe(const WorldPosition &src, const WorldPosition &dest, Unit* jumper)
{
    return CanLand(dest, jumper) && IsNotMagmaSlime(dest, jumper);
}

bool JumpAction::CanWalkTo(const WorldPosition &src, const WorldPosition &dest, Unit* jumper, float maxDistance)
{
    if (!src || !dest)
        return false;

    if (src.GetMapId() != dest.GetMapId())
        return false;

    if (src.fDist(dest) > sPlayerbotAIConfig.sightDistance)
        return false;

    std::vector<WorldPosition> path = dest.GetPathStepFrom(src, jumper, true);
    if (path.empty())
    {
        sLog.outDetail("%s: Jump CanWalkTo Fail! No Path!", jumper->GetName());
        return false;
    }

    float pathLength = src.GetPathLength(path);
    // todo add config
    if (pathLength > maxDistance)
    {
        sLog.outDetail("%s: Jump CanWalkTo Fail! Path is too big! Max Distance: %f, Path Distance %f", jumper->GetName(), maxDistance, pathLength);
        return false;
    }

    return true;
}

bool JumpAction::IsJumpFasterThanWalking(const WorldPosition& src, const WorldPosition& dest, const WorldPosition& jumpLanding, Unit* jumper, float maxDistance)
{
    if (!src || !dest || !jumpLanding)
        return false;

    if (src.GetMapId() != dest.GetMapId() || src.GetMapId() != jumpLanding.GetMapId())
        return false;

    // landing too far from destination
    if (dest.fDist(jumpLanding) > maxDistance)
        return false;

    std::vector<WorldPosition> pathWalk = dest.GetPathStepFrom(src, jumper, true);
    std::vector<WorldPosition> pathJump = dest.GetPathStepFrom(jumpLanding, jumper, true);
    if (pathJump.empty())
        return false;
    if (pathWalk.empty())
        return true;

    float pathLengthWalk = src.GetPathLength(pathWalk);
    float pathLengthJump = jumpLanding.GetPathLength(pathJump);

    if (pathLengthWalk > 20.f && pathLengthWalk > (pathLengthJump * 2))
    {
        sLog.outDebug("%s: Jump IsJumpFasterThanWalking Jumping is faster than walking! Walk Distance: %f, Jump Distance: %f", jumper->GetName(), pathLengthWalk, pathLengthJump);
        return true;
    }

    sLog.outDebug("%s: Jump IsJumpFasterThanWalking Jumping is slower than walking! Walk Distance: %f, Jump Distance: %f", jumper->GetName(), pathLengthWalk, pathLengthJump);
    return false;
}

bool JumpAction::CanLand(const ai::WorldPosition &dest, Unit *jumper)
{
    // do not let jump to abyss
    float mapHeightCheck = jumper->GetMap()->GetHeight(dest.getX(), dest.getY(), dest.getZ() + 0.5f);
    if (mapHeightCheck <= INVALID_HEIGHT)
    {
        sLog.outDetail("%s: Jump Fail! Invalid landing height: %f", jumper->GetName(), mapHeightCheck);
        return false;
    }
    return true;
}

bool JumpAction::IsNotMagmaSlime(const WorldPosition &dest, Unit *jumper)
{
    if (const TerrainInfo* terrain = dest.GetTerrain())
    {
        if (!terrain->CanCheckLiquidLevel(dest.getX(), dest.getY()))
            return true;

        GridMapLiquidData data;
        if (terrain->getLiquidStatus(dest.getX(), dest.getY(), dest.getZ(), MAP_ALL_LIQUIDS, &data) == LIQUID_MAP_NO_WATER)
            return true;

        switch (data.type_flags)
        {
            case MAP_LIQUID_TYPE_MAGMA:
            case MAP_LIQUID_TYPE_SLIME:
            {
                sLog.outDetail("%s: Jump Fail! Landing is Magma or Slime!", jumper->GetName());
                return false;
            }
        }
    }

    return true;
}

bool JumpAction::CanJumpTo(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possiblelanding, float& jumpAngle, Unit* jumper, float jumpSpeed, float maxDistance)
{
    if (!src || !dest)
        return false;

    if (src.GetMapId() != dest.GetMapId())
        return false;

    if (src.fDist(dest) > sPlayerbotAIConfig.sightDistance)
        return false;

    // some preparation
    static float m_gravity = 19.2911f;
    float timeForMaxHeight = sPlayerbotAIConfig.jumpVSpeed / m_gravity;
    float maxHeight = sPlayerbotAIConfig.jumpVSpeed * timeForMaxHeight - m_gravity * timeForMaxHeight * timeForMaxHeight / 2;

    // can't jump too high
    if ((src.getZ() + maxHeight) < dest.getZ())
        return false;

    float destAngle = src.GetAngleTo(dest);
    for (float angle = destAngle; angle <= destAngle + 2 * M_PI; angle += M_PI_F / 4.0f)
    {
        float timeToLand, distToLand;
        bool goodLanding = true;
        std::vector<WorldPosition> path;
        WorldPosition jumpLanding = JumpAction::CalculateJumpParameters(src, jumper, angle, sPlayerbotAIConfig.jumpVSpeed, jumpSpeed, timeToLand, distToLand, maxHeight, goodLanding, path);
        if (jumpLanding && CanWalkTo(jumpLanding, dest, jumper, maxDistance))
        {
            jumpAngle = angle;
            possiblelanding = jumpLanding;
            return true;
        }
    }

    return false;
}

bool JumpAction::JumpTowards(const ai::WorldPosition &src, const ai::WorldPosition &dest, Unit* jumper, float jumpSpeed, bool preSetLanding)
{
    if (src.GetMapId() != dest.GetMapId())
        return false;

    if (src.fDist(dest) > sPlayerbotAIConfig.sightDistance)
        return false;

    bool jumpInPlace = false;
    bool jumpBackward = false;

    float angle = src.GetAngleTo(dest);

    float timeToLand, distToLand, maxHeight;
    bool goodLanding = true;
    std::vector<WorldPosition> path;
    WorldPosition jumpLanding = JumpAction::CalculateJumpParameters(src, jumper, angle, sPlayerbotAIConfig.jumpVSpeed, jumpSpeed, timeToLand, distToLand, maxHeight, goodLanding, path);
    sLog.outDebug("%s: JumpTowards attempt! Jump speed: %f", bot->GetName(), jumpSpeed);
    if (jumpLanding && goodLanding)
    {
        // set highest jump point to relocate
        WorldPosition highestPoint = dest;
        for (auto& point : path)
        {
            if (point.getZ() > highestPoint.getZ())
                highestPoint = point;
        }

        return DoJump((preSetLanding ? dest : jumpLanding), highestPoint, angle, sPlayerbotAIConfig.jumpVSpeed, jumpSpeed, timeToLand, distToLand, maxHeight, goodLanding, jumpInPlace, jumpBackward, false);
    }

    sLog.outDetail("%s: Jump ForwardTo Fail!", jumper->GetName());
    return false;
}

bool JumpAction::DoJump(const WorldPosition &dest, const WorldPosition& highestPoint, float angle, float vSpeed, float hSpeed, float timeToLand, float distanceToLand, float maxHeight, bool goodLanding, bool jumpInPlace, bool jumpBackward, bool showOnly)
{
    if (!dest)
        return false;

    WorldPosition landing = dest;

    // fix height
    if (goodLanding && !dest.isInWater())
    {
        float ox = dest.getX();
        float oy = dest.getY();
        float oz = dest.getZ() + 0.5f;
        bot->UpdateAllowedPositionZ(ox, oy, oz);
        landing = WorldPosition(dest.GetMapId(), ox, oy, oz);
    }

    if (!goodLanding)
        ai->SetFallAfterJump();

    ai->InterruptSpell(false);
    ai->StopMoving();
    ai->SetJumpDestination(landing);
    bot->SetFallInformation(0, maxHeight);

    bool slowJump = false;// !jumpBackward && hSpeed == bot->GetSpeed(MOVE_WALK);
    // TODO calculate slow jump (jump + move forward)

    // send move packet before jump
    if (!jumpInPlace && !slowJump)
    {
        bot->m_movementInfo.AddMovementFlag(jumpBackward ? MOVEFLAG_BACKWARD : MOVEFLAG_FORWARD);
        WorldPacket move(jumpBackward ? MSG_MOVE_START_BACKWARD : MSG_MOVE_START_FORWARD);
// write packet info
        move << bot->m_movementInfo;
        ai->QueuePacket(move);
    }

    float vsin = jumpInPlace ? 0 : sin(angle);
    float vcos = jumpInPlace ? 1 : cos(angle);

    // write jump info
    uint32 curTime = sWorld.GetCurrentMSTime();
    uint32 jumpTime = curTime + sWorld.GetAverageDiff() * 2 + uint32(timeToLand * static_cast<float>(IN_MILLISECONDS));
    ai->SetJumpTime(jumpTime);
    bot->m_movementInfo.jump.zspeed = -vSpeed;
    bot->m_movementInfo.jump.cosAngle = vcos;
    bot->m_movementInfo.jump.sinAngle = vsin;
    bot->m_movementInfo.jump.xyspeed = slowJump ? 0.f : hSpeed;

    sLog.outDetail("%s: Jump x: %f, y: %f, z: %f, time: %f, dist: %f, inPlace: %u, landTime: %u, curTime: %u", bot->GetName(), landing.getX(), landing.getY(), landing.getZ(), timeToLand, distanceToLand, jumpInPlace, jumpTime, curTime);

    // send jump packet
    bot->m_movementInfo.AddMovementFlag(MOVEFLAG_JUMPING);

    // client doesn't seem to show proper bigger jump with faster than real speeds
    if (vSpeed > (7.96f * 1.3f) || hSpeed > (bot->GetSpeed(MOVE_RUN) * 1.3f))
    {
        WorldPacket data(MSG_MOVE_KNOCK_BACK);
        data << bot->GetMover()->GetPackGUID();
        data << bot->m_movementInfo;
        data << bot->m_movementInfo.jump.cosAngle;
        data << bot->m_movementInfo.jump.sinAngle;
        data << bot->m_movementInfo.jump.xyspeed;
        data << bot->m_movementInfo.jump.zspeed;
        bot->GetMover()->SendMessageToSetExcept(data, bot);
    }
    else
    {
        WorldPacket jump(MSG_MOVE_JUMP);
        // write packet info
        jump << bot->m_movementInfo;
        ai->QueuePacket(jump);
    }

    // send move packet after jump
    if (!jumpInPlace && slowJump)
    {
        bot->m_movementInfo.AddMovementFlag(jumpBackward ? MOVEFLAG_BACKWARD : MOVEFLAG_FORWARD);
        bot->m_movementInfo.jump.xyspeed = hSpeed;
        WorldPacket move(jumpBackward ? MSG_MOVE_START_BACKWARD : MSG_MOVE_START_FORWARD);
        // write packet info
        move << bot->m_movementInfo;
        ai->QueuePacket(move);
    }

    // change position to highest position
    // todo add in between points to avoid mobs instant aggro before landing
    bot->Relocate(highestPoint.getX(), highestPoint.getY(), highestPoint.getZ());

    if (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
    {
        std::string text = "Jump: cos: " + std::to_string(vcos) + " sin: " + std::to_string(vsin) + " distance: " + std::to_string(distanceToLand) + " speed: " + std::to_string(hSpeed);
        bot->Say(text, (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
    }

    return true;
}

WorldPosition JumpAction::GetPossibleJumpStartFor(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possibleLanding, Unit* jumper, float &requiredSpeed, float distanceTo, float distanceFrom)
{
    if (!src || !dest)
        return WorldPosition();

    if (src.GetMapId() != dest.GetMapId())
        return WorldPosition();

    if (src.fDist(dest) > sPlayerbotAIConfig.sightDistance)
        return WorldPosition();

    float jumpAngle;
    // try jump from where at
    if (CanJumpTo(src, dest, possibleLanding, jumpAngle, jumper, bot->GetSpeed(MOVE_WALK), distanceFrom))
    {
        requiredSpeed = bot->GetSpeed(MOVE_WALK);
        return src;
    }
    else
    {
        // try slow jump
        if (CanJumpTo(src, dest, possibleLanding, jumpAngle, jumper, jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed, distanceFrom))
        {
            requiredSpeed = jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed;
            return src;
        }
    }

    // try find a closer point
    std::vector<WorldPosition> path = dest.GetPathStepFrom(src, jumper);

    // no path found closer to it...
    if (path.empty() || path.size() == 2)
    {
        sLog.outDetail("%s: Jump Fail! Can't pathfind closer!", jumper->GetName());
        return WorldPosition();
    }

    float pathLength = src.GetPathLength(path);
    for (auto& p : path)
    {
        if (p.fDist(src) > distanceTo)
            break;

        // try slow jump
        if (CanJumpTo(p, dest, possibleLanding, jumpAngle, jumper, bot->GetSpeed(MOVE_WALK), distanceFrom))
        {
            requiredSpeed = bot->GetSpeed(MOVE_WALK);
            return p;
        }
        else
        {
            // try fast jump
            if (CanJumpTo(p, dest, possibleLanding, jumpAngle, jumper, jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed, distanceFrom))
            {
                requiredSpeed = jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed;
                return p;
            }
            else
                continue;
        }
    }

    sLog.outDetail("%s: GetPossibleJumpStartFor Failed to find jump point!", jumper->GetName());
    return WorldPosition();
}

WorldPosition JumpAction::GetPossibleJumpStartForInRange(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possibleLanding, Unit* jumper, float& requiredSpeed, float distanceTo, float distanceFrom)
{
    if (!src || !dest)
        return WorldPosition();

    if (src.GetMapId() != dest.GetMapId())
        return WorldPosition();

    if (src.fDist(dest) > sPlayerbotAIConfig.sightDistance)
        return WorldPosition();

    float jumpAngle;
    // try jump from where at
    if (CanJumpTo(src, dest, possibleLanding, jumpAngle, jumper, jumper->GetSpeed(MOVE_WALK), distanceFrom))
    {
        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            jumper->SummonCreature(VISUAL_WAYPOINT, src.getX(), src.getY(), src.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000);
        }
        requiredSpeed = jumper->GetSpeed(MOVE_WALK);
        return src;
    }
    else
    {
        // try slow jump
        if (CanJumpTo(src, dest, possibleLanding, jumpAngle, jumper, jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed, distanceFrom))
        {
            if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
            {
                jumper->SummonCreature(VISUAL_WAYPOINT, src.getX(), src.getY(), src.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000);
            }
            requiredSpeed = jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed;
            return src;
        }
    }

    // try find a point in range to jump from

    // some limitations
    /*if (range > 40.0f)
        range = 40.0f;
    if (range < 0.1f)
        range = 5.0f;*/

    std::vector<WorldPosition> jumpPoints;

    float gx, gy, gz;
    gx = src.getX();
    gy = src.getY();
    gz = src.getZ();

    uint32 tries = 1;
    uint32 successes = 0;
    uint32 attempts = 0;
    uint32 startTime = WorldTimer::getMSTime();
    for (; tries < 500; ++tries)
    {
        gx = src.getX();
        gy = src.getY();
        gz = src.getZ();
        if (jumper->GetMap()->GetReachableRandomPointOnGround(gx, gy, gz, distanceTo))
        {
            WorldPosition p(jumper->GetMapId(), gx, gy, gz);
            ++attempts;
            if (attempts >= 100)
                break;

            // point is not reachable
            if (!CanWalkTo(src, p, jumper))
                continue;

            if (CanJumpTo(p, dest, possibleLanding, jumpAngle, jumper, jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed, distanceFrom))
            {
                requiredSpeed = jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed;
                jumpPoints.push_back(p);
            }
            else
                continue;

            if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
            {
                jumper->SummonCreature(VISUAL_WAYPOINT, gx, gy, gz, 0, TEMPSPAWN_TIMED_DESPAWN, 10000);
            }
            ++successes;
            if (successes >= 10)
                break;
        }
    }

    if (!jumpPoints.empty())
    {
        WorldPosition closest = src.closestSq(jumpPoints);
        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            Creature* wpCreature = bot->SummonCreature(15631, closest.getX(), closest.getY(), closest.getZ(), closest.getO(), TEMPSPAWN_TIMED_DESPAWN, 2000.0f);
            wpCreature->SetObjectScale(0.2f);
        }

        // recalculate to get landing and angle
        CanJumpTo(closest, dest, possibleLanding, jumpAngle, jumper, jumper->GetSpeedRate(MOVE_RUN) * sPlayerbotAIConfig.jumpHSpeed, distanceFrom);
        return closest;
    }

    sLog.outDetail("%s: GetPossibleJumpStartFor Failed to find jump point!", jumper->GetName());
    return WorldPosition();
}
