
#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "runtime/GuildOrderContract.h"

namespace ai
{
// The ladder move after the auction visit ends with nothing bought (no
// listing, outbid, or unaffordable): farm what is missing when sourcable,
// else a sub-order, else the need list. Shared by the buy action, the travel
// pick when the auctioneer search comes back empty, and the reach deadline.
// `reason` rides onto the next leg (NoAuctioneer for an unreachable leg).
void AdvanceGuildOrderAfterAuction(PlayerbotAI* ai, Player* bot,
    TortoiseBots::GuildOrders::OrderRecord const& order,
    TortoiseBots::GuildOrders::OrderReason reason = TortoiseBots::GuildOrders::OrderReason::None);

// A usable auctioneer near the bot, out to the service-trip arrival radius
// plus a margin: a trip counts as arrived at 10 yd while interaction needs
// 5 yd, so the buy action walks the last steps itself.
inline constexpr float kGuildOrderAuctioneerNearYd = 15.0f;
Unit* GuildOrderAuctioneerNear(PlayerbotAI* ai, Player* bot);

// An auctioneer the bot can actually interact with (the core's 5 yd + LOS
// check). The auction leg's reach clock clears on this, not on the wider
// arrival radius above.
Unit* GuildOrderAuctioneerInReach(PlayerbotAI* ai, Player* bot);
} // namespace ai

namespace ai
{
    class GuildOrderBuyAction : public MovementAction
    {
    public:
        GuildOrderBuyAction(PlayerbotAI* ai) : MovementAction(ai, "guild order buy") {}

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
