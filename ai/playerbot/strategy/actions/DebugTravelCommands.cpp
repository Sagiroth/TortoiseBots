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

bool DebugAction::HandlePOI(Event& event, Player* requester, const std::string& text)
{
    WorldPosition botPos = WorldPosition(bot);

    WorldPosition poiPoint = botPos;
    std::string name = "bot";

    std::vector<std::string> args = Qualified::getMultiQualifiers(text.substr(4), " ");

    DestinationList dests = ChooseTravelTargetAction::FindDestination(bot, args[0]);
    TravelDestination* dest = nullptr;

    if (!dests.empty())
    {
        WorldPosition botPos(bot);

        dest = *std::min_element(dests.begin(), dests.end(), [botPos](TravelDestination* i, TravelDestination* j) {return i->DistanceTo(botPos) < j->DistanceTo(botPos); });
    }

    if (dest)
    {
        std::list<uint8> chancesToGoFar = { 10,50,90 }; //Closest map, grid, cell.
        WorldPosition* point = dest->GetNextPoint(botPos, chancesToGoFar);
        if (point)
        {
            poiPoint = *point;
            name = dest->GetTitle();
        }
    }

    if (args.size() == 1)
    {
        args.push_back("99");
    }
    else if (args.size() == 2)
    {
        args.push_back("6");
    }
    else if (args.size() == 3)
    {
        args.push_back("0");
    }

    ai->Poi(poiPoint.x, poiPoint.y, name, nullptr, stoi(args[1]), stoi(args[2]), stoi(args[3]));

    return true;
}

bool DebugAction::HandleRPG(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;

    if (text.size() < 4)
        return false;

    GuidPosition guidP;

    std::string link = text.substr(3);

    if (!link.empty())
    {
        std::list<ObjectGuid> gos = chat->parseGameobjects(link);
        if (!gos.empty())
        {
            for (auto go : gos)
            {
                guidP = GuidPosition(go,bot);
                guidP.updatePosition(bot->GetInstanceId());

                if(guidP)
                    break;
            }
        }
    }

    if (!guidP)
        return false;

    SET_AI_VALUE(GuidPosition, "rpg target", guidP);

    if (guidP.GetWorldObject(bot->GetInstanceId()))
        ai->TellPlayerNoFacing(requester, "Setting Rpg target to " + ChatHelper::formatWorldobject(guidP.GetWorldObject(bot->GetInstanceId())));

    return true;
}

bool DebugAction::HandleRPGTargets(Event& event, Player* requester, const std::string& text)
{
    Action* action = ai->GetAiObjectContext()->GetAction("choose rpg target");

    if (!action)
        return false;

    ChooseRpgTargetAction* targetAction = static_cast<ChooseRpgTargetAction*>(action);

    if (!targetAction)
        return false;

    targetAction->GetTargets(requester, true);

    return true;
}

bool DebugAction::HandleTravel(Event& event, Player* requester, const std::string& text)
{
    WorldPosition botPos = WorldPosition(bot);

    std::string destination = text.substr(7);

    DestinationList dests = ChooseTravelTargetAction::FindDestination(bot, destination);
    TravelDestination* dest = nullptr;

    if (!dests.empty())
    {
        WorldPosition botPos(bot);

        dest = *std::min_element(dests.begin(), dests.end(), [botPos](TravelDestination* i, TravelDestination* j) {return i->DistanceTo(botPos) < j->DistanceTo(botPos); });
    }

    if (dest)
    {
        std::list<uint8> chancesToGoFar = { 10,50,90 }; //Closest map, grid, cell.
        WorldPosition* point = dest->GetNextPoint(botPos, chancesToGoFar);

        if (!point)
            return false;

        std::vector<WorldPosition> beginPath, endPath;
        TravelNodeRoute route = sTravelNodeMap.GetRoute(botPos, *point, beginPath, endPath, bot);

        std::ostringstream out; out << "Traveling to " << dest->GetTitle() << ": ";

        for (auto node : route.GetNodes())
        {
            out << node->GetName() << ", ";
        }

        ai->TellPlayerNoFacing(requester, out.str());

        return true;
    }
    else
    {
        ai->TellPlayerNoFacing(requester, "Destination " + destination + " not found.");
        return true;
    }
}

bool DebugAction::HandlePrintTravel(Event& event, Player* requester, const std::string& text)
{
    if (!sPlayerbotAIConfig.isLogOpen("travel.csv"))
        sPlayerbotAIConfig.openLog("travel.csv", "w", true);

    if (!sPlayerbotAIConfig.isLogOpen("travelActive.csv"))
        sPlayerbotAIConfig.openLog("travelActive.csv", "w", true);

    PlayerTravelInfo info(bot);

    std::vector<std::type_index> types = { typeid(QuestTravelDestination), typeid(RpgTravelDestination), typeid(ExploreTravelDestination), typeid(GrindTravelDestination), typeid(BossTravelDestination), typeid(GatherTravelDestination) };

    for (auto& type : types)
    {
        for (auto& dest : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::None, {}, true, 0))
        {
            bool isPossible = dest->IsPossible(info);
            bool isActive = dest->IsActive(bot, info);
            for (auto& point : dest->GetPoints())
            {
                GuidPosition* guidP = dynamic_cast<GuidPosition*>(point);

                if (guidP && guidP->IsEventUnspawned()) //Skip points that are not spawned due to events.
                {
                    continue;
                }

                std::ostringstream out;

                out << type.name() << ",";
                out << (uint32)((EntryTravelDestination*)dest)->GetPurpose() << ",";
                out << "\"";
                if (type == typeid(QuestTravelDestination))
                {
                    uint32 questId = ((QuestTravelDestination*)dest)->GetQuestId();
                    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                    out << quest->GetTitle();
                }
                else if (type != typeid(ExploreTravelDestination))
                {
                    if (((EntryTravelDestination*)dest)->GetCreatureInfo())
                        out << ((EntryTravelDestination*)dest)->GetCreatureInfo()->name;
                    else if (((EntryTravelDestination*)dest)->GetGoInfo())
                        out << ((EntryTravelDestination*)dest)->GetGoInfo()->name;
                    else
                        out << "";
                }
                else
                {
                    out << dest->GetTitle();
                }
                out << "\",";
                out << std::to_string(dest->GetEntry()) << ",";
                out << "\"" << dest->GetTitle() << "\",";
                out << isPossible << ",";
                out << isActive << ",";
                point->printWKT(out);
                out << point->GetAreaLevel();

                //sPlayerbotAIConfig.log("travel.csv", out.str().c_str());
                if(isActive)
                    sPlayerbotAIConfig.log("travelActive.csv", out.str().c_str());
            }

            if (dest->GetPoints().empty())
                sLog.outError("Destination %s has no points!", dest->GetTitle().c_str());
        }
    }
    return true;
}

bool DebugAction::HandleTaxi(Event& event, Player* requester, const std::string& text)
{
    for (uint32 i = 1; i < sTaxiNodesStore.GetNumRows(); ++i)
    {
        if (!bot->GetTaxi().IsTaximaskNodeKnown(i))
            continue;

        TaxiNodesEntry const* taxiNode = sTaxiNodesStore.LookupEntry(i);

        std::ostringstream out;

        out << taxiNode->name[LOCALE_enUS];

        ai->TellPlayerNoFacing(requester, out);
    }
    return true;
}

bool DebugAction::HandleSaveNode(Event& event, Player* requester, const std::string& text)
{
    sTravelNodeMap.printNodeStore();
    sTravelNodeMap.saveNodeStore(true);
    return true;
}

bool DebugAction::HandleShowNode(Event& event, Player* requester, const std::string& text)
{
    WorldPosition pos(bot);

    std::vector<TravelNode*> nodes = sTravelNodeMap.GetNodes(pos, 500);

    for (auto& node : nodes)
    {
        for (auto& l : *node->GetLinks())
        {
            Unit* start = nullptr;
            std::list<ObjectGuid> units;

            uint32 time = 60 * IN_MILLISECONDS;

            std::vector<WorldPosition> ppath = l.second->GetPath();

            for (auto p : ppath)
            {
                Creature* wpCreature = bot->SummonCreature(2334, p.getX(), p.getY(), p.getZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 20000.0f);
                if (!wpCreature)
                    continue;
                //addAura(246, wpCreature);
                units.push_back(wpCreature->getObjectGuid());

                if(!start)
                    ai->AddAura(wpCreature, 1130);
                else
                    ai->AddAura(wpCreature, 246);


                if (!start)
                    start = wpCreature;
            }
        }
    }
    return true;
}

bool DebugAction::HandleNodes(Event& event, Player* requester, const std::string& text)
{
    WorldPosition pos(bot);
    ai->TellPlayer(requester, "=== Debug: getRoute Pathfinding Analysis ===");
    ai->TellPlayer(requester, "Bot position: " + pos.print());

    bool inCombat = bot->IsInCombat();
    bool inWater = bot->IsInWater();
    bool isUnderWater = bot->IsUnderwater();
    ai->TellPlayer(requester, "Bot state: combat=" + std::string(inCombat ? "YES" : "NO") +
                   " water=" + std::string(inWater ? "YES" : "NO") +
                   " underwater=" + std::string(isUnderWater ? "YES" : "NO"));

    std::vector<TravelNode*> allNodes = sTravelNodeMap.GetNodes(pos, -1.0f);
    ai->TellPlayer(requester, "Total nodes on map " + std::to_string(pos.GetMapId()) + ": " + std::to_string(allNodes.size()));

    if (allNodes.empty())
    {
        ai->TellPlayer(requester, ">>> No nodes at all on this map!");
        return true;
    }

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== getRoute Step 1: Find START nodes (closest 5) ===");

    std::sort(allNodes.begin(), allNodes.end(), [pos](TravelNode* i, TravelNode* j) {
        return i->getPosition()->sqDistance(pos) < j->getPosition()->sqDistance(pos);
    });

    std::vector<TravelNode*> startNodes;
    uint32 startNr = std::min(5u, (uint32)allNodes.size());
    for (uint32 i = 0; i < startNr; i++)
        startNodes.push_back(allNodes[i]);

    ai->TellPlayer(requester, "Testing pathfinding to each of the 5 closest nodes:");

    int startNodesWithPath = 0;
    for (uint32 i = 0; i < startNodes.size(); i++)
    {
        auto& node = startNodes[i];

        TravelNodeMap::PathFindResult result = sTravelNodeMap.testPathToLoop(pos, *node->getPosition(), bot, 0, {}, "debug");

        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Start Node " + std::to_string(i+1) + ": " + node->GetName());
        ai->TellPlayer(requester, "  Distance: " + std::to_string(pos.distance(*node->getPosition())) + "y, PathType: " +
            (result.type == PATHFIND_NORMAL ? "NORMAL" :
             result.type == PATHFIND_INCOMPLETE ? "INCOMPLETE" :
             result.type == PATHFIND_NOPATH ? "NOPATH" : "UNKNOWN"));

        if (result.path.size() > 0)
        {
            ai->TellPlayer(requester, "  Path points: " + std::to_string(result.path.size()) + ", stops " +
                std::to_string(result.path.back().distance(bot)) + "y from bot");
        }

        if (node->getPosition()->IsPathTo(result.path))
        {
            ai->TellPlayer(requester, "  >>> FULL PATH to node");
            startNodesWithPath++;
        }
        else
        {
            ai->TellPlayer(requester, "  >> NO FULL PATH");
        }
    }

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== getRoute Step 2: Check network routes ===");

    int nodesWithRoute = 0;
    for (uint32 i = 0; i < startNodes.size(); i++)
    {
        auto& startNode = startNodes[i];

        float dist = pos.distance(*startNode->getPosition());

        std::unique_ptr<PathFinder> pathfinder = std::make_unique<PathFinder>(bot);
        pathfinder->calculate(pos.GetVector3(), startNode->getPosition()->GetVector3(), false);

        PointsArray points = pathfinder->getPath();
        bool hasFullPath = false;
        if (!points.empty())
        {
            Vector3 lastVec = points.back();
            WorldPosition lastPoint(bot->GetMapId(), lastVec.x, lastVec.y, lastVec.z);
            if (dist - pos.distance(lastPoint) <= 1.0f)
                hasFullPath = true;
        }

        if (!hasFullPath)
            continue;

        std::vector<TravelNode*> reachable = startNode->GetNodeMap(false, {}, false);

        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Start: " + startNode->GetName() + " (has path)");
        ai->TellPlayer(requester, "  Can reach: " + std::to_string(reachable.size()) + " nodes in network");

        if (reachable.size() > 0)
        {
            std::string nodeList;
            uint32 count = std::min(10u, (uint32)reachable.size());
            for (uint32 j = 0; j < count; j++)
            {
                nodeList += reachable[j]->GetName();
                if (j < count - 1)
                    nodeList += ", ";
            }
            if (reachable.size() > count)
                nodeList += "...";
            ai->TellPlayer(requester, "  Nodes: " + nodeList);
        }

        if (reachable.size() > 10)
        {
            nodesWithRoute++;
            ai->TellPlayer(requester, "  >>> HAS NETWORK ROUTES");
        }
        else
        {
            ai->TellPlayer(requester, "  >> LIMITED NETWORK (only " + std::to_string(reachable.size()) + " nodes)");
        }
    }

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== getRoute Step 3: Try actual route (like bot uses) ===");

    WorldPosition targetPos = pos;
    targetPos.x += 100.0f;
    targetPos.y += 100.0f;

    std::vector<WorldPosition> beginPath, endPath;
    TravelNodeRoute route = sTravelNodeMap.GetRoute(pos, targetPos, beginPath, endPath, bot);

    if (!route.isEmpty())
    {
        ai->TellPlayer(requester, ">>> ROUTE FOUND!");
        ai->TellPlayer(requester, "  Nodes in route: " + std::to_string(route.GetNodes().size()));
    }
    else
    {
        ai->TellPlayer(requester, ">>> NO ROUTE FOUND");
        ai->TellPlayer(requester, "  This means bot cannot travel using the node network");
    }

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== Summary ===");
    ai->TellPlayer(requester, "Start nodes with full path: " + std::to_string(startNodesWithPath) + "/5");
    ai->TellPlayer(requester, "Start nodes with network routes: " + std::to_string(nodesWithRoute));

    if (startNodesWithPath == 0)
    {
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, ">>> PROBLEM: Bot cannot path to ANY start node!");
        ai->TellPlayer(requester, "  - Bot may be in water, indoors, void, or no navmesh");
    }
    else if (nodesWithRoute == 0)
    {
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, ">>> PROBLEM: Start nodes have no network routes!");
        ai->TellPlayer(requester, "  - Try: debug gen path (mod only)");
    }

    return true;
}
