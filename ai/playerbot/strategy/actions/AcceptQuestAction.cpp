
#include "playerbot/playerbot.h"
#include "AcceptQuestAction.h"
#include "playerbot/PullRegenPolicy.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

using namespace ai;

static bool IsFixedRewardUpgrade(AiObjectContext* context, Quest const* quest)
{
    for (uint8 i = 0; i < quest->GetRewItemsCount(); ++i)
    {
        ItemUsage usage = AI_VALUE2_LAZY(ItemUsage, "item usage", quest->RewItemId[i]);
        if (usage == ItemUsage::ITEM_USAGE_EQUIP || usage == ItemUsage::ITEM_USAGE_BAD_EQUIP)
            return true;
    }
    return false;
}

//The quest-log policy this action applies before it takes a quest. Exposed so
//the idle nearby-service rule can ask a giver the same question instead of
//walking to one whose only quest is blocked here (leave-the-valley quest
//level below 10, CLUCK, the hardcore challenge, the Tortoise rogue quests,
//grey quests with a useless reward) - one policy, so the two cannot drift.
bool AcceptAllQuestsAction::WouldAcceptQuest(PlayerbotAI* ai, Player* bot, Quest const* quest, WorldObject* questGiver)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    // Leave-the-valley hand-ins (end-of-start-zone deliveries) are not
    // taken early: their taker sits in the next town while the bot is
    // level 1-2, and the trip out dies on the way. A pool bot below 10
    // only takes a quest rated at most one above its own level (same +1
    // as the grind order cap, QuestTakerTripFits); the trip is offered
    // again once the bot reaches the quest's level. QuestLevel 0 is
    // scaling content, never blocked; owned/hired bots follow the player.
    // Donor mod-playerbots IsQuestCapableDoing (NewRpgBaseAction.cpp:573)
    // refuses +3 at any level; the pool uses +1 below 10.
    if (!ai::QuestTakerTripFits((int)quest->GetQuestLevel(), false, bot->GetLevel(),
        sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster()))
        return false;

    // CLUCK! — a novelty quest bots can't meaningfully complete; block entirely.
    if (quest->GetQuestId() == 3861)
        return false;

    // Challenge quests (Hardcore mode, etc.) - must never be taken by bots.
    if (quest->GetQuestId() == 80388)
        return false;

    if (questGiver && (questGiver->GetEntry() == 81030 || questGiver->GetEntry() == 62609))
        return false;

    // Tortoise-wow rogue-only quests with excessive walking and poor reward.
    static const std::unordered_set<uint32> tortoiseOnlyBlacklist = {
        50000, // Professor Malkovich
        50003, // Professor Papucho
    };
    if (tortoiseOnlyBlacklist.count(quest->GetQuestId()))
        return false;

    // Quest-log upkeep for masterless random bots (donor IsQuestWorthDoing
    // idea, own code): skip grey quests unless the reward is worth it. Grey
    // here is quest level + low-level-hide-diff < bot level; the need-reward
    // check below is the same "need quest reward" value the travel layer
    // consults before fetching grey givers (QuestValues.cpp, TravelMgr.cpp),
    // extended to fixed RewItemId upgrades as well as choice rewards.
    if (sPlayerbotAIConfig.botQuestLogUpkeep &&
        !ai->HasActivePlayerMaster() &&
        sRandomBotFacade.IsRandomBot(bot) &&
        !quest->GetRequiredClasses() &&
        quest->GetQuestLevel() > 0 &&
        bot->GetLevel() > bot->GetQuestLevelForPlayer(quest) + (uint32)sWorld.getConfig(CONFIG_INT32_QUEST_LOW_LEVEL_HIDE_DIFF) + 1)
    {
        if (!AI_VALUE2(bool, "need quest reward", (int32)quest->GetQuestId()) && !IsFixedRewardUpgrade(context, quest))
            return false;
    }

    return true;
}

//Any quest in this giver's menu that WouldAcceptQuest() accepts and the bot can
//pick up right now. The idle nearby-service rule walks to a giver only when this
//is true, so it cannot park itself at a giver whose quests are all blocked.
bool AcceptAllQuestsAction::OffersAcceptableQuest(PlayerbotAI* ai, Player* bot, WorldObject* questGiver)
{
    if (!questGiver)
        return false;

    bot->PrepareQuestMenu(questGiver->getObjectGuid());
    QuestMenu& menu = bot->PlayerTalkClass->GetQuestMenu();

    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
    {
        QuestMenuItem const& item = menu.GetItem(i);

        if (item.m_qIcon != DIALOG_STATUS_AVAILABLE) //Only what the bot could take now.
            continue;

        Quest const* quest = sObjectMgr.GetQuestTemplate(item.m_qId);

        if (quest && WouldAcceptQuest(ai, bot, quest, questGiver))
            return true;
    }

    return false;
}

bool AcceptAllQuestsAction::ProcessQuest(Player* requester, Quest const* quest, WorldObject* questGiver)
{
    if (!WouldAcceptQuest(ai, bot, quest, questGiver))
        return false;

    if (AcceptQuest(requester, quest, questGiver->getObjectGuid()))
    {
        if (sPlayerbotAIConfig.globalSoundEffects)
            bot->PlayDistanceSound(620);
        return true;
    }

    return false;
}

bool AcceptQuestAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (!requester)
        return false;

    Player *bot = ai->GetBot();
    uint64 guid = 0;
    uint32 quest = 0;

    std::string text = event.GetParam();
    PlayerbotChatHandler ch(requester);
    quest = ch.extractQuestId(text);

    bool hasAccept = false;

    if (event.GetPacket().empty())
    {
        std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
        for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
        {
            Unit* unit = ai->GetUnit(*i);
            if (unit && quest && unit->HasQuest(quest))
            {
                guid = unit->getObjectGuid().GetRawValue();
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
                guid = go->getObjectGuid().GetRawValue();
                break;
            }
            if (go && text == "*" && bot->GetDistance(go) <= INTERACTION_DISTANCE)
                hasAccept |= QuestAction::ProcessQuests(go);
        }
    }
    else
    {
        WorldPacket& p = event.GetPacket();
        p.rpos(0);
        p >> guid >> quest;
    }

    if (!quest || !guid || quest == 80388)
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
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Player* bot = ai->GetBot();

    if (!ai->IsSafe(requester))
        requester = bot;

    WorldPacket& p = event.GetPacket();
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
            bot->CastSpell(bot, qInfo->GetSrcSpell(), true);
        }

        ai->TellPlayer(requester, BOT_TEXT("quest_accept"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    return false;
}

bool ConfirmQuestAction::Execute(Event& event)
{
    Player *bot = ai->GetBot();
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    WorldPacket& p = event.GetPacket();
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
            bot->CastSpell(bot, qInfo->GetSrcSpell(), true);
        }

        ai->TellPlayer(requester, BOT_TEXT("quest_accept"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    return false;
}

bool QuestDetailsAction::Execute(Event& event)
{
    Player* bot = ai->GetBot();
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    WorldPacket& p = event.GetPacket();
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

        // The gossip/quest-details path is the one accept that used to be
        // invisible in bot_events.csv (the emote-time hello makes the core send
        // the details page for a single-entry quest menu). Log it under the same
        // name QuestAction::AcceptQuest uses so the quest ledger counts it.
        sPlayerbotAIConfig.logEvent(ai, "AcceptQuestAction", qInfo->GetTitle(), std::to_string(qInfo->GetQuestId()));

        if (bot->CanCompleteQuest(quest))
            bot->CompleteQuest(quest);

        if (qInfo->GetSrcSpell() > 0)
        {
            bot->CastSpell(bot, qInfo->GetSrcSpell(), true);
        }

        ai->TellPlayer(requester, BOT_TEXT("quest_accept"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    return false;
}
