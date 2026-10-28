#pragma once

#include "runtime/GuildOrderContract.h"

class Player;

namespace ai
{

// The one source finder for guild orders: which sources this bot can use for
// the item, its verdict and the farm zones ranked best first. Search, give and
// travel all ask it, so they never disagree. World thread only.
TortoiseBots::GuildOrders::SourcePlan PlanGuildOrderSources(Player* bot, uint32_t itemId,
                                                           TortoiseBots::GuildOrders::OrderType type);

} // namespace ai
