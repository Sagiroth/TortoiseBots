#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/Formations.h"

#include "playerbot/strategy/Action.h"

namespace ai
{
    class AcceptInvitationAction : public Action
    {
    public:
        AcceptInvitationAction(PlayerbotAI* ai) : Action(ai, "accept invitation") {}

        virtual bool Execute(Event& event) override
        {
            Group* grp = bot->GetGroupInvite();
            if (!grp)
            {
                sLog.outDebug("TortoiseBots: AcceptInvitationAction %s has no group invite", bot->GetName());
                return false;
            }

            Player* inviter = sObjectMgr.GetPlayer(grp->GetLeaderGuid());
            if (!inviter)
            {
                sLog.outDebug("TortoiseBots: AcceptInvitationAction %s cannot resolve inviter guid %s",
                    bot->GetName(), grp->GetLeaderGuid().GetString().c_str());
                return false;
            }
            // Bot-to-bot pool grouping is governed only by
            // RandomBotGroupNearby: decline another pool bot's invite only
            // when it is 0. Invites from real players, hires and
            // owner-account bots pass through: their AI carries a
            // real-player master (or they are not random pool records).
            if (!sPlayerbotAIConfig.randomBotGroupNearby &&
                TortoiseBots::BotManager::Instance().IsRandomBot(bot->GetObjectGuid()) &&
                !ai->HasRealPlayerMaster() &&
                TortoiseBots::BotManager::Instance().IsRandomBot(inviter->GetObjectGuid()))
            {
                PlayerbotAI* inviterAi = PlayerbotAIStorage::Instance().GetAI(inviter);
                if (!inviterAi || !inviterAi->HasRealPlayerMaster())
                {
                    sLog.outDebug("TortoiseBots: AcceptInvitationAction %s declines pool-bot invite from %s",
                        bot->GetName(), inviter->GetName());
                    WorldPacket data(SMSG_GROUP_DECLINE, 10);
                    data << bot->GetName();
                    sServerFacade.SendPacket(inviter, data);
                    bot->UninviteFromGroup();
                    return false;
                }
            }

			bool allowed = ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE, false, inviter);
            sLog.outDebug("TortoiseBots: AcceptInvitationAction %s inviter %s security %u",
                bot->GetName(), inviter->GetName(), allowed ? 1 : 0);
			if (!allowed)
            {
                WorldPacket data(SMSG_GROUP_DECLINE, 10);
                data << bot->GetName();
                sServerFacade.SendPacket(inviter, data);
                bot->UninviteFromGroup();
                return false;
            }

            if (bot->IsAFK())
                bot->ToggleAFK();

            WorldPacket p;
            uint32 roles_mask = 0;
            p << roles_mask;
            bot->GetSession()->HandleGroupAcceptOpcode(p);

            if (!bot->GetGroup() || !bot->GetGroup()->IsMember(inviter->getObjectGuid()))
                return false;

            bool adoptedHumanMaster = false;
            if (TortoiseBots::BotManager::Instance().IsBot(bot->GetObjectGuid()))
            {
                adoptedHumanMaster = TortoiseBots::BotManager::Instance().BindBotMaster(
                    bot->GetObjectGuid(), inviter->GetObjectGuid());
                if (!adoptedHumanMaster)
                {
                    sLog.outError("TortoiseBots: module bot %s accepted master %s but durable master bind failed; keeping ownership unchanged",
                        bot->GetName(), inviter->GetName());
                    return false;
                }
            }

            ai->ResetStrategies();

            ai->ChangeStrategy("-lfg,-bg", BotState::BOT_STATE_NON_COMBAT);
            ai->Reset();

            if (adoptedHumanMaster)
            {
                ai::Event followEvent("follow", "", inviter);
                if (!ai->DoSpecificAction("follow chat shortcut", followEvent, true))
                {
                    sLog.outError("TortoiseBots: mature follow action failed after bot %s joined human %s",
                        bot->GetName(), inviter->GetName());
                    return false;
                }
            }

            sPlayerbotAIConfig.logEvent(ai, "AcceptInvitationAction", grp->GetLeaderName(), std::to_string(grp->GetMembersCount()));

            Player* master = inviter;
            bool inviterIsBot = TortoiseBots::BotManager::Instance().IsBot(inviter->getObjectGuid());

            if (PlayerbotAIStorage::Instance().GetAI(master)) //Copy formation from bot master.
            {
                Formation* masterFormation = MAI_VALUE(Formation*, "formation");
                FormationValue* value = (FormationValue*)context->GetValue<Formation*>("formation");
                value->Load(masterFormation->GetName());
            }

            // Greet the new master, but never another pool bot: the grouper
            // invites nearby bots non-stop while a pool fills up, and a bot
            // answering the bot that invited it is chatter no human reads.
            if (!inviterIsBot)
            {
                ai->TellPlayer(inviter, BOT_TEXT("hello"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            ai->DoSpecificAction("reset raids", event, true);
            ai->DoSpecificAction("update gear", event, true);

            return true;
        }

        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "accept invitation"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot accept group invitations.\n"
                   "It will automatically handle AFK status and update strategies.\n"
                   "For free bots, the inviter becomes the bot's master.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {"reset raids", "update gear"}; }
        virtual std::vector<std::string> GetUsedValues() { return {"formation"}; }
#endif
    };

}
