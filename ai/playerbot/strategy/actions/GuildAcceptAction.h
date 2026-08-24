#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"

namespace ai
{
    class GuildAcceptAction : public Action {
    public:
        GuildAcceptAction(PlayerbotAI* ai) : Action(ai, "guild accept") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };
}
