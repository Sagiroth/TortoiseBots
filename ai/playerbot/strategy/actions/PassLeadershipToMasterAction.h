#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/GroupHygienePolicy.h"
#include "GenericActions.h"

namespace ai
{
    class PassLeadershipToMasterAction : public ChatCommandAction
    {
    public:
        PassLeadershipToMasterAction(PlayerbotAI* ai, std::string name = "leader", std::string message = "Passing leader to you!") : ChatCommandAction(ai, name), message(message) {}

        virtual Player* PassLeaderTo(Event& event) { return GetMaster(); };

        virtual bool Execute(Event& event) override
        {
            Player* passLeaderTo = PassLeaderTo(event);
            if (passLeaderTo && passLeaderTo != bot && bot->GetGroup() && bot->GetGroup()->IsMember(passLeaderTo->getObjectGuid()))
            {
                WorldPacket p(SMSG_GROUP_SET_LEADER, 8);
                p << passLeaderTo->getObjectGuid();
                bot->GetSession()->HandleGroupSetLeaderOpcode(p);

                if (!message.empty())
                    ai->TellPlayerNoFacing(passLeaderTo, message);

                if (TortoiseBots::BotManager::Instance().IsRandomBot(bot->GetObjectGuid()))
                {
                    ai->ResetStrategies();
                    ai->Reset();
                }

                return true;
            }

            return false;
        }

        virtual bool isUseful() override
        {
            return ai->IsAlt() && bot->GetGroup() && bot->GetGroup()->IsLeader(bot->getObjectGuid());
        }

        virtual bool isUsefulWhenStunned() override { return true; }

    protected:
        std::string message;
    };

    class GiveLeaderAction : public PassLeadershipToMasterAction
    {
    public:
        GiveLeaderAction(PlayerbotAI* ai, std::string message = "Lead the way!") : PassLeadershipToMasterAction(ai, "give leader", message) {}

        virtual Player* PassLeaderTo(Event& event) { return event.GetOwner(); };

        virtual bool isUseful() override
        {
            return bot->GetGroup() && bot->GetGroup()->IsLeader(bot->getObjectGuid());
        }
    };

    // Yield leadership to a real-player master inside a dungeon (SOC-G4):
    // the bot does not know the dungeon, the master does. Donor
    // mod-playerbots UnknownDungeonTrigger behavior; fires from the group
    // strategy's seldom tick, so the target must be the master (strategy
    // events carry no whisper owner).
    class GiveLeaderInDungeonAction : public GiveLeaderAction
    {
    public:
        GiveLeaderInDungeonAction(PlayerbotAI* ai, std::string message = "I don't know this dungeon, lead the way!") : GiveLeaderAction(ai, message) {}

        virtual Player* PassLeaderTo(Event& event) { return GetMaster(); };

        virtual bool isUseful() override
        {
            Player* master = GetMaster();
            Map* masterMap = (master && master->IsInWorld()) ? master->GetMap() : nullptr;
            return ai::DungeonLeadershipYield(
                bot->GetGroup() && bot->GetGroup()->IsLeader(bot->getObjectGuid()),
                ai->HasRealPlayerMaster() && master != nullptr,
                master && master->IsInWorld(),
                masterMap && masterMap->IsDungeon(),
                master && bot->GetMapId() == master->GetMapId());
        }
    };
}
