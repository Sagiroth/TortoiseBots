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
            if (TortoiseBots::BotManager::Instance().IsRandomBot(bot->GetObjectGuid()))
            {
                adoptedHumanMaster = TortoiseBots::BotManager::Instance().BindBotMaster(
                    bot->GetObjectGuid(), inviter->GetObjectGuid());
                if (!adoptedHumanMaster)
                {
                    sLog.outError("TortoiseBots: random bot %s accepted human %s but durable master bind failed",
                        bot->GetName(), inviter->GetName());
                    ai->SetMaster(inviter);
                }
            }

            ai->ResetStrategies();
            
            ai->ChangeStrategy("-lfg,-bg", BotState::BOT_STATE_NON_COMBAT);
            ai->Reset();

            if (adoptedHumanMaster)
                ai->SetMovementStrategy("follow");

            sPlayerbotAIConfig.logEvent(ai, "AcceptInvitationAction", grp->GetLeaderName(), std::to_string(grp->GetMembersCount()));

            Player* master = inviter;

            if (PlayerbotAIStorage::Instance().GetAI(master)) //Copy formation from bot master.
            {
                if (sPlayerbotAIConfig.inviteChat && (TortoiseBots::BotManager::Instance().IsRandomBot(bot->GetObjectGuid()) || !ai->HasActivePlayerMaster()))
                {
                    std::map<std::string, std::string> placeholders;
                    placeholders["%name"] = master->GetName();
                    std::string reply;
                    if (urand(0, 3))
                        reply = BOT_TEXT2("Send me an invite %name!", placeholders);
                    else
                        reply = BOT_TEXT2("Sure I will join you.", placeholders);

                    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());

                    if (guild && master->IsInGuild(bot->GetGuildId()))
                        guild->BroadcastToGuild(bot->GetSession(), reply, LANG_UNIVERSAL);
                    else if (sServerFacade.getDistance2d(bot, master) < sPlayerbotAIConfig.spellDistance * 1.5)
                        bot->Say(reply, (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                }

                Formation* masterFormation = MAI_VALUE(Formation*, "formation");
                FormationValue* value = (FormationValue*)context->GetValue<Formation*>("formation");
                value->Load(masterFormation->GetName());
            }

            ai->TellPlayer(inviter, BOT_TEXT("hello"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

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
