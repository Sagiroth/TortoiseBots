#pragma once
#include "playerbot/PlayerbotAI.h"
#include "QueryItemUsageAction.h"

namespace ai
{
    class TradeStatusAction : public QueryItemUsageAction
    {
    public:
        TradeStatusAction(PlayerbotAI* ai) : QueryItemUsageAction(ai, "accept trade") {}
        virtual bool Execute(Event& event) override;

    private:
        // Issue #469: pool-bot trade gate (donor EnableRandomBotTrading
        // parity). True unless the bot is a masterless non-hired pool bot
        // and the trader is a stranger under the configured PoolBotTradeMode.
        bool PoolBotMayTradeWith(Player* trader);
        void BeginTrade();
        void AutoShareConjured(Player* trader);
        bool CheckTrade();
        int32 CalculateCost(Player *player, bool sell);
    };
}
