
#include "playerbot/playerbot.h"
#include "AddLootAction.h"

#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

#include "playerbot/strategy/actions/DestroyItemAction.h"

using namespace ai;
using namespace MaNGOS;

using namespace ai;

bool AddLootAction::Execute(Event& event)
{
    ObjectGuid guid = event.getObject();
    if (!guid)
        return false;

    return AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
}

bool AddAllLootAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    bool added = false;

    std::string text = event.getParam();

    if (!text.empty())
    {
        std::list<ObjectGuid> objects = ChatHelper::parseGameobjects(text);

        for (auto& guid : objects)
            added |= AddLoot(requester, guid);
    }
    else
    {
        std::list<ObjectGuid> gos = context->GetValue<std::list<ObjectGuid>>("nearest game objects no los")->Get();
        for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
            added |= AddLoot(requester, *i);

        std::list<ObjectGuid> corpses = context->GetValue<std::list<ObjectGuid>>("nearest corpses")->Get();
        for (std::list<ObjectGuid>::iterator i = corpses.begin(); i != corpses.end(); i++)
            added |= AddLoot(requester, *i);
    }

    return added;
}

bool AddLootAction::isUseful()
{
    return true;
}

bool AddAllLootAction::isUseful()
{
    return true;
}

bool AddAllLootAction::AddLoot(Player* requester, ObjectGuid guid)
{
    LootObject loot(bot, guid);

    if (ai->HasStrategy("debug loot", BotState::BOT_STATE_NON_COMBAT))
        loot.Refresh(bot, guid, true);

    WorldObject* wo = loot.GetWorldObject(bot);

    if (!wo)
    {
        wo = ai->GetWorldObject(guid);

        if (!wo)
            ai->TellDebug(requester, "Trying to add loot from " + std::to_string(guid) + " but it doesn't exists.", "debug loot");
        else
            ai->TellDebug(requester, "for trying to add loot from " + ChatHelper::formatWorldobject(wo), "debug loot");

        sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (no lootable WorldObject: corpse not tapped/looted-by-bot or wrong type)",
            bot->GetName(), guid.GetRawValue());
        return false;
    }
    else
    {
        ai->TellDebug(requester, "Add loot from " + ChatHelper::formatWorldobject(wo), "debug loot");
    }

    if (loot.IsEmpty())
    {
        ai->TellDebug(requester, "Loot object is empty.", "debug loot");
        sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (loot object empty / not lootable-tapped)",
            bot->GetName(), guid.GetRawValue());
        return false;
    }

    if (abs(wo->GetPositionZ() - bot->GetPositionZ()) > INTERACTION_DISTANCE)
    {
        ai->TellDebug(requester, "Object too high or low.", "debug loot");
        sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (z-diff %.1f > %.1f)",
            bot->GetName(), guid.GetRawValue(), (float)fabs(wo->GetPositionZ() - bot->GetPositionZ()), (float)INTERACTION_DISTANCE);
        return false;
    }

    if (!loot.IsLootPossible(bot))
    {
        ai->TellDebug(requester, "Looting is not possible.", "debug loot");
        sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (IsLootPossible=false)",
            bot->GetName(), guid.GetRawValue());
        return false;
    }

    float lootDistanceToUse = sPlayerbotAIConfig.lootDistance;

    Group* group = bot->GetGroup();

    bool isInGroup = group ? true : false;
    bool isInDungeon = bot->GetMap()->IsDungeon();

    if (isInGroup)
    {
        //if is not master looter (and loot is set to MASTER_LOOT)
        //NOTE: They are !unable to loot quests items! too if so
        if (isInDungeon
            && group->GetLootMethod() == LootMethod::MASTER_LOOT
            && group->GetMasterLooterGuid()
            && group->GetMasterLooterGuid() != bot->GetObjectGuid())
        {
            ai->TellDebug(requester, "Not master looter.", "debug loot");
            sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (dungeon MASTER_LOOT, not master looter)",
                bot->GetName(), guid.GetRawValue());
            return false;
        }

        if (ai->IsGroupLeader())
        {
            lootDistanceToUse = sPlayerbotAIConfig.lootDistance;
        }
        else
        {
            if (ai->HasActivePlayerMaster())
            {
                lootDistanceToUse = sPlayerbotAIConfig.groupMemberLootDistanceWithActiveMaster;
            }
            else
            {
                lootDistanceToUse = sPlayerbotAIConfig.groupMemberLootDistance;
            }
        }
    }
    else
    {
        lootDistanceToUse = sPlayerbotAIConfig.lootDistance;
    }

    float botDist = sServerFacade.GetDistance2d(bot, wo);
    float masterDist = sServerFacade.GetDistance2d(requester, wo);
    if (sServerFacade.IsDistanceGreaterThan(masterDist, lootDistanceToUse))
    {
        ai->TellDebug(requester, "Outside of loot range: " + std::to_string(lootDistanceToUse), "debug loot");
        sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (range: masterDist=%.1f > cap=%.1f; botDist=%.1f)",
            bot->GetName(), guid.GetRawValue(), masterDist, lootDistanceToUse, botDist);
        return false;
    }

    //check hostile units after distance checks, to avoid unnecessary calculations

    if (isInGroup && !ai->IsGroupLeader())
    {
        float MOB_AGGRO_DISTANCE = 30.0f;
        std::list<Unit*> hostiles = ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, MOB_AGGRO_DISTANCE);

        if (hostiles.size() > 0)
        {
            std::ostringstream out;
            out << hostiles.front()->GetName() << " is blocking " << wo->GetName() << ", need to kill it or I will not loot";
            ai->TellError(requester, out.str());
            sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (hostile '%s' within %.0fy of corpse, count=%zu)",
                bot->GetName(), guid.GetRawValue(), hostiles.front()->GetName(), MOB_AGGRO_DISTANCE, hostiles.size());
            return false;
        }
    }

    uint8 usedBagSpacePercent = AI_VALUE(uint8, "bag space");

    if (usedBagSpacePercent > 99 && !ai->CanLootSomethingFromWO(wo))
    {
        if (ai->HasQuestItemsInWOLootList(wo))
        {
            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("destroy all gray"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("smart destroy item"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99)
            {
                ai->TellPlayer(requester, "Can not loot quest item, my bags are full", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (bags full, quest item)", bot->GetName(), guid.GetRawValue());
                return false;
            }

        }

        if (usedBagSpacePercent > 99)
        {
            ai->TellError(requester, "There is some loot but I do not have free bag space, so not looting");
            sLog.outDebug("[BOT LOOT] %s: AddLoot reject guid=%lu (bags full)", bot->GetName(), guid.GetRawValue());
            return false;
        }
    }

    bool added = AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
    sLog.outDebug("[BOT LOOT] %s: AddLoot queued guid=%lu (botDist=%.1f masterDist=%.1f cap=%.1f added=%d)",
        bot->GetName(), guid.GetRawValue(), botDist, masterDist, lootDistanceToUse, added ? 1 : 0);
    return added;
}

bool AddGatheringLootAction::AddLoot(Player* requester, ObjectGuid guid)
{
    LootObject loot(bot, guid);

    WorldObject *wo = loot.GetWorldObject(bot);
    if (loot.IsEmpty() || !wo)
        return false;

    if (!sServerFacade.IsWithinLOSInMap(bot, wo))
        return false;

    if (loot.skillId == SKILL_NONE)
        return false;

    if (!loot.IsLootPossible(bot))
        return false;

    float gatheringDistanceToUse = sPlayerbotAIConfig.gatheringDistance;

    Group* group = bot->GetGroup();

    bool isInGroup = group ? true : false;
    bool isInDungeon = bot->GetMap()->IsDungeon();

    if (isInGroup && !ai->IsGroupLeader())
    {
        if (ai->HasActivePlayerMaster())
        {
            gatheringDistanceToUse = sPlayerbotAIConfig.groupMemberGatheringDistanceWithActiveMaster;
        }
        else
        {
            gatheringDistanceToUse = sPlayerbotAIConfig.groupMemberGatheringDistance;
        }
    }
    else if (ai->IsGroupLeader())
    {
        gatheringDistanceToUse = sPlayerbotAIConfig.gatheringDistance;
    }
    else
    {
        gatheringDistanceToUse = sPlayerbotAIConfig.gatheringDistance;
    }

    if (sServerFacade.IsDistanceGreaterThan(sServerFacade.GetDistance2d(requester, wo), gatheringDistanceToUse))
    {
        return false;
    }

    //check hostile units after distance checks, to avoid unnecessary calculations

    float MOB_AGGRO_DISTANCE = 30.0f;
    std::list<Unit*> hostiles = ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, MOB_AGGRO_DISTANCE);
    std::list<Unit*> strongHostiles;
    for (auto hostile : hostiles)
    {
        if (!(bot->GetLevel() > hostile->GetLevel() + 7))
        {
            strongHostiles.push_back(hostile);
        }
    }

    if (isInGroup && !ai->IsGroupLeader())
    {
        if (hostiles.size() > 0)
        {
            std::ostringstream out;
            out << hostiles.front()->GetName() << " is blocking " << wo->GetName() << ", need to kill it or I will not gather";
            ai->TellError(requester, out.str());
            return false;
        }
    }
    else
    {
        if (strongHostiles.size() > 1)
        {
            std::ostringstream out;
            out << strongHostiles.front()->GetName() << " is blocking " << wo->GetName() << ", need to kill it or I will not gather";
            ai->TellError(requester, out.str());
            return false;
        }
    }

    uint8 usedBagSpacePercent = AI_VALUE(uint8, "bag space");

    if (usedBagSpacePercent > 99 && !ai->CanLootSomethingFromWO(wo))
    {
        if (ai->HasQuestItemsInWOLootList(wo))
        {
            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("destroy all gray"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("smart destroy item"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99)
            {
                ai->TellPlayer(requester, "Can not loot quest item, my bags are full", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                return false;
            }

        }

        if (usedBagSpacePercent > 99)
        {
            ai->TellError(requester, "There is some loot but I do not have free bag space, so not looting");
            return false;
        }
    }

    return AddAllLootAction::AddLoot(requester, guid);
}
