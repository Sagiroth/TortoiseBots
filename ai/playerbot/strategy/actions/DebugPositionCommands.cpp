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

#include "vmap/VMapFactory.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

#include <iomanip>
#include <typeindex>
#include "Maps/MoveMap.h"

using namespace ai;
using namespace MaNGOS;

PositionTarget DebugAction::ParseLocation(const std::string& param, Player* bot)
{
    PositionTarget result;

    if (param.empty())
        return result;

    std::istringstream iss(param);
    std::vector<float> numbers;
    float num;
    while (iss >> num)
    {
        numbers.push_back(num);
    }

    bool isAllNumbers = numbers.size() > 0 && param.find_first_not_of("0123456789.-+ ") == std::string::npos;

    if (isAllNumbers && numbers.size() <= 4)
    {
        result.valid = true;
        if (numbers.size() == 2)
        {
            result.mapId = bot->GetMapId();
            result.x = numbers[0];
            result.y = numbers[1];
            result.z = bot->getPositionZ();
        }
        else if (numbers.size() == 3)
        {
            result.mapId = (uint32)numbers[0];
            result.x = numbers[1];
            result.y = numbers[2];
            result.z = bot->getPositionZ();
        }
        else if (numbers.size() == 4)
        {
            result.mapId = (uint32)numbers[0];
            result.x = numbers[1];
            result.y = numbers[2];
            result.z = numbers[3];
        }
        return result;
    }

    GameTele const* tele = sObjectMgr.GetGameTele(param);
    if (tele)
    {
        result.valid = true;
        result.mapId = tele->mapId;
        result.x = tele->x;
        result.y = tele->y;
        result.z = tele->z;
        result.name = param;
        return result;
    }

    return result;
}

bool DebugAction::HandlePosition(Event& event, Player* requester, const std::string& text)
{
    std::string param = text.substr(8); // skip "position"
    while (param.size() > 0 && param[0] == ' ')
        param = param.substr(1);

    // Handle subcommands
    if (param.substr(0, 4) == "zone")
    {
        std::string zoneParam = param.size() > 4 ? param.substr(4) : "";
        while (zoneParam.size() > 0 && zoneParam[0] == ' ')
            zoneParam = zoneParam.substr(1);

        uint32 mapId = bot->GetMapId();
        float x = bot->getPositionX();
        float y = bot->getPositionY();

        if (!zoneParam.empty())
        {
            PositionTarget target = ParseLocation(zoneParam, bot);
            if (target.valid)
            {
                mapId = target.mapId;
                x = target.x;
                y = target.y;
            }
            else
            {
                ai->TellPlayer(requester, "Usage: position zone [x y / map x y / location]");
                return true;
            }
        }

        uint32 areaId = sTerrainMgr.GetAreaId(mapId, x, y, bot->getPositionZ());
        const AreaTableEntry* area = GetAreaEntryByAreaID(areaId);
        std::ostringstream out;
        out << "Zone: " << areaId;
        if (area)
        {
            out << " (" << area->Name << ")";
            if (area->ZoneId)
            {
                const AreaTableEntry* zone = GetAreaEntryByAreaID(area->ZoneId);
                if (zone)
                    out << " Zone: " << zone->Name;
            }
        }
        ai->TellPlayer(requester, out.str());
        return true;
    }

    // Check travel nodes near bot or location
    if (param.substr(0, 5) == "nodes")
    {
        std::string nodeParam = param.size() > 5 ? param.substr(5) : "";
        while (nodeParam.size() > 0 && nodeParam[0] == ' ')
            nodeParam = nodeParam.substr(1);

        WorldPosition pos(bot);
        if (!nodeParam.empty())
        {
            PositionTarget target = ParseLocation(nodeParam, bot);
            if (target.valid)
            {
                pos = WorldPosition(target.mapId, target.x, target.y, target.z, 0);
            }
        }

        std::vector<TravelNode*> nodes = sTravelNodeMap.GetNodes(pos);
        std::ostringstream out;
        out << "Travel nodes near " << pos.GetAreaName() << " (" << uint32(pos.getX()) << "," << uint32(pos.getY()) << "," << uint32(pos.getZ()) << "): " << nodes.size();

        if (nodes.size() > 0)
        {
            out << " - closest: " << uint32(nodes[0]->getDistance(pos)) << "y";
            if (nodes.size() > 1)
                out << ", 2nd: " << uint32(nodes[1]->getDistance(pos)) << "y";
        }
        ai->TellPlayer(requester, out.str());
        return true;
    }

    if (param.substr(0, 8) == "teleport")
    {
        std::string teleParam = param.substr(8);
        while (teleParam.size() > 0 && teleParam[0] == ' ')
            teleParam = teleParam.substr(1);

        if (teleParam.empty())
        {
            Player* master = ai->GetMaster();
            if (master)
            {
                bot->TeleportTo(master->GetMapId(), master->getPositionX(), master->getPositionY(), master->getPositionZ(), master->getOrientation());
                ai->TellPlayer(requester, "Teleported to master");
            }
            else
            {
                ai->TellPlayer(requester, "No master found");
            }
            return true;
        }

        PositionTarget target = ParseLocation(teleParam, bot);
        if (target.valid)
        {
            bot->TeleportTo(target.mapId, target.x, target.y, target.z, bot->getOrientation());
            std::ostringstream out;
            if (!target.name.empty())
                out << "Teleported to " << target.name;
            else if (target.mapId == bot->GetMapId())
                out << "Teleported to " << target.x << ", " << target.y << " on map " << target.mapId;
            else
                out << "Teleported to map " << target.mapId << " at " << target.x << ", " << target.y << ", " << target.z;
            ai->TellPlayer(requester, out.str());
            return true;
        }

        ai->TellPlayer(requester, "Usage: position teleport [x y / map x y / map x y z / location]");
        return true;
    }

    if (param.substr(0, 8) == "distance" || param.substr(0, 4) == "dist")
    {
        std::string distParam = param.size() > 8 ? param.substr(8) : (param.size() > 4 ? param.substr(4) : "");
        while (distParam.size() > 0 && distParam[0] == ' ')
            distParam = distParam.substr(1);

        if (distParam.empty())
        {
            Player* master = ai->GetMaster();
            if (master)
            {
                float dist = bot->GetDistance(master);
                std::ostringstream out;
                out << "Distance to master: " << dist << " yards";
                ai->TellPlayer(requester, out.str());
            }
            else
            {
                ai->TellPlayer(requester, "No master found");
            }
            return true;
        }

        PositionTarget target = ParseLocation(distParam, bot);
        if (target.valid)
        {
            float dx = bot->getPositionX() - target.x;
            float dy = bot->getPositionY() - target.y;
            float dz = bot->getPositionZ() - target.z;
            float dist = sqrt(dx*dx + dy*dy + dz*dz);
            std::ostringstream out;
            out << "Distance to " << (target.name.empty() ? std::string("position") : target.name) << ": " << dist << " yards";
            ai->TellPlayer(requester, out.str());
            return true;
        }

        ai->TellPlayer(requester, "Usage: position distance [x y / map x y / map x y z / location]");
        return true;
    }

    if (param.substr(0, 4) == "path")
    {
        std::string pathParam = param.size() > 4 ? param.substr(4) : "";
        while (pathParam.size() > 0 && pathParam[0] == ' ')
            pathParam = pathParam.substr(1);

        if (pathParam.empty())
        {
            LastMovement& moveData = *ai->GetAiObjectContext()->GetValue<LastMovement&>("last movement");
            std::ostringstream out;
            out << "Last movement: ";
            WorldPosition().printWKT(moveData.lastPath.GetPointPath(), out, 1);
            ai->TellPlayer(requester, out.str());
            return true;
        }

        std::string targetParam;
        WorldPosition startPos(bot);

        size_t pipePos = pathParam.find('/');
        if (pipePos == std::string::npos)
            pipePos = pathParam.find("->");
        if (pipePos == std::string::npos)
            pipePos = pathParam.find('|');

        bool showFull = false;
        if (pipePos != std::string::npos)
        {
            std::string startStr = pathParam.substr(0, pipePos);
            targetParam = pathParam.substr(pipePos + 1);
            while (targetParam.size() > 0 && targetParam[0] == ' ')
                targetParam = targetParam.substr(1);

            if (targetParam.find("full") != std::string::npos)
            {
                showFull = true;
                size_t fullPos = targetParam.find("full");
                targetParam = targetParam.substr(0, fullPos);
                while (targetParam.size() > 0 && targetParam[targetParam.size() - 1] == ' ')
                    targetParam = targetParam.substr(0, targetParam.size() - 1);
            }

            PositionTarget startTarget = ParseLocation(startStr, bot);
            if (startTarget.valid)
            {
                startPos = WorldPosition(startTarget.mapId, startTarget.x, startTarget.y, startTarget.z);
            }
            else
            {
                ai->TellPlayer(requester, "Usage: position path <from>/<to>");
                return true;
            }
        }
        else
        {
            targetParam = pathParam;
        }

        PositionTarget target = ParseLocation(targetParam, bot);
        if (target.valid)
        {
            WorldPosition endPos(target.mapId, target.x, target.y, target.z);
            TravelPath fullPath = sTravelNodeMap.GetFullPath(startPos, endPos, bot);

            if (fullPath.empty())
            {
                ai->TellPlayer(requester, "No path found");
                return true;
            }

            float totalDist = 0.0f;
            float walkDist = 0.0f, flightDist = 0.0f, transportDist = 0.0f, teleportCount = 0;
            uint32 prevMapId = startPos.GetMapId();
            std::set<uint32> maps;
            maps.insert(prevMapId);
            WorldPosition prevPos = startPos;
            bool debugMove = ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT);

            std::ostringstream out;
            out << "Path (" << fullPath.GetPath().size() << " points):";
            ai->TellPlayer(requester, out.str());

            if (showFull)
            {
                for (size_t i = 0; i < fullPath.GetPath().size(); i++)
                {
                    PathNodePoint& p = fullPath.GetPath()[i];
                    float dist = prevPos.distance(p.point);
                    totalDist += dist;

                    if (p.point.GetMapId() != prevMapId)
                    {
                        maps.insert(p.point.GetMapId());
                    }

                    switch (p.type)
                    {
                        case PathNodeType::NODE_PATH:
                        case PathNodeType::NODE_PREPATH:
                            walkDist += dist;
                            break;
                        case PathNodeType::NODE_FLIGHTPATH:
                            flightDist += dist;
                            break;
                        case PathNodeType::NODE_TRANSPORT:
                            transportDist += dist;
                            break;
                        case PathNodeType::NODE_TELEPORT:
                        case PathNodeType::NODE_STATIC_PORTAL:
                            teleportCount++;
                            break;
                        default:
                            break;
                    }

                    std::ostringstream ptOut;
                    ptOut << "  " << i << ". " << p.point.getX() << "," << p.point.getY() << "," << p.point.getZ()
                          << " m" << p.point.GetMapId() << " type:" << (int)p.type;
                    ai->TellPlayer(requester, ptOut.str());

                    prevPos = p.point;
                    prevMapId = p.point.GetMapId();
                }
            }
            else
            {
                for (size_t i = 0; i < fullPath.GetPath().size(); i++)
                {
                    PathNodePoint& p = fullPath.GetPath()[i];
                    float dist = prevPos.distance(p.point);
                    totalDist += dist;

                    if (p.point.GetMapId() != prevMapId)
                    {
                        maps.insert(p.point.GetMapId());
                    }

                    switch (p.type)
                    {
                        case PathNodeType::NODE_PATH:
                        case PathNodeType::NODE_PREPATH:
                            walkDist += dist;
                            break;
                        case PathNodeType::NODE_FLIGHTPATH:
                            flightDist += dist;
                            break;
                        case PathNodeType::NODE_TRANSPORT:
                            transportDist += dist;
                            break;
                        case PathNodeType::NODE_TELEPORT:
                        case PathNodeType::NODE_STATIC_PORTAL:
                            teleportCount++;
                            break;
                        default:
                            break;
                    }

                    if (debugMove && i < 20)
                    {
                        bot->SummonCreature(2334, p.point.getX(), p.point.getY(), p.point.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 20000.0f);
                    }

                    prevPos = p.point;
                    prevMapId = p.point.GetMapId();
                }
            }

            if (debugMove && !fullPath.GetPath().empty())
            {
                bot->SummonCreature(6, fullPath.GetFront().getX(), fullPath.GetFront().getY(), fullPath.GetFront().getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 30000.0f);
            }

            std::ostringstream summary;
            summary << "Total: " << totalDist << " yards";
            ai->TellPlayer(requester, summary.str());

            std::ostringstream breakdown;
            breakdown << "Walk: " << walkDist << "y, Fly: " << flightDist << "y, Transport: " << transportDist << "y, Teleport: " << teleportCount << "x";
            ai->TellPlayer(requester, breakdown.str());

            if (maps.size() > 1)
            {
                std::ostringstream mapInfo;
                mapInfo << "Maps: ";
                for (auto m : maps)
                    mapInfo << m << " ";
                ai->TellPlayer(requester, mapInfo.str());
            }

            std::ostringstream next;
            next << "Next Point: " << fullPath.GetFront().getX() << " " << fullPath.GetFront().getY() << " " << fullPath.GetFront().getZ();
            ai->TellPlayer(requester, next.str());
            return true;
        }

        ai->TellPlayer(requester, "Usage: position path [<from> /] <to> [full]");
        return true;
    }

    if (param.substr(0, 5) == "route")
    {
        std::string routeParam = param.size() > 5 ? param.substr(5) : "";
        while (routeParam.size() > 0 && routeParam[0] == ' ')
            routeParam = routeParam.substr(1);

        if (routeParam.empty())
        {
            ai->TellPlayer(requester, "Usage: position route <from> / <to>");
            return true;
        }

        std::string targetParam;
        WorldPosition startPos(bot);

        size_t pipePos = routeParam.find('/');
        if (pipePos == std::string::npos)
            pipePos = routeParam.find("->");
        if (pipePos == std::string::npos)
            pipePos = routeParam.find('|');

        if (pipePos != std::string::npos)
        {
            std::string startStr = routeParam.substr(0, pipePos);
            targetParam = routeParam.substr(pipePos + 1);
            while (targetParam.size() > 0 && targetParam[0] == ' ')
                targetParam = targetParam.substr(1);

            PositionTarget startTarget = ParseLocation(startStr, bot);
            if (startTarget.valid)
            {
                startPos = WorldPosition(startTarget.mapId, startTarget.x, startTarget.y, startTarget.z);
            }
            else
            {
                ai->TellPlayer(requester, "Usage: position route <from>/<to>");
                return true;
            }
        }
        else
        {
            targetParam = routeParam;
        }

        PositionTarget target = ParseLocation(targetParam, bot);
        if (target.valid)
        {
            WorldPosition endPos(target.mapId, target.x, target.y, target.z);

            std::vector<WorldPosition> beginPath, endPath;
            TravelNodeRoute route = sTravelNodeMap.GetRoute(startPos, endPos, beginPath, endPath, bot);

            if (route.isEmpty())
            {
                ai->TellPlayer(requester, "No route found");
                return true;
            }

            std::ostringstream out;
            out << "Route (" << route.GetNodes().size() << " nodes):";
            ai->TellPlayer(requester, out.str());

            float totalDist = 0.0f;
            WorldPosition prevPos = startPos;
            bool debugMove = ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT);

            for (size_t i = 0; i < route.GetNodes().size(); i++)
            {
                TravelNode* node = route.GetNodes()[i];
                WorldPosition nodePos = *node->getPosition();
                float dist = prevPos.distance(nodePos);
                totalDist += dist;

                uint32 areaId = sTerrainMgr.GetAreaId(nodePos.GetMapId(), nodePos.getX(), nodePos.getY(), nodePos.getZ());
                const AreaTableEntry* area = GetAreaEntryByAreaID(areaId);
                std::string areaName = "Unknown";
                if (area)
                {
                    areaName = area->Name;
                }

                std::ostringstream nodeOut;
                nodeOut << "  " << (i + 1) << ". " << node->GetName() << " [" << areaName << ", m" << nodePos.GetMapId() << "] (" << dist << " yd)";
                ai->TellPlayer(requester, nodeOut.str());

                if (debugMove)
                {
                    bot->SummonCreature(2334, nodePos.getX(), nodePos.getY(), nodePos.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 20000.0f);
                }

                prevPos = nodePos;
            }

            if (debugMove && !route.GetNodes().empty())
            {
                bot->SummonCreature(6, route.GetNodes().front()->getPosition()->getX(),
                    route.GetNodes().front()->getPosition()->getY(),
                    route.GetNodes().front()->getPosition()->getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 30000.0f);
            }

            std::ostringstream summary;
            summary << "Total: " << totalDist << " yards";
            ai->TellPlayer(requester, summary.str());
            return true;
        }

        ai->TellPlayer(requester, "Usage: position route <from> / <to>");
        return true;
    }

    if (param.substr(0, 5) == "water")
    {
        std::string waterParam = param.size() > 5 ? param.substr(5) : "";
        while (waterParam.size() > 0 && waterParam[0] == ' ')
            waterParam = waterParam.substr(1);

        float x, y, z;
        uint32 mapId;

        if (!waterParam.empty())
        {
            PositionTarget target = ParseLocation(waterParam, bot);
            if (target.valid)
            {
                x = target.x;
                y = target.y;
                z = target.z;
                mapId = target.mapId;
            }
            else
            {
                ai->TellPlayer(requester, "Usage: position water [x y / map x y / map x y z / location]");
                return true;
            }
        }
        else
        {
            x = bot->getPositionX();
            y = bot->getPositionY();
            z = bot->getPositionZ();
            mapId = bot->GetMapId();
        }

        Map* map = sMapMgr.FindMap(mapId);
        if (!map)
        {
            ai->TellPlayer(requester, "Map not found");
            return true;
        }

        const TerrainInfo* terrain = map->GetTerrain();
        if (!terrain)
        {
            ai->TellPlayer(requester, "Terrain not loaded");
            return true;
        }

        float groundLevel = 0.0f;
        float waterLevel = terrain->GetWaterLevel(x, y, z, &groundLevel);

        std::ostringstream out;
        out << "Pos: " << x << "," << y << "," << z << " m" << mapId;
        ai->TellPlayer(requester, out.str());

        std::ostringstream groundOut;
        groundOut << "Ground: " << groundLevel;
        ai->TellPlayer(requester, groundOut.str());

        std::ostringstream waterOut;
        waterOut << "Water: " << waterLevel;
        ai->TellPlayer(requester, waterOut.str());

        if (waterLevel > groundLevel)
        {
            float waterDepth = waterLevel - groundLevel;
            if (z >= waterLevel)
            {
                ai->TellPlayer(requester, "State: Above water surface");
            }
            else if (z >= groundLevel)
            {
                std::ostringstream inWater;
                inWater << "State: In water (depth: " << waterDepth << " yards)";
                ai->TellPlayer(requester, inWater.str());
            }
            else
            {
                ai->TellPlayer(requester, "State: Underwater (swimming/flying below surface)");
            }
        }
        else
        {
            ai->TellPlayer(requester, "State: Not in water (no water at this location)");
        }

        if (z < groundLevel - 0.5f)
        {
            float underGround = groundLevel - z;
            std::ostringstream underground;
            underground << "Underground: Yes (" << underGround << " yards below ground)";
            ai->TellPlayer(requester, underground.str());
        }
        else
        {
            ai->TellPlayer(requester, "Underground: No");
        }

        return true;
    }

    // Check if position is above ground or underground
    if (param.substr(0, 6) == "ground")
    {
        std::string groundParam = param.size() > 7 ? param.substr(7) : "";

        WorldPosition pos(bot);
        if (!groundParam.empty())
        {
            PositionTarget target = ParseLocation(groundParam, bot);
            if (target.valid)
            {
                pos = WorldPosition(target.mapId, target.x, target.y, target.z);
            }
            else
            {
                ai->TellPlayer(requester, "Usage: position ground [x y / map x y / map x y z / location]");
                return true;
            }
        }

        std::ostringstream out;
        out << pos.getX() << ", " << pos.getY() << ", " << pos.getZ() << " m" << pos.GetMapId() << ": ";
        out << (pos.isUnderground() ? "Underground" : "Above ground");
        ai->TellPlayer(requester, out.str());
        return true;
    }

    // If param is empty, show bot's position
    if (param.empty())
    {
        std::ostringstream out;
        out << bot->getPositionX() << " " << bot->getPositionY() << " " << bot->getPositionZ() << " " << bot->GetMapId() << " " << bot->getOrientation();
        uint32 area = sServerFacade.GetAreaId(bot);
        if (const AreaTableEntry* areaEntry = GetAreaEntryByAreaID(area))
        {
            if (AreaTableEntry const* zoneEntry = areaEntry->ZoneId ? GetAreaEntryByAreaID(areaEntry->ZoneId) : areaEntry)
                out << " |" << zoneEntry->Name << "|";
        }
        ai->TellPlayer(requester, out.str());
        return true;
    }

    // Try to find player by name
    if (Player* target = sObjectMgr.GetPlayer(param.c_str()))
    {
        std::ostringstream out;
        out << target->GetName() << ": " << target->getPositionX() << " " << target->getPositionY() << " " << target->getPositionZ() << " " << target->GetMapId();
        ai->TellPlayer(requester, out.str());
        return true;
    }

    // Try to find creature by name (search nearby)
    std::list<Unit*> targets;
    AnyUnitInObjectRangeCheck u_check(bot, 100000.0f);
    UnitListSearcher<AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, 100000.0f);

    for (Unit* u : targets)
    {
        if (u && u->GetTypeId() == TYPEID_UNIT)
        {
            Creature* c = (Creature*)u;
            if (c->GetName() == param)
            {
                std::ostringstream out;
                out << c->GetName() << ": " << c->getPositionX() << " " << c->getPositionY() << " " << c->getPositionZ() << " " << c->GetMapId();
                ai->TellPlayer(requester, out.str());
                return true;
            }
        }
    }

    // Try parsing as guid or link
    uint32 guid = 0;
    try { guid = std::stoul(param); } catch (...) {}

    if (guid > 0)
    {
        if (GameObject* go = bot->GetMap()->GetGameObject(ObjectGuid(HIGHGUID_GAMEOBJECT, guid)))
        {
            std::ostringstream out;
            out << "GO " << guid << ": " << go->getPositionX() << " " << go->getPositionY() << " " << go->getPositionZ() << " " << go->GetMapId();
            ai->TellPlayer(requester, out.str());
            return true;
        }
        if (Unit* u = bot->GetMap()->GetUnit(ObjectGuid(HIGHGUID_UNIT, guid)))
        {
            std::ostringstream out;
            out << "Unit " << guid << ": " << u->getPositionX() << " " << u->getPositionY() << " " << u->getPositionZ() << " " << u->GetMapId();
            ai->TellPlayer(requester, out.str());
            return true;
        }
    }

    ai->TellPlayer(requester, "Usage: position [zone/teleport/distance/path/<name/guid>]");
    return true;
}
