
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/values/GuildValues.h"

namespace TortoiseBots::GuildOrders { struct OrderRecord; }

namespace ai
{
// The ladder move after the auction visit ends with nothing bought (no
// listing, outbid, or unaffordable): farm what is missing when sourcable,
// else a sub-order, else the need list. Shared by the buy action and the
// travel pick when the auctioneer search itself comes back empty.
void AdvanceGuildOrderAfterAuction(PlayerbotAI* ai, Player* bot,
    TortoiseBots::GuildOrders::OrderRecord const& order);
} // namespace ai

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
    };

    // Takes order items waiting in the mailbox when one is in reach. The rpg
    // mail errand skips mail while the bot wants to sell, which is most of
    // the time on a long order, so bought items never reached the bags.
    class GuildOrderTakeMailAction : public Action
    {
    public:
        GuildOrderTakeMailAction(PlayerbotAI* ai) : Action(ai, "guild order take mail") {}

        bool Execute(Event& event) override;
        bool isUseful() override;
    };
}
