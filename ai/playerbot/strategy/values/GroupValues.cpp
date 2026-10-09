
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "GroupValues.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

std::list<ObjectGuid> GroupMembersValue::Calculate()
{
    std::list<ObjectGuid> members;

    Group* group = bot->GetGroup();
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            members.push_back(member->getObjectGuid());
        }
    }
    else
        members.push_back(bot->getObjectGuid());

    return members;
}


bool IsFollowingPartyValue::Calculate()
{
    if (ai->GetGroupMaster() == bot)
        return true;

    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
        return true;

    return false;
}

bool IsNearLeaderValue::Calculate()
{
    Player* groupMaster = ai->GetGroupMaster();

    if (!groupMaster)
        return false;

    if (groupMaster == bot)
        return true;

    return sServerFacade.getDistance2d(bot, ai->GetGroupMaster()) < sPlayerbotAIConfig.reactDistance;
}

uint32 GroupBoolCountValue::Calculate()
{
    uint32 count = 0;

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!PlayerbotAIStorage::Instance().GetAI(player))
            continue;

        if (PAI_VALUE2(bool, "and", getQualifier()))
            return count++;
    }

    return count;
};

bool GroupBoolANDValue::Calculate()
{
    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!PlayerbotAIStorage::Instance().GetAI(player))
            continue;

        if (!PAI_VALUE2(bool,"and", getQualifier()))
            return false;
    }

    return true;
};

bool GroupBoolORValue::Calculate()
{
    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!PlayerbotAIStorage::Instance().GetAI(player))
            continue;

        if (PAI_VALUE2(bool, "and", getQualifier()))
            return true;
    }

    return false;
};

bool GroupReadyValue::Calculate()
{
    bool inDungeon = !WorldPosition(bot).isOverworld();

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* member = sObjectMgr.GetPlayer(guid);

        if (!member)
            continue;

        if (inDungeon) // In dungeons all following members need to be alive before continuing.
        {
            PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);

            bool isFollowing = memberAi
                ? (memberAi->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
                    memberAi->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
                : true;

            if (!member->IsAlive() && isFollowing)
                return false;
        }
        //We only wait for members that are in range otherwise we might be waiting for bots stuck in dead loops forever.
        if (ai->GetGroupMaster() && sServerFacade.getDistance2d(member, ai->GetGroupMaster()) > sPlayerbotAIConfig.sightDistance)
            continue;

        // Between pulls the party drinks/eats together: like the donor
        // (GroupReadyValue, no attacker gate), hold movement until members
        // are topped up. The live hasAttackers conjunct released the wait the
        // moment a fight ended, so wounded/OOM bots walked on at once.
        // Still skip members already fighting (they are being healed, not
        // resting) and mana-less classes below.
        if (member->GetHealthPercent() < sPlayerbotAIConfig.almostFullHealth && !member->IsInCombat())
            return false;

        if (!member->GetPower(POWER_MANA))
            continue;

        float mana = (static_cast<float> (member->GetPower(POWER_MANA)) / member->GetMaxPower(POWER_MANA)) * 100;

        if (mana < sPlayerbotAIConfig.mediumMana && !member->IsInCombat())
            return false;
    }

    return true;
};