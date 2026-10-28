
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/values/GuildValues.h"

namespace ai
{
    class GuildOrderBuyAction : public Action
    {
    public:
        GuildOrderBuyAction(PlayerbotAI* ai) : Action(ai, "guild order buy") {}

        bool Execute(Event& event) override;
        bool isUseful() override;

    private:
        Unit* FindNearbyAuctioneer();
        uint32 CountMailboxItems(uint32 itemId);
    };
}
