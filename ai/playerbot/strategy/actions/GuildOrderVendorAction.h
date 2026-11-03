#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/values/GuildValues.h"

namespace ai
{
    // Buys the shortfall of the order's vendor reagent from a nearby vendor
    // with the bot's own gold. Runs when the "order vendor" trip arrives.
    class GuildOrderVendorAction : public Action
    {
    public:
        GuildOrderVendorAction(PlayerbotAI* ai) : Action(ai, "guild order vendor") {}

        bool Execute(Event& event) override;
        bool isUseful() override;
    };
}
