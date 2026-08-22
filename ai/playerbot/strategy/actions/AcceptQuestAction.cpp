
#include "playerbot/playerbot.h"
#include "AcceptQuestAction.h"

using namespace ai;

bool AcceptAllQuestsAction::ProcessQuest(Player* requester, Quest const* quest, WorldObject* questGiver)
{
    // Breadcrumb quests that lead bots out of the starting zone into dangerous territory.
    // Block until level 5 when the bot is actually ready to move on.
    static const std::unordered_set<uint32> startingZoneBreadcrumbs = {
        2158, // Rest and Relaxation    (Human      -> Goldshire inn)
        1656, // A Task Unfinished      (Tauren     -> Mulgore)
        2159, // Dolanaar Delivery      (NElf       -> Dolanaar inn)
        8,    // A Rogue's Deal         (Undead     -> Deathknell)
        2160, // Supplies to Tannok     (Dwarf/Gnome -> Dun Morogh)
        2161, // A Peon's Burden        (Orc        -> Durotar)
    };
    if (startingZoneBreadcrumbs.count(quest->GetQuestId()) && bot->GetLevel() < 5)
        return false;

    // CLUCK! — a novelty quest bots can't meaningfully complete; block entirely.
    if (quest->GetQuestId() == 3861)
        return false;

    // Tortoise-wow rogue-only quests with excessive walking and poor reward.
    static const std::unordered_set<uint32> turtleOnlyBlacklist = {
        50000, // Professor Malkovich
        50003, // Professor Papucho
    };
    if (turtleOnlyBlacklist.count(quest->GetQuestId()))
        return false;

    if (AcceptQuest(requester, quest, questGiver->GetObjectGuid()))
    {
        if (sPlayerbotAIConfig.globalSoundEffects)
            bot->PlayDistanceSound(620);
        return true;
    }

    return false;
}

bool AcceptQuestAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    if (!requester)
        return false;

    Player *bot = ai->GetBot();
    uint64 guid = 0;
    uint32 quest = 0;

    std::string text = event.getParam();
    PlayerbotChatHandler ch(requester);
    quest = ch.extractQuestId(text);

    bool hasAccept = false;

    if (event.getPacket().empty())
    {
        std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
        for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
        {
            Unit* unit = ai->GetUnit(*i);
            if (unit && quest && unit->HasQuest(quest))
            {
                guid = unit->GetObjectGuid().GetRawValue();
                break;
            }
            if (unit && text == "*" && bot->GetDistance(unit) <= INTERACTION_DISTANCE)
                hasAccept |= QuestAction::ProcessQuests(unit);
        }
        std::list<ObjectGuid> gos = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
        for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
        {
            GameObject* go = ai->GetGameObject(*i);
            if (go && quest && go->HasQuest(quest))
            {
                guid = go->GetObjectGuid().GetRawValue();
                break;
            }
            if (go && text == "*" && bot->GetDistance(go) <= INTERACTION_DISTANCE)
                hasAccept |= QuestAction::ProcessQuests(go);
        }
    }
    else
    {
        WorldPacket& p = event.getPacket();
        p.rpos(0);
        p >> guid >> quest;
    }

    if (!quest || !guid)
        return false;

    Quest const* qInfo = sObjectMgr.GetQuestTemplate(quest);
    if (!qInfo)
        return false;

    hasAccept |= AcceptQuest(requester, qInfo, guid);

    if (hasAccept)
        sPlayerbotAIConfig.logEvent(ai, "AcceptQuestAction", qInfo->GetTitle(), std::to_string(qInfo->GetQuestId()));

    return hasAccept;
}

bool AcceptQuestShareAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    Player* bot = ai->GetBot();

    if (!ai->IsSafe(requester))
        requester = bot;

    WorldPacket& p = event.getPacket();
    p.rpos(0);
    uint32 quest;
    p >> quest;
    Quest const* qInfo = sObjectMgr.GetQuestTemplate(quest);

    if (!qInfo || !bot->GetDividerGuid())
        return false;

    quest = qInfo->GetQuestId();
    if( !bot->CanTakeQuest( qInfo, false ) )
    {
        // can't take quest
        bot->SetDividerGuid( ObjectGuid() );
        ai->TellError(requester, BOT_TEXT("quest_cant_take"));

        return false;
    }

    if( !bot->GetDividerGuid().IsEmpty() )
    {
        // send msg to quest giving player
        requester->SendPushToPartyResponse( bot, QUEST_PARTY_MSG_ACCEPT_QUEST );
        bot->SetDividerGuid( ObjectGuid() );
    }

    if( bot->CanAddQuest( qInfo, false ) )
    {
        bot->AddQuest( qInfo, requester);

        sPlayerbotAIConfig.logEvent(ai, "AcceptQuestShareAction", qInfo->GetTitle(), std::to_string(qInfo->GetQuestId()));

        if( bot->CanCompleteQuest( quest ) )
            bot->CompleteQuest( quest );

        // Runsttren: did not add typeid switch from WorldSession::HandleQuestgiverAcceptQuestOpcode!
        // I think it's not needed, cause typeid should be TYPEID_PLAYER - and this one is not handled
        // there and there is no default case also.

        if( qInfo->GetSrcSpell() > 0 )
        {
            bot->CastSpell( bot, qInfo->GetSrcSpell(),
#ifdef MANGOS
                    true
#endif
#ifdef CMANGOS
                    (uint32)0
#endif
            );
        }

        ai->TellPlayer(requester, BOT_TEXT("quest_accept"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    return false;
}

bool ConfirmQuestAction::Execute(Event& event)
{
    Player *bot = ai->GetBot();
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    WorldPacket& p = event.getPacket();
    p.rpos(0);
    uint32 quest;
    p >> quest;
    Quest const* qInfo = sObjectMgr.GetQuestTemplate(quest);

    quest = qInfo->GetQuestId();
    if( !bot->CanTakeQuest( qInfo, false ) )
    {
        // can't take quest
        ai->TellError(requester, BOT_TEXT("quest_cant_take"));
        return false;
    }

    if( bot->CanAddQuest( qInfo, false ) )
    {
        bot->AddQuest( qInfo, requester );

        if( bot->CanCompleteQuest( quest ) )
            bot->CompleteQuest( quest );

        if( qInfo->GetSrcSpell() > 0 )
        {
            bot->CastSpell( bot, qInfo->GetSrcSpell(),
#ifdef MANGOS
                    true
#endif
#ifdef CMANGOS
                    (uint32)0
#endif
            );
        }

        ai->TellPlayer(requester, BOT_TEXT("quest_accept"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    return false;
}

bool QuestDetailsAction::Execute(Event& event)
{
    Player* bot = ai->GetBot();
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    WorldPacket& p = event.getPacket();
    p.rpos(0);
    ObjectGuid guid;
    uint32 quest;
    p >> guid;
    p >> quest;
    Quest const* qInfo = sObjectMgr.GetQuestTemplate(quest);

    if (!qInfo)
        return false;

    quest = qInfo->GetQuestId();
    if (!bot->CanTakeQuest(qInfo, false))
    {
        // can't take quest
        ai->TellError(requester, BOT_TEXT("quest_cant_take"));
        return false;
    }

    if (bot->CanAddQuest(qInfo, false))
    {
        bot->AddQuest(qInfo, requester);

        if (bot->CanCompleteQuest(quest))
            bot->CompleteQuest(quest);

        if (qInfo->GetSrcSpell() > 0)
        {
            bot->CastSpell(bot, qInfo->GetSrcSpell(),
#ifdef MANGOS
                true
#endif
#ifdef CMANGOS
                (uint32)0
#endif
            );
        }

        ai->TellPlayer(requester, BOT_TEXT("quest_accept"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    return false;
}
