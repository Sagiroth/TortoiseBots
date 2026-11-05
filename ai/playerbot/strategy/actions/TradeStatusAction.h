#pragma once
#include "playerbot/PlayerbotAI.h"
#include "QueryItemUsageAction.h"

namespace ai
{
    class TradeStatusAction : public QueryItemUsageAction
    {
    public:
        TradeStatusAction(PlayerbotAI* ai) : QueryItemUsageAction(ai, "accept trade") {}
        // The enchant order's trade step. Also run from the order action each
        // tick: placing an item only sends a window update, no trade status.
        bool HandleEnchantOrderTrade(Event& event, Player* trader);
        virtual bool Execute(Event& event) override;

    private:
        // Issue #469: pool-bot trade gate (donor EnableRandomBotTrading
        // parity). True unless the bot is a masterless non-hired pool bot
        // and the trader is a stranger under the configured PoolBotTradeMode.
        bool PoolBotMayTradeWith(Player* trader);
        void BeginTrade();
        void AutoShareConjured(Player* trader);
        bool CheckTrade();
        // Guild enchant order (Coming/Enchanting row): per-update window
        // checks, the replace answer, the try budget and the done verify.
        // True when the row owned the packet (the caller returns at once).
        int32 CalculateCost(Player *player, bool sell);
    };
}
