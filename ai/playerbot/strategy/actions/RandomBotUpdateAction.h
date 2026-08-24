#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/RandomBotFacade.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "playerbot/strategy/Action.h"

namespace ai
{
    class RandomBotUpdateAction : public Action
    {
    public:
        RandomBotUpdateAction(PlayerbotAI* ai) : Action(ai, "random bot update")
        {}

        virtual bool Execute(Event& event) override
        {
            if (!sRandomBotFacade.IsRandomBot(bot))
                return false;

            if (bot->GetGroup() && ai->GetGroupMaster() && (PlayerbotAIStorage::Instance().GetAI(!ai->GetGroupMaster()) || PlayerbotAIStorage::Instance().GetAI(ai->GetGroupMaster())->IsRealPlayer()))
                return true;

            if (ai->HasPlayerNearby())
                return true;

            return sRandomBotFacade.ProcessBot(bot);
        }

        virtual bool isUseful() override
        {
            return AI_VALUE(bool, "random bot update");
        }
    };

}