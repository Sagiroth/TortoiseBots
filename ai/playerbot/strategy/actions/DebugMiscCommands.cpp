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

bool DebugAction::HandleAvoidScan(Event& event, Player* requester, const std::string& text)
{
    ai->TellError(requester, "Avoidance scan is unavailable: the pinned Tortoise core does not expose nav area queries.",
        PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true);
    return false;
}

bool DebugAction::HandleAvoidAdd(Event& event, Player* requester, const std::string& text)
{
    ai->TellError(requester, "Avoidance areas are unavailable: the pinned Tortoise core does not expose a path area filter.",
        PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true);
    return false;
}

bool DebugAction::HandleMount(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;
    out << (bot->IsMounted() ? "mounted" : "not mounted");
    out << (bot->IsTaxiFlying() ? ", taxi flying" : ", not taxi flying");
    out << ", mount speed: " << AI_VALUE2(uint32, "current mount speed", "self target");
    out << ", mount id: " << bot->GetMountID();
    ai->TellPlayerNoFacing(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
    return true;
}

bool DebugAction::HandleUnmount(Event& event, Player* requester, const std::string& text)
{
    ai->Unmount();
    return true;
}

bool DebugAction::HandleArea(Event& event, Player* requester, const std::string& text)
{
    WorldPosition point(requester);
    AreaTableEntry const* area = point.GetArea();
    std::ostringstream out;
    out << point.GetAreaName(true, false);
    out << "," << area->Team << " (" << (area->Team != FACTION_GROUP_MASK_ALLIANCE ? (area->Team != FACTION_GROUP_MASK_HORDE ? "neutral" : "horde") : "alliance") << ")";
    ai->TellPlayerNoFacing(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
    return true;
}

bool DebugAction::HandleGY(Event& event, Player* requester, const std::string& text)
{
    ai->TellError(requester, "Graveyard diagnostic is unavailable in the Tortoise core.");
    return false;
}

bool DebugAction::HandleGrid(Event& event, Player* requester, const std::string& text)
{
    WorldPosition botPos = bot;
    std::string loaded = botPos.GetMap(bot->GetInstanceId())->IsLoaded(botPos.getX(), botPos.getY()) ? "loaded" : "unloaded";

    std::ostringstream out;

    out << "Map: " << botPos.GetMapId() << " " << botPos.GetAreaName() << " Grid: " << botPos.getGridPair().x_coord << "," << botPos.getGridPair().y_coord << " [" << loaded << "] Cell: " << botPos.getCellPair().x_coord << "," << botPos.getCellPair().y_coord;

    ai->TellPlayer(requester, out.str());

    return true;
}

bool DebugAction::HandleGPS(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;
    WorldPosition botPos(bot);
    out << bot->GetName() << ",";
    out << botPos.GetAreaName() << ",";

    out << "\"node = sTravelNodeMap.addNode(WorldPosition(" << botPos.GetMapId() << std::fixed << std::setprecision(2);
    out << ',' << botPos.x << "f";
    out << ',' << botPos.y << "f";
    out << ',' << botPos.z << "f";
    out << "), \\\"c1" << botPos.GetAreaName() << "\\\", true, false);\"";

    //node = sTravelNodeMap.addNode(WorldPosition(1, -3626.39f, 917.37f, 150.13f), "c1-Dire maul", true, false);

    if (!sPlayerbotAIConfig.isLogOpen("gps.csv"))
        sPlayerbotAIConfig.openLog("gps.csv", "a", true);
    sPlayerbotAIConfig.log("gps.csv", out.str().c_str());

    return true;
}

bool DebugAction::HandleCorpse(Event& event, Player* requester, const std::string& text)
{

    Corpse* corpse = bot->GetCorpse();

    if (!corpse)
    {
        ai->TellPlayerNoFacing(requester, "no corpse");
        return true;
    }


    std::ostringstream out;

    if(corpse->GetType() == CORPSE_BONES)
        out << "CORPSE_BONES";

    if (corpse->GetType() == CORPSE_RESURRECTABLE_PVE)
        out << "CORPSE_RESURRECTABLE_PVE";

    if (corpse->GetType() == CORPSE_RESURRECTABLE_PVP)
        out << "CORPSE_RESURRECTABLE_PVP";

    out << ",p:" << WorldPosition(corpse).print();

    out << ",time: " << corpse->GetGhostTime();

    ai->TellPlayerNoFacing(requester, out);

    return true;
}

bool DebugAction::HandleLogoutTime(Event& event, Player* requester, const std::string& text)
{
    int32 time = sRandomBotFacade.GetValueValidTime(bot->GetGUIDLow(), "add");

    int32 min = 0, hr = 0;

    if (time > 3600)
    {
        hr = floor(time / 3600);
        time = time % 3600;
    }
    if (time > 60)
    {
        min = floor(time / 60);
        time = time % 60;
    }

    std::ostringstream out;

    out << "Logout in: " << hr << ":" << min << ":" << time;

    ai->TellPlayer(requester, out.str());

    return true;
}

bool DebugAction::HandleLevel(Event& event, Player* requester, const std::string& text)
{
    uint32 level = bot->GetLevel();
    uint32 nextLevelXp = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    uint32 xp = bot->GetUInt32Value(PLAYER_XP);
    float flevel = ai->GetLevelFloat();

    std::ostringstream out;

    out << "Level: " << level << ", xp:" << xp << "/" << nextLevelXp << " :" || flevel;

    ai->TellPlayer(requester, out.str());

    return true;
}

bool DebugAction::HandleQuest(Event& event, Player* requester, const std::string& text)
{
    std::string param = text.substr(5); // skip "quest"
    while (param.size() > 0 && param[0] == ' ')
        param = param.substr(1);

    std::string subcmd;
    std::string questParam;

    // Parse subcommand
    size_t spacePos = param.find(' ');
    if (spacePos != std::string::npos)
    {
        subcmd = param.substr(0, spacePos);
        questParam = param.substr(spacePos + 1);
        while (questParam.size() > 0 && questParam[0] == ' ')
            questParam = questParam.substr(1);
    }
    else
    {
        subcmd = param;
    }

    // Handle 'list' subcommand
    if (subcmd == "list")
    {
        std::ostringstream out;
        out << "=== Quest Log ===";
        int count = 0;
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = GetQuestSlotIdCompat(bot, slot);
            if (!questId)
                continue;

            Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
            std::string name = pQuest ? pQuest->GetTitle() : "Unknown";
            QuestStatus status = bot->GetQuestStatus(questId);
            std::string statusStr = (status == QUEST_STATUS_COMPLETE) ? "[COMPLETE]" : "[INCOMPLETE]";

            out << "\n" << questId << ": " << statusStr << " " << name;
            count++;
        }
        if (count == 0)
            out << "\nNo quests in log";
        ai->TellPlayer(requester, out.str());
        return true;
    }

    // Parse quest ID - try both subcmd and questParam
    uint32 questId = 0;

    // First try questParam (if provided with subcommand like "complete 123")
    if (!questParam.empty())
    {
        try
        {
            questId = std::stoul(questParam);
        }
        catch (...)
        {
            if (questParam.find("|Hquest:") != std::string::npos)
            {
                size_t start = questParam.find(":") + 1;
                size_t end = questParam.find(":", start);
                if (end != std::string::npos)
                {
                    try
                    {
                        questId = std::stoul(questParam.substr(start, end - start));
                    }
                    catch (...) {}
                }
            }
        }
    }

    // If no questId yet, try subcmd directly (for "quest 745" syntax)
    if (!questId && !subcmd.empty())
    {
        try
        {
            questId = std::stoul(subcmd);
        }
        catch (...)
        {
            if (subcmd.find("|Hquest:") != std::string::npos)
            {
                size_t start = subcmd.find(":") + 1;
                size_t end = subcmd.find(":", start);
                if (end != std::string::npos)
                {
                    try
                    {
                        questId = std::stoul(subcmd.substr(start, end - start));
                    }
                    catch (...) {}
                }
            }
        }
    }

    // Handle 'complete' subcommand
    if (subcmd == "complete")
    {
        if (!questId)
        {
            ai->TellPlayer(requester, "Usage: quest complete <questId>");
            return true;
        }

        // Find the quest in bot's quest log
        bool found = false;
        bool completed = false;

        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 logQuestId = GetQuestSlotIdCompat(bot, slot);
            if (!logQuestId)
                continue;

            if (logQuestId != questId)
                continue;

            found = true;

            if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
            {
                completed = true;
                break;
            }

            Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
            if (!pQuest)
                break;

            // Add required items
            for (uint8 x = 0; x < QUEST_ITEM_OBJECTIVES_COUNT; ++x)
            {
                uint32 id = pQuest->ReqItemId[x];
                uint32 count = pQuest->ReqItemCount[x];
                if (!id || !count)
                    continue;

                uint32 curItemCount = bot->GetItemCount(id, true);
                if (curItemCount >= count)
                    continue;

                ItemPosCountVec dest;
                uint8 msg = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, id, count - curItemCount);
                if (msg == EQUIP_ERR_OK)
                {
                    Item* item = bot->StoreNewItem(dest, id, true);
                    bot->SendNewItem(item, count - curItemCount, true, false);
                }
            }

            // Complete kill objectives
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                int32 creature = pQuest->ReqCreatureOrGOId[i];
                uint32 creaturecount = pQuest->ReqCreatureOrGOCount[i];

                if (uint32 spell_id = pQuest->ReqSpell[i])
                {
                    for (uint16 z = 0; z < creaturecount; ++z)
                        bot->CastedCreatureOrGO(creature, ObjectGuid((creature > 0 ? HIGHGUID_UNIT : HIGHGUID_GAMEOBJECT), uint32(std::abs(creature)), 1u), spell_id);
                }
                else if (creature > 0)
                {
                    if (CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(creature))
                        for (uint16 z = 0; z < creaturecount; ++z)
                            bot->KilledMonster(cInfo, ObjectGuid());
                }
                else if (creature < 0)
                {
                    for (uint16 z = 0; z < creaturecount; ++z)
                        bot->CastedCreatureOrGO(-creature, ObjectGuid(), 0);
                }
            }

            // Complete reputation
            if (uint32 repFaction = pQuest->GetRepObjectiveFaction())
            {
                uint32 repValue = pQuest->GetRepObjectiveValue();
                uint32 curRep = bot->GetReputationMgr().GetReputation(repFaction);
                if (curRep < repValue)
                {
                    if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
                        bot->GetReputationMgr().SetReputation(factionEntry, repValue);
                }
            }

            // Complete money requirement
            int32 ReqOrRewMoney = pQuest->GetRewOrReqMoney();
            if (ReqOrRewMoney < 0)
                bot->ModifyMoney(-ReqOrRewMoney);

            bot->CompleteQuest(questId);
            completed = true;
            break;
        }

        if (completed)
        {
            std::ostringstream out;
            out << "Quest " << questId << " completed!";
            ai->TellPlayer(requester, out.str());
        }
        else if (found)
        {
            std::ostringstream out;
            out << "Quest " << questId << " already completed.";
            ai->TellPlayer(requester, out.str());
        }
        else
        {
            std::ostringstream out;
            out << "Quest " << questId << " not found in quest log.";
            ai->TellPlayer(requester, out.str());
        }
        return true;
    }

    // Handle 'drop' subcommand (abandon quest)
    if (subcmd == "drop")
    {
        if (!questId)
        {
            ai->TellPlayer(requester, "Usage: quest drop <questId>");
            return true;
        }

        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 logQuestId = GetQuestSlotIdCompat(bot, slot);
            if (!logQuestId || logQuestId != questId)
                continue;

            SetQuestSlotCompat(bot, slot, 0);
            SetQuestSlotStateCompat(bot, slot, QUEST_STATE_NONE);

            std::ostringstream out;
            out << "Quest " << questId << " dropped.";
            ai->TellPlayer(requester, out.str());
            return true;
        }

        std::ostringstream out;
        out << "Quest " << questId << " not found in quest log.";
        ai->TellPlayer(requester, out.str());
        return true;
    }

    // Handle 'add' subcommand (add quest to log)
    if (subcmd == "add")
    {
        if (!questId)
        {
            ai->TellPlayer(requester, "Usage: quest add <questId>");
            return true;
        }

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
        {
            std::ostringstream out;
            out << "Quest " << questId << " not found in database.";
            ai->TellPlayer(requester, out.str());
            return true;
        }

        // Find empty slot
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            if (GetQuestSlotIdCompat(bot, slot))
                continue;

            SetQuestSlotCompat(bot, slot, questId);
            SetQuestSlotStateCompat(bot, slot, QUEST_STATE_NONE);

            std::ostringstream out;
            out << "Quest " << questId << " (" << pQuest->GetTitle() << ") added.";
            ai->TellPlayer(requester, out.str());
            return true;
        }

        ai->TellPlayer(requester, "Quest log is full");
        return true;
    }

    // Handle 'travel' subcommand (show travel info for quest)
    if (subcmd == "travel")
    {
        if (!questId)
        {
            ai->TellPlayer(requester, "Usage: quest travel <questId>");
            return true;
        }

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
        {
            std::ostringstream out;
            out << "Quest " << questId << " not found in database.";
            ai->TellPlayer(requester, out.str());
            return true;
        }

        std::ostringstream out;
        out << "=== Quest: " << questId << " ===";
        out << "\n" << pQuest->GetTitle();

        // Show objectives
        out << "\n--- Objectives ---";
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 creature = pQuest->ReqCreatureOrGOId[i];
            uint32 count = pQuest->ReqCreatureOrGOCount[i];
            if (!creature || !count)
                continue;

            std::string targetType = (creature > 0) ? "Kill" : "Collect";
            out << "\n" << targetType << " " << abs(creature) << " x" << count;
        }
        for (uint8 x = 0; x < QUEST_ITEM_OBJECTIVES_COUNT; ++x)
        {
            uint32 item = pQuest->ReqItemId[x];
            uint32 count = pQuest->ReqItemCount[x];
            if (!item || !count)
                continue;

            out << "\nCollect item " << item << " x" << count;
        }

        ai->TellPlayer(requester, out.str());
        return true;
    }

    // Handle 'info' or just quest ID - show info about quest
    if (!subcmd.empty() && questId)
    {
        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
        {
            std::ostringstream out;
            out << "Quest " << questId << " not found in database.";
            ai->TellPlayer(requester, out.str());
            return true;
        }

        // Check if in quest log
        bool inLog = false;
        QuestStatus status = QUEST_STATUS_NONE;
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            if (GetQuestSlotIdCompat(bot, slot) == questId)
            {
                inLog = true;
                status = bot->GetQuestStatus(questId);
                break;
            }
        }

        std::ostringstream out;
        out << "=== Quest: " << questId << " ===";
        out << "\n" << pQuest->GetTitle();
        out << "\nLevel: " << pQuest->GetQuestLevel();
        out << "\nStatus: ";
        if (!inLog)
            out << "NOT IN LOG";
        else if (status == QUEST_STATUS_COMPLETE)
            out << "COMPLETE";
        else
            out << "IN PROGRESS";

        ai->TellPlayer(requester, out.str());
        return true;
    }

    // No valid subcommand - show usage
    ai->TellPlayer(requester, "Usage: quest <complete/drop/add/list/travel> [questId]");
    return true;
}

bool DebugAction::HandleNPC(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;

    GuidPosition guidP = GuidPosition(requester->GetSelectionGuid(), requester);

    if (text.size() > 4)
    {
        std::string link = text.substr(4);

        if (!link.empty())
        {
            std::list<int32> entries = chat->parseWorldEntries(link);
            if (!entries.empty())
            {

                int32 entry = entries.front();

                for (auto cre : WorldPosition(bot).GetCreaturesNear(0.0f, entry))
                {
                    guidP = GuidPosition(cre);
                    break;
                }
            }
        }
    }

    if (!guidP)
        return false;

    if (guidP.GetWorldObject(bot->GetInstanceId()))
        out << chat->formatWorldobject(guidP.GetWorldObject(bot->GetInstanceId()));

    out << " (e:" << guidP.GetEntry();

    if (guidP.GetUnit(bot->GetInstanceId()))
        out << ",level:" << guidP.GetUnit(bot->GetInstanceId())->GetLevel();

    out << ") ";

    guidP.printWKT(out);

    out << "[a:" << guidP.GetArea()->Name;

    if (guidP.GetArea() && guidP.GetAreaLevel())
        out << " level: " << guidP.GetAreaLevel();
    if (guidP.GetArea()->ZoneId && GetAreaEntryByAreaID(guidP.GetArea()->ZoneId))
    {
        out << " z:" << GetAreaEntryByAreaID(guidP.GetArea()->ZoneId)->Name;
        if (sTravelMgr.GetAreaLevel(guidP.GetArea()->ZoneId))
            out << " level: " << sTravelMgr.GetAreaLevel(guidP.GetArea()->ZoneId);
    }

    out << "] ";

    uint16 event_id = sGameEventMgr.GetGameEventId<Creature>(guidP.GetCounter());

    if (event_id)
        out << " event:" << event_id << (sGameEventMgr.IsActiveEvent(event_id) ? " active" : " inactive");

    uint16 topPoolId = sPoolMgr.IsPartOfTopPool<Creature>(guidP.GetCounter());

    if (topPoolId)
        out << " pool:" << topPoolId << (sGameEventMgr.GetGameEventId<Pool>(topPoolId) ? " event" : " nonevent");

    ai->TellPlayerNoFacing(requester, out);

    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_GOSSIP))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_GOSSIP");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_QUESTGIVER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_VENDOR");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_FLIGHTMASTER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_TRAINER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_TRAINER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_SPIRITHEALER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_SPIRITHEALER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_SPIRITGUIDE))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_SPIRITGUIDE");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_INNKEEPER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_INNKEEPER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_BANKER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_BANKER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_PETITIONER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_PETITIONER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_TABARDDESIGNER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_TABARDDESIGNER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_BATTLEMASTER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_BATTLEMASTER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_AUCTIONEER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_STABLEMASTER))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_STABLEMASTER");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_REPAIR))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_REPAIR");
    if (guidP.HasNpcFlag(UNIT_NPC_FLAG_OUTDOORPVP))
        ai->TellPlayerNoFacing(requester, "UNIT_NPC_FLAG_OUTDOORPVP");


    std::ostringstream out2;
    FactionTemplateEntry const* requestFaction = sFactionTemplateStore.LookupEntry(requester->GetFactionTemplateId());
    FactionTemplateEntry const* objectFaction = nullptr;
    if(guidP.GetCreatureTemplate() && guidP.GetCreatureTemplate()->faction)
        objectFaction = sFactionTemplateStore.LookupEntry(guidP.GetCreatureTemplate()->faction);
    FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
    FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);

    std::map<ReputationRank, std::string> rep;

    rep[REP_HATED] = "hated";
    rep[REP_HOSTILE] = "hostile";
    rep[REP_UNFRIENDLY] = "unfriendly";
    rep[REP_NEUTRAL] = "neutral";
    rep[REP_FRIENDLY] = "friendly";
    rep[REP_HONORED] = "honored";
    rep[REP_REVERED] = "revered";
    rep[REP_EXALTED] = "exalted";

    if (objectFaction)
    {
        ReputationRank reactionRequest = PlayerbotAI::GetFactionReaction(requestFaction, objectFaction);
        ReputationRank reactionHum = PlayerbotAI::GetFactionReaction(humanFaction, objectFaction);
        ReputationRank reactionOrc = PlayerbotAI::GetFactionReaction(orcFaction, objectFaction);

        out2 << " faction:" << guidP.GetCreatureTemplate()->faction << " reaction me: " << rep[reactionRequest] << ",alliance: " << rep[reactionHum] << " ,horde: " << rep[reactionOrc];
    }

    ai->TellPlayerNoFacing(requester, out2);

    return true;
}

bool DebugAction::HandleGO(Event& event, Player* requester, const std::string& text)
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
                guidP = GuidPosition(go, bot);
                break;
            }
        }
    }

    if (!guidP)
        return false;

    if (!guidP.IsGameObject())
        return false;

    if (guidP.GetWorldObject(bot->GetInstanceId()))
        out << chat->formatWorldobject(guidP.GetWorldObject(bot->GetInstanceId()));

    out << " (e:" << guidP.GetEntry();

    if (guidP.GetUnit(bot->GetInstanceId()))
        out << ",level:" << guidP.GetUnit(bot->GetInstanceId())->GetLevel();

    out << ") ";

    guidP.printWKT(out);


    ai->TellPlayerNoFacing(requester, out);

    std::ostringstream out2;

    FactionTemplateEntry const* requestFaction = sFactionTemplateStore.LookupEntry(requester->GetFactionTemplateId());
    FactionTemplateEntry const* objectFaction = sFactionTemplateStore.LookupEntry(guidP.GetGameObjectInfo()->faction);
    FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
    FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);

    std::map<ReputationRank, std::string> rep;

    rep[REP_HATED] = "hated";
    rep[REP_HOSTILE] = "hostile";
    rep[REP_UNFRIENDLY] = "unfriendly";
    rep[REP_NEUTRAL] = "neutral";
    rep[REP_FRIENDLY] = "friendly";
    rep[REP_HONORED] = "honored";
    rep[REP_REVERED] = "revered";
    rep[REP_EXALTED] = "exalted";

    if (objectFaction)
    {
        ReputationRank reactionRequest = PlayerbotAI::GetFactionReaction(requestFaction, objectFaction);
        ReputationRank reactionHum = PlayerbotAI::GetFactionReaction(humanFaction, objectFaction);
        ReputationRank reactionOrc = PlayerbotAI::GetFactionReaction(orcFaction, objectFaction);

        out2 << " faction:" << guidP.GetGameObjectInfo()->faction << " reaction me: " << rep[reactionRequest] << ",alliance: " << rep[reactionHum] << " ,horde: " << rep[reactionOrc];
    }

    ai->TellPlayerNoFacing(requester, out2);

    std::unordered_map<uint32, std::string>  types;
    types[GAMEOBJECT_TYPE_DOOR] = "GAMEOBJECT_TYPE_DOOR";
    types[GAMEOBJECT_TYPE_BUTTON] = "GAMEOBJECT_TYPE_BUTTON";
    types[GAMEOBJECT_TYPE_QUESTGIVER] = "GAMEOBJECT_TYPE_QUESTGIVER";
    types[GAMEOBJECT_TYPE_CHEST] = "GAMEOBJECT_TYPE_CHEST";
    types[GAMEOBJECT_TYPE_BINDER] = "GAMEOBJECT_TYPE_BINDER";
    types[GAMEOBJECT_TYPE_GENERIC] = "GAMEOBJECT_TYPE_GENERIC";
    types[GAMEOBJECT_TYPE_TRAP] = "GAMEOBJECT_TYPE_TRAP";
    types[GAMEOBJECT_TYPE_CHAIR] = "GAMEOBJECT_TYPE_CHAIR";
    types[GAMEOBJECT_TYPE_SPELL_FOCUS] = "GAMEOBJECT_TYPE_SPELL_FOCUS";
    types[GAMEOBJECT_TYPE_TEXT] = "GAMEOBJECT_TYPE_TEXT";
    types[GAMEOBJECT_TYPE_GOOBER] = "GAMEOBJECT_TYPE_GOOBER";
    types[GAMEOBJECT_TYPE_TRANSPORT] = "GAMEOBJECT_TYPE_TRANSPORT";
    types[GAMEOBJECT_TYPE_AREADAMAGE] = "GAMEOBJECT_TYPE_AREADAMAGE";
    types[GAMEOBJECT_TYPE_CAMERA] = "GAMEOBJECT_TYPE_CAMERA";
    types[GAMEOBJECT_TYPE_MAP_OBJECT] = "GAMEOBJECT_TYPE_MAP_OBJECT";
    types[GAMEOBJECT_TYPE_MO_TRANSPORT] = "GAMEOBJECT_TYPE_MO_TRANSPORT";
    types[GAMEOBJECT_TYPE_DUEL_ARBITER] = "GAMEOBJECT_TYPE_DUEL_ARBITER";
    types[GAMEOBJECT_TYPE_FISHINGNODE] = "GAMEOBJECT_TYPE_FISHINGNODE";
    types[GAMEOBJECT_TYPE_SUMMONING_RITUAL] = "GAMEOBJECT_TYPE_SUMMONING_RITUAL";
    types[GAMEOBJECT_TYPE_MAILBOX] = "GAMEOBJECT_TYPE_MAILBOX";
    types[GAMEOBJECT_TYPE_AUCTIONHOUSE] = "GAMEOBJECT_TYPE_AUCTIONHOUSE";
    types[GAMEOBJECT_TYPE_GUARDPOST] = "GAMEOBJECT_TYPE_GUARDPOST";
    types[GAMEOBJECT_TYPE_SPELLCASTER] = "GAMEOBJECT_TYPE_SPELLCASTER";
    types[GAMEOBJECT_TYPE_MEETINGSTONE] = "GAMEOBJECT_TYPE_MEETINGSTONE";
    types[GAMEOBJECT_TYPE_FLAGSTAND] = "GAMEOBJECT_TYPE_FLAGSTAND";
    types[GAMEOBJECT_TYPE_FISHINGHOLE] = "GAMEOBJECT_TYPE_FISHINGHOLE";
    types[GAMEOBJECT_TYPE_FLAGDROP] = "GAMEOBJECT_TYPE_FLAGDROP";
    types[GAMEOBJECT_TYPE_MINI_GAME] = "GAMEOBJECT_TYPE_MINI_GAME";
    types[GAMEOBJECT_TYPE_LOTTERY_KIOSK] = "GAMEOBJECT_TYPE_LOTTERY_KIOSK";
    types[GAMEOBJECT_TYPE_CAPTURE_POINT] = "GAMEOBJECT_TYPE_CAPTURE_POINT";
    types[GAMEOBJECT_TYPE_AURA_GENERATOR] = "GAMEOBJECT_TYPE_AURA_GENERATOR";

    ai->TellPlayerNoFacing(requester, types[guidP.GetGameObjectInfo()->type]);

    if (guidP.GetGameObject(bot->GetInstanceId()))
    {
        GameObject* object = guidP.GetGameObject(bot->GetInstanceId());

        GOState state = object->GetGoState();

        std::ostringstream out;

        out << "state:";

        if (state == GO_STATE_ACTIVE)
            out << "GO_STATE_ACTIVE";
        if (state == GO_STATE_READY)
            out << "GO_STATE_READY";
        if (state == GO_STATE_ACTIVE_ALTERNATIVE)
            out << "GO_STATE_ACTIVE_ALTERNATIVE";

        out << (object->getLootState() == GO_ACTIVATED ? ", in use" : ", not in use");

        LootState lootState = object->getLootState();

        out << " lootState:";

        if (lootState == GO_NOT_READY)
            out << "GO_NOT_READY";
        if (lootState == GO_READY)
            out << "GO_READY";
        if (lootState == GO_ACTIVATED)
            out << "GO_ACTIVATED";
        if (lootState == GO_JUST_DEACTIVATED)
            out << "GO_JUST_DEACTIVATED";

        ai->TellPlayerNoFacing(requester, out);
    }

    return true;
}

bool DebugAction::HandleFind(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;

    if (text.size() <= 5)
        return false;

    std::string link = text.substr(5);

    if (link.empty())
        return false;

    if (!Qualified::isValidNumberString(link))
        return false;

    uint32 entry = stoi(link);

    Creature* creature = nullptr;
    MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck creature_check(*bot, entry, true, 1000.0f);
    MaNGOS::CreatureLastSearcher<MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck> searcher(creature, creature_check);
    Cell::VisitGridObjects(bot, searcher, 1000.0f);

    if (!creature)
        return false;

    out << "Found: ";
    out << chat->formatWorldobject(creature);
    out << " at distance ";
    out << bot->GetDistance(creature);

    ai->TellPlayerNoFacing(requester, out);

    return true;
}
