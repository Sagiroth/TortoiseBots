#include "playerbot/playerbot.h"
#include "DebugAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include <playerbot/TravelNode.h>
#include "ChooseTravelTargetAction.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/strategy/actions/RpgSubActions.h"
#include "playerbot/LootObjectStack.h"
#include "GameEventMgr.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "Transports/Transport.h"
#include "Maps/PathFinder.h"
#include "playerbot/PlayerbotLLMInterface.h"

#include "vmap/VMapFactory.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

#include <iomanip>
#include <typeindex>
#include "SayAction.h"
#include "Maps/MoveMap.h"

using namespace ai;
using namespace MaNGOS;

bool DebugAction::HandleMotion(Event& event, Player* requester, const std::string& text)
{
    Unit* requesterTarget = nullptr;
    if (requester->GetSelectionGuid())
    {
        requesterTarget = ai->GetUnit(requester->GetSelectionGuid());
    }

    Unit* motionBot = bot;
    Unit* motionTarget = requesterTarget;

    if (text.find("motion") != 0)
    {
        if (requesterTarget)
        {
            motionBot = requesterTarget;
            motionTarget = requester;
        }
        else
        {
            motionBot = requester;
            motionTarget = bot;
        }
    }

    MotionMaster* mm = motionBot->GetMotionMaster();

    MovementGeneratorType type = mm->GetCurrentMovementGeneratorType();

    std::string sType = "TODO"; // GetMoveTypeStr(type);

    Unit* cTarget = sServerFacade.GetChaseTarget(motionBot);
    float cAngle = sServerFacade.GetChaseAngle(motionBot);
    float cOffset = sServerFacade.GetChaseOffset(motionBot);
    std::string cTargetName = cTarget ? cTarget->GetName() : "none";

    std::string motionName = motionBot->GetName();

    ai->TellPlayer(requester, motionName + " :" + sType + " (" + cTargetName + " a:" + std::to_string(cAngle) + " o:" + std::to_string(cOffset) + ")");

    if (!requesterTarget)
    {
        requesterTarget = requester;
    }

    if (text.size() > 7)
    {
        std::string cmd = text.substr(7);

        if (cmd == "clear")
            mm->Clear();
        else if (cmd == "reset")
            mm->Clear(true);
        else if (cmd == "clearall")
            mm->Clear(false, true);
        else if (cmd == "expire")
            mm->MovementExpired();
        else if (cmd == "flee")
            mm->MoveFleeing(motionTarget, 10);
        else if (cmd == "followmain")
            mm->MoveFollow(motionTarget, 5.0f, 0.0f);
        else if (cmd == "follow")
            mm->MoveFollow(motionTarget, 5, 0);
        else if (cmd == "dist")
            ai->TellError(requester, "Distance movement is not available in the Tortoise core.");
        else if (cmd == "update")
            mm->UpdateMotion(10);
        else if (cmd == "chase")
            mm->MoveChase(motionTarget, 5, 0);
        else if (cmd == "fall")
            ai->TellError(requester, "Forced falling movement is not available in the Tortoise core.");
        else if (cmd == "formation")
            ai->TellError(requester, "Formation movement is not available in the Tortoise core.");

        std::string sType = "TODO"; // GetMoveTypeStr(type);
        ai->TellPlayer(requester, "new:" + sType);
    }
    return true;
}

bool DebugAction::HandleTransport(Event& event, Player* requester, const std::string& text)
{
    std::vector<GenericTransport*> transports;

    for (auto trans : WorldPosition(bot).GetTransports())
        transports.push_back(trans);

    WorldPosition botPos(bot);

    //Closest transport last = below in chat.
    std::sort(transports.begin(), transports.end(), [botPos](GenericTransport* i, GenericTransport* j) { return botPos.distance(i) > botPos.distance(j); });

    for (auto trans : transports)
    {
        GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(trans->GetEntry());
        std::ostringstream out;

        std::string transportName = trans->GetName();
        if (transportName.empty())
            transportName = data->name;

        out << transportName << " (" << trans->GetEntry() << ")";

        if (bot->GetTransport() == trans)
            out << "[inside]";

        if (WorldPosition(bot).isOnTransport(trans))
        {
            out << "[ontop]";
        }

        out << " dist:" << std::fixed << std::setprecision(2) << botPos.distance(trans) << " offset: " << (botPos - trans).print(2, true);

        ai->TellPlayer(requester, out);
    }

    return true;
}

bool DebugAction::HandlePointOnTrans(Event& event, Player* requester, const std::string& text)
{

    uint32 radius = 20;
    std::string param;

    if (text.length() > std::string("pointontrans").size())
    {
        param = text.substr(std::string("pointontrans").size() + 1);
        radius = stoi(param);
    }

    //Get transport
    std::vector<GenericTransport*> transports;

    for (auto trans : WorldPosition(bot).GetTransports())
        transports.push_back(trans);

    WorldPosition botPos(bot);

    std::sort(transports.begin(), transports.end(), [botPos](GenericTransport* i, GenericTransport* j) { return botPos.distance(i) > botPos.distance(j); });

    GenericTransport* transport = transports.back();

    GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(transport->GetEntry());
    std::string transportName = transport->GetName();
    if (transportName.empty())
        transportName = data->name;

    std::ostringstream out;
    out << "found: " << transportName << " (" << transport->GetEntry() << ")";
    ai->TellPlayer(requester, out);

    //Get point on transport and path.
    std::vector<WorldPosition> path;

    WorldPosition pointOnTrans = WorldPosition(bot).RandomPointOnTrans(transport, radius, bot, path);

    if (!pointOnTrans)
    {
        ai->TellPlayer(requester, "No points found.");
        return false;
    }

    WorldPosition transPos(transport);
    transPos.SetTranpotHeightToFloor(transport->GetEntry());

    out << "point at offset: " << (pointOnTrans - transPos).print(1, true);
    out << " path size: " << path.size();

    //Place WP at point.
    GenericTransport* botTrans = bot->GetTransport();
    bot->SetTransport(nullptr);

    Creature* wpCreature = bot->SummonCreature(2334, pointOnTrans.getX(), pointOnTrans.getY(), pointOnTrans.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000.0f);
    if (wpCreature)
    {
        transport->AddPassenger(wpCreature);
        wpCreature->NearTeleportTo(pointOnTrans.getX(), pointOnTrans.getY(), pointOnTrans.getZ(), wpCreature->getOrientation());
        ai->AddAura(wpCreature, 246);
    }
    bot->SetTransport(botTrans);

    //Check if we have good path.
    if (wpCreature && path.size())
    {
        WorldPosition pathEnd = path.back();
        pathEnd.CalculatePassengerPosition(transport);

        if (pathEnd.distance(pointOnTrans) < 1.0f)
        {
            out << " ON TRANS!!";
            ai->AddAura(wpCreature, 1130);
        }
    }

    ai->TellPlayer(requester, out);

    return true;
    /*
    uint32 radius = 20;

    for (float x = radius * -1.0f; x < radius; x += 1.0f)
    {
        for (float y = radius * -1.0f; y < radius; y += 1.0f)
        {
            if (x * x + y * y > radius * radius)
                continue;

            WorldPosition botPos(bot);
            WorldPosition transPos(transport);

            WorldPosition pos = transPos + WorldPosition(0, x, y);

            transPos.SetTranpotHeightToFloor(transport->GetEntry());

            bool onTrans = pos.SetOnTransport(transport, 0.5, -0.5);

            if (!onTrans)
                continue;

            Player* pathBot = bot;
            GenericTransport* botTrans = bot->GetTransport();

            bot->SetTransport(nullptr);

            Creature* wpCreature = bot->SummonCreature(2334, pos.getX(), pos.getY(), pos.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000.0f);
            if (!wpCreature)
            {
                bot->SetTransport(botTrans);
                continue;
            }
            transport->AddPassenger(wpCreature);
            wpCreature->NearTeleportTo(pos.getX(), pos.getY(), pos.getZ(), wpCreature->getOrientation());
            ai->AddAura(wpCreature, 246);

            bot->SetTransport(transport);
            std::unique_ptr<PathFinder> pathfinder = std::make_unique<PathFinder>(bot, true);
            WorldPosition tStart = bot, tEnd = pos;
            tStart.CalculatePassengerOffset(transport);
            tEnd.CalculatePassengerOffset(transport);
            pathfinder->calculate(tStart.GetVector3(), tEnd.GetVector3(), false);

            if (!(pathfinder->getPathType() & PATHFIND_NORMAL))
                continue;

            std::vector<WorldPosition> path = pos.fromPointsArray(pathfinder->getPath());

            if (path.size() < 2 || path.front().distance(path[1]) > 10.0f)
                continue;

            ai->TellPlayer(ai->GetMaster(), "Height diff:" + std::to_string(pos.getZ() - transPos.getZ()));

            WorldPosition rp = path.back();
            rp.CalculatePassengerPosition(transport);

            if (rp.distance(pos) < 1.0f)
                ai->AddAura(wpCreature, 1130);

            bot->SetTransport(botTrans);
        }
    }

    return true;
    */
}

bool DebugAction::HandleDoTransport(Event& event, Player* requester, const std::string& text)
{
    std::string param;

    if (text.length() > std::string("dotrans").size())
        param = text.substr(std::string("dotrans").size() + 1);

    WorldPosition botPos(bot), transPos(bot);

    uint32 entry = 0;
    float distance = FLT_MAX;

    for (auto trans : botPos.GetTransports())
    {
        float dist = botPos.distance(trans);

        if (dist < distance)
        {
            distance = dist;
            entry = trans->GetEntry();
            transPos = trans;
        }
    }

    MovementAction::UseTransport(ai, entry, transPos, requester, param.find("tele") != std::string::npos);

    return true;
}

bool DebugAction::HandleOnTrans(Event& event, Player* requester, const std::string& text)
{
    uint32 radius = 20;

    if (text.length() > std::string("ontrans").size())
    {
        radius = stoi(text.substr(std::string("ontrans").size() + 1));
    }

    WorldPosition botPos(bot);

    float distance = FLT_MAX;

    GenericTransport* transport = nullptr;
    for (auto trans : botPos.GetTransports())
    {
        float dist = botPos.distance(trans);
        if (dist < distance)
        {
            transport = trans;
            distance = dist;
        }
    }

    if (!transport)
    {
        return false;
    }

    GenericTransport* botTrans = bot->GetTransport();

    std::vector<WorldPosition> path;

    WorldPosition transPos = botPos.RandomPointOnTrans(transport, 20.0f, bot, path);

    if (!transPos)
        return false;

    bot->SetTransport(botTrans);

    if (path.empty())
    {
        ai->TellPlayer(requester, "No point, trying to get near transport first.");
        path = WorldPosition(transport).GetPathStepFrom(botPos, bot);

        if (path.empty())
            return false;
    }
    else
    {
        ai->TellPlayer(requester, "Found point on transport, trying to walk there.");
        transport->AddPassenger(bot);

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
            if (!wpCreature)
                continue;

            transport->AddPassenger(wpCreature);

            wpCreature->NearTeleportTo(p.getX(), p.getY(), p.getZ(), wpCreature->getOrientation());

            ai->AddAura(wpCreature, 246);

            if (p == path.back())
                ai->AddAura(wpCreature, 1130);
        }
    }

    bot->GetMotionMaster()->Clear();

    std::vector<G3D::Vector3> pointPath = transPos.toPointsArray(path);
    bot->GetMotionMaster()->MovePoint(0, pointPath.back().x, pointPath.back().y, pointPath.back().z);

    return true;
}

bool DebugAction::HandleOffTrans(Event& event, Player* requester, const std::string& text)
{
    if (!bot->GetTransport())
    {
        ai->TellPlayer(requester, "Bot not on transport.");
        return false;
    }

    GenericTransport* transport = bot->GetTransport();

    WorldPosition exitPos(requester);

    transport->RemovePassenger(bot);

    bot->NearTeleportTo(bot->m_movementInfo.pos.x, bot->m_movementInfo.pos.y, bot->m_movementInfo.pos.z, bot->m_movementInfo.pos.o);

    std::vector<WorldPosition> path = WorldPosition(bot).GetPathStepFrom(exitPos, bot, false);

    if (path.empty())
    {
        ai->TellPlayer(requester, "No path to exitPos.");
        return false;
    }

    if (exitPos.sqDistance(path.back()) > 5.0f)
    {
        ai->TellPlayer(requester, "Path doesn't get to exit pos.");
        return false;
    }

    bot->GetMotionMaster()->Clear();

    std::vector<G3D::Vector3> pointPath = exitPos.toPointsArray(path);

    bot->GetMotionMaster()->MovePoint(0, pointPath.back().x, pointPath.back().y, pointPath.back().z);

    return true;
}

bool DebugAction::HandlePathable(Event& event, Player* requester, const std::string& text)
{
    uint32 radius = 10;

    if (text.length() > std::string("pathable").size())
        radius = stoi(text.substr(std::string("pathable").size() + 1));

    GenericTransport* transport = nullptr;
    for (auto trans : WorldPosition(bot).GetTransports(bot->GetInstanceId()))
        if (!transport || WorldPosition(bot).distance(trans) < WorldPosition(bot).distance(transport))
            transport = trans;

    for (float x = radius * -1.0f; x < radius; x += 1.0f)
    {
        for (float y = radius * -1.0f; y < radius; y += 1.0f)
        {
            if (x * x + y * y > radius * radius)
                continue;

            WorldPosition botPos(bot);
            WorldPosition pos = botPos + WorldPosition(0, x, y, 3.0f);

            Player* pathBot = bot;
            GenericTransport* botTrans = bot->GetTransport();
            GenericTransport* trans = botTrans ? botTrans : transport;

            if (!pos.isOnTransport(trans)) //When trying to calculate a position off the transport, act like the bot is off the transport.
                pathBot = nullptr;
            else
                bot->SetTransport(trans);

            std::vector<WorldPosition> path = pos.GetPathFrom(botPos, pathBot); //Use full pathstep to get proper paths on to transports.

            if (path.empty())
            {
                bot->SetTransport(botTrans);
                continue;
            }

            pos = path.back();

            bool onTrans = pos.isOnTransport(trans);

            if (onTrans) //Summon creature needs to be Summoned on offset coordinates.
            {
                pos.CalculatePassengerOffset(trans);
                bot->SetTransport(trans);
            }
            else //Generate wp off transport so it doesn't spawn on transport.
                bot->SetTransport(nullptr);

            Creature* wpCreature = bot->SummonCreature(2334, pos.getX(), pos.getY(), pos.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000.0f);
            if (!wpCreature)
            {
                bot->SetTransport(botTrans);
                continue;
            }
            ai->AddAura(wpCreature, 246);

            if (onTrans)
                ai->AddAura(wpCreature, 1130);

            bot->SetTransport(botTrans);
        }
    }

    return false;
}

bool DebugAction::HandleRandomSpot(Event& event, Player* requester, const std::string& text)
{
    ai->TellError(requester, "Random path points are unavailable: the pinned Tortoise core does not expose navmesh random-point generation.",
        PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true);
    return false;
}

bool DebugAction::HandlePrintMap(Event& event, Player* requester, const std::string& text)
{
    sTravelNodeMap.printMap();
    sTravelNodeMap.printNodeStore();
    return true;
}

bool DebugAction::HandleTransanal(Event& event, Player* requester, const std::string& text)
{
    sLog.outString("Starting transport analysis...");

    sPlayerbotAIConfig.openLog("transportanalysis.csv", "w", true);

    uint32 shiftx = 0;
    uint32 shifty = 0;

    for (auto& [mapId, map] : sMapMgr.Maps())
    {
        for (auto& transport : WorldPosition(map->GetId(), 1, 1).GetTransports())
        {
            std::string transportName = transport->GetName();
            GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(transport->GetEntry());
            if (transportName.empty())
                transportName = data->name;

            sLog.outString("transport %s", transportName.c_str());

            MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
            if (!mmap->GetModelNavMeshQuery(transport->GetDisplayId()))
            {
                sLog.outString("%d has no ModelNavMeshQuery", transport->GetDisplayId());

                continue;
            }

            BarGoLink bar(18 * 80 * 80 * 80);

            shifty += 1;

            shiftx = 0;

            for (float rotation = 0.0f; rotation <= 160.0f; rotation += 20.0f)
            {
                shiftx += 1;
                float rad = rotation * M_PI / 180.0f;

                transport->UpdatePosition(0, 0, 0, rad);

                WorldPosition transPos(transport);
                transPos.SetTranpotHeightToFloor(transport->GetEntry());

                std::vector<WorldPosition> hitPoints;

                sLog.outString("rotation %d", (int32)rotation);

                for (float x = -50.0f; x <= 50.0f; x += 2.0f)
                {
                    for (float y = -50.0f; y <= 50.0f; y += 2.0f)
                    {
                        for (float z = -50.0f; z <= 50.0f; z += 2.0f)
                        {
                            bar.step();
                            WorldPosition testPos = transPos + WorldPosition(0, x, y, z);

                            float hx = x, hy = y, hz = z - 50;

                            bool hasHit = map->GetLosHitPosition(testPos.getX(), testPos.getY(), testPos.getZ(), hx, hy, hz, 0.0f);

                            if (!hasHit)
                                continue;

                            WorldPosition hitPos(0, hx, hy, hz);

                            hasHit = false;

                            for (auto& hp : hitPoints)
                                if (hp.sqDistance(hitPos) < 0.1)
                                {
                                    hasHit = true;
                                    break;
                                }

                            if (!hasHit)
                                hitPoints.push_back(hitPos);
                        }
                    }
                }

                if (hitPoints.empty())
                    continue;

                // Diagnostic fixture disabled: core owns Headless sessions, module must not
                // construct synthetic Headless WorldSessions. Skipping this rotation.
                {
                    sLog.outError("TortoiseBots: transanal diagnostic skipped synthetic Headless fixture (core owns sessions)");
                    continue;
                }

            }

            transport->Update(100, 100);

            transport->UpdatePosition(5000, 5000, 0, 0);
        }
    }

    sLog.outString("Transport analysis complete.");
    return true;
}

bool DebugAction::HandleUpdownspace(Event& event, Player* requester, const std::string& text)
{
    float startHeightOffset = 0.0f;

    if (text.length() > std::string("updownspace").size())
    {
        startHeightOffset = atof(text.substr(std::string("updownspace").size() + 1).c_str());
    }

    float botX = bot->getPositionX();
    float botY = bot->getPositionY();
    float botZ = bot->getPositionZ();
    float startZ = botZ + startHeightOffset;

    std::ostringstream out;
    out << "Bot pos: (" << botX << ", " << botY << ", " << botZ << ") offset: " << startHeightOffset << " startZ: " << startZ << "\n";

    float destX_down = botX;
    float destY_down = botY;
    float destZ_down = startZ - 50.0f;
    float vmapDestX_down = botX, vmapDestY_down = botY, vmapDestZ_down = startZ - 50.0f;

    VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(bot->GetMapId(), botX, botY, startZ, destX_down, destY_down, destZ_down, vmapDestX_down, vmapDestY_down, vmapDestZ_down, 0);
    bool vmapHitDown = (vmapDestX_down != botX || vmapDestY_down != botY || fabs(vmapDestZ_down - (startZ - 50.0f)) > 0.1f);

    bool combinedHitDown = bot->GetMap()->GetLosHitPosition(botX, botY, startZ, destX_down, destY_down, destZ_down, 0);

    if (vmapHitDown)
        out << "VMAP DOWN hit at z: " << vmapDestZ_down << "\n";
    else
        out << "VMAP DOWN no hit\n";

    if (combinedHitDown)
    {
        bool isDynamic = (destX_down != vmapDestX_down || destY_down != vmapDestY_down || fabs(destZ_down - vmapDestZ_down) > 0.1f);
        if (isDynamic)
            out << "OBJECT DOWN hit at z: " << destZ_down << "\n";
    }

    float destX_up = botX;
    float destY_up = botY;
    float destZ_up = startZ + 50.0f;
    float vmapDestX_up = botX, vmapDestY_up = botY, vmapDestZ_up = startZ + 50.0f;

    VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(bot->GetMapId(), botX, botY, startZ, destX_up, destY_up, destZ_up, vmapDestX_up, vmapDestY_up, vmapDestZ_up, 0);
    bool vmapHitUp = (vmapDestX_up != botX || vmapDestY_up != botY || fabs(vmapDestZ_up - (startZ + 50.0f)) > 0.1f);

    bool combinedHitUp = bot->GetMap()->GetLosHitPosition(botX, botY, startZ, destX_up, destY_up, destZ_up, 0);

    if (vmapHitUp)
        out << "VMAP UP hit at z: " << vmapDestZ_up << "\n";
    else
        out << "VMAP UP no hit\n";

    if (combinedHitUp)
    {
        bool isDynamic = (destX_up != vmapDestX_up || destY_up != vmapDestY_up || fabs(destZ_up - vmapDestZ_up) > 0.1f);
        if (isDynamic)
            out << "OBJECT UP hit at z: " << destZ_up << "\n";
    }

    ai->TellPlayer(requester, out.str());
    return true;
}

bool DebugAction::HandlePatharound(Event& event, Player* requester, const std::string& text)
{
    float distance = 20.0f;

    if (text.length() > std::string("patharound").size())
    {
        distance = atof(text.substr(std::string("patharound").size() + 1).c_str());
    }

    WorldPosition botPos(bot);
    float botX = botPos.getX();
    float botY = botPos.getY();
    float botZ = botPos.getZ();

    std::ostringstream out;
    out << "Patharound: distance = " << distance << " from (" << botX << ", " << botY << ", " << botZ << ")\n";

    uint32 pathCount = 0;
    uint32 successCount = 0;

    if (bot->GetTransport())
        botPos.CalculatePassengerOffset(bot->GetTransport());

    for (float angle = 0.0f; angle < 360.0f; angle += 20.0f)
    {
        float rad = angle * M_PI / 180.0f;

        WorldPosition target(bot);
        target += WorldPosition(0, distance * cos(rad), distance * sin(rad));

        if (bot->GetTransport())
            target.CalculatePassengerOffset(bot->GetTransport());

        PathFinder pathfinder(bot);
        pathfinder.calculate(botPos.GetVector3(), target.GetVector3(), false);

        if (!(pathfinder.getPathType() & PATHFIND_NORMAL))
        {
            continue;
        }

        std::vector<WorldPosition> path = botPos.fromPointsArray(pathfinder.getPath());

        if (path.size() < 2 || path.front().distance(path[1]) > 10.0f)
        {
            continue;
        }

        bool reachedTarget = false;

        for (size_t i = 0; i < path.size(); ++i)
        {
            WorldPosition p = path[i];

            //if (bot->GetTransport())
            //    p.CalculatePassengerPosition(bot->GetTransport());

            Creature* wpCreature = bot->SummonCreature(2334, p.getX(), p.getY(), p.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 10000.0f);
            if (!wpCreature)
                continue;

            ai->AddAura(wpCreature, 246);

            if (i == path.size() - 1)
            {
                float finalDist = target.distance(p);
                if (finalDist < 5.0f)
                {
                    reachedTarget = true;
                    ai->AddAura(wpCreature, 1130);
                }
            }
        }

        pathCount++;
        if (reachedTarget)
            successCount++;
    }

    out << "Path attempts: " << pathCount << ", reached target: " << successCount;
    ai->TellPlayer(requester, out.str());
    return true;
}

bool DebugAction::HandleHeightForLos(Event& event, Player* requester, const std::string& text)
{
    Player* master = GetMaster();
    if (!master)
    {
        ai->TellPlayer(requester, "No master found");
        return true;
    }

    WorldPosition masterPos(master);
    WorldPosition botPos(bot);

    std::ostringstream out;
    out << "Bot: (" << botPos.print() << ")\n";
    out << "Master: (" << masterPos.print() << ")\n";

    bool initialLos = botPos.IsInLineOfSight(masterPos, 1.5f);
    out << "Initial LoS (1.5f offset): " << (initialLos ? "YES" : "NO") << "\n";

    if (initialLos)
    {
        out << "Already have LoS at offset 1.5f both sides. Total height: 3.0f";
        ai->TellPlayer(requester, out.str());
        return true;
    }

    float heightStep = 0.5f;
    float maxHeight = 50.0f;

    float bestBotHeight = 0.0f;

    out << "Searching for bot height...\n";
    for (float botHeight = 0.0f; botHeight <= maxHeight; botHeight += heightStep)
    {
        bool los = botPos.IsInLineOfSight(masterPos, botHeight);
        if (los)
        {
            out << "Found LoS with bot +" << botHeight;
            bestBotHeight = botHeight;
            break;
        }
    }

    if (bestBotHeight == maxHeight)
    {
        out << "No LoS found within " << maxHeight << " height range";
    }

    ai->TellPlayer(requester, out.str());
    return true;
}
