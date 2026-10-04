// Standalone regression test for issue #469: pool-bot trade safety policy.
// Exercises the pure decision rules that must not regress — mode parsing
// (fail closed), the inbound stranger gate, the buy/sell settle gate, the
// trade-channel mention guard, and the excluded-prefix check — without a
// database or a running core.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pool_bot_trade_policy.cpp -o /tmp/test_pool_bot_trade
//   /tmp/test_pool_bot_trade

#include "../runtime/PoolBotTradePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::PoolBotTradeAllowed;
using TortoiseBots::PoolBotTradeChatAllowed;
using TortoiseBots::PoolBotTradeMode;
using TortoiseBots::PoolBotTradeSettleAllowed;
using TortoiseBots::ParsePoolBotTradeMode;
using TortoiseBots::TradeTextExcluded;

int main()
{
    std::cout << "Starting TortoiseBots pool-bot trade policy tests...\n";

    // 1. Mode parsing: donor 0-3 plus our open 4; garbage fails closed.
    CHECK(ParsePoolBotTradeMode(0) == PoolBotTradeMode::Off);
    CHECK(ParsePoolBotTradeMode(1) == PoolBotTradeMode::Trusted);
    CHECK(ParsePoolBotTradeMode(2) == PoolBotTradeMode::BuyOnly);
    CHECK(ParsePoolBotTradeMode(3) == PoolBotTradeMode::SellOnly);
    CHECK(ParsePoolBotTradeMode(4) == PoolBotTradeMode::On);
    CHECK(ParsePoolBotTradeMode(-1) == PoolBotTradeMode::Off);
    CHECK(ParsePoolBotTradeMode(99) == PoolBotTradeMode::Off);
    std::cout << "  [PASS] mode parsing fails closed\n";

    // 2. Non-pool (owned/hired) bots never reach the gate: always allowed.
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::Off, false, false, false, false));
    CHECK(PoolBotTradeSettleAllowed(PoolBotTradeMode::Off, false, true, true));
    CHECK(PoolBotTradeChatAllowed(false, true, false));
    std::cout << "  [PASS] owned/hired bots bypass the gates\n";

    // 3. Off/Trusted: strangers refused, master/group/hire-master pass.
    CHECK(!PoolBotTradeAllowed(PoolBotTradeMode::Off, true, false, false, false));
    CHECK(!PoolBotTradeAllowed(PoolBotTradeMode::Trusted, true, false, false, false));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::Off, true, true, false, false));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::Off, true, false, true, false));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::Off, true, false, false, true));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::Trusted, true, true, false, false));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::Trusted, true, false, true, false));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::Trusted, true, false, false, true));
    std::cout << "  [PASS] safe modes keep strangers out, masters in\n";

    // 4. Open modes let strangers open a window (settle gate restricts).
    // Review regression pin: TradeStatusAction reconciles this with the
    // legacy master/group shouldTrade block, so modes 2/3/4 and ungrouped
    // owners/hire-masters actually reach CheckTrade instead of being
    // cancelled by the legacy block.
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::BuyOnly, true, false, false, false));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::SellOnly, true, false, false, false));
    CHECK(PoolBotTradeAllowed(PoolBotTradeMode::On, true, false, false, false));
    std::cout << "  [PASS] open modes admit strangers to the window\n";

    // 5. Settle: Off completes nothing with value; Trusted completes all;
    // BuyOnly takes without giving; SellOnly gives without taking; On all.
    CHECK(!PoolBotTradeSettleAllowed(PoolBotTradeMode::Off, true, true, false));
    CHECK(!PoolBotTradeSettleAllowed(PoolBotTradeMode::Off, true, false, true));
    CHECK(PoolBotTradeSettleAllowed(PoolBotTradeMode::Off, true, false, false));
    CHECK(PoolBotTradeSettleAllowed(PoolBotTradeMode::Trusted, true, true, false));
    CHECK(PoolBotTradeSettleAllowed(PoolBotTradeMode::Trusted, true, false, true));
    CHECK(PoolBotTradeSettleAllowed(PoolBotTradeMode::BuyOnly, true, false, true));
    CHECK(!PoolBotTradeSettleAllowed(PoolBotTradeMode::BuyOnly, true, true, false));
    CHECK(!PoolBotTradeSettleAllowed(PoolBotTradeMode::BuyOnly, true, true, true));
    CHECK(PoolBotTradeSettleAllowed(PoolBotTradeMode::SellOnly, true, true, false));
    CHECK(!PoolBotTradeSettleAllowed(PoolBotTradeMode::SellOnly, true, false, true));
    CHECK(!PoolBotTradeSettleAllowed(PoolBotTradeMode::SellOnly, true, true, true));
    CHECK(PoolBotTradeSettleAllowed(PoolBotTradeMode::On, true, true, true));
    std::cout << "  [PASS] settle gate follows the mode\n";

    // 6. Trade channel: pool bots need a direct mention; other channels pass.
    // Review regression pin: PlayerbotAI applies this ONLY to the Trade
    // channel (SRC_TRADE via channel id) — General/LFG/Defense/World keep
    // prior behaviour and never consult this gate.
    CHECK(!PoolBotTradeChatAllowed(true, true, false));
    CHECK(PoolBotTradeChatAllowed(true, true, true));
    CHECK(PoolBotTradeChatAllowed(true, false, false));
    std::cout << "  [PASS] trade-channel chatter needs a mention\n";

    // 7. Excluded prefixes: addon chatter never drives the trade action.
    CHECK(TradeTextExcluded("Questie: need [item]", "Questie"));
    CHECK(!TradeTextExcluded("WTS [item]", "Questie"));
    CHECK(!TradeTextExcluded("hi", "Questie: long prefix"));
    CHECK(!TradeTextExcluded("anything", ""));
    std::cout << "  [PASS] excluded prefixes match at the start only\n";

    std::cout << "pool-bot trade policy: OK\n";
    return 0;
}
