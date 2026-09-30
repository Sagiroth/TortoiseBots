#pragma once
#include "playerbot/PlayerbotAI.h"

#include <map>

#include "playerbot/strategy/Action.h"
#include "QuestAction.h"
#include "UseItemAction.h"

namespace ai
{
    class GreetAction : public Action
    {
    public:
        GreetAction(PlayerbotAI* ai);
        virtual bool Execute(Event& event) override;

    private:
        // Last greeting per player: a player who keeps stepping in and out of
        // sight is greeted once per AiPlayerbot.GreetCooldown, not on every pass.
        std::map<ObjectGuid, time_t> greetTimes;
    };
}
