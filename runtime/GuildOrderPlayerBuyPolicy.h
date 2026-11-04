#pragma once

#include "GuildOrderContract.h"

#include <cstdint>

// Whether a player may give a Buy order directly: the addon window and the
// chat command share this gate, so both refuse together. Header-only and
// free of core types so the rule stays unit tested on its own
// (tools/test_guild_order_player_buy_policy.cpp). The supply chain never
// asks it: sub-orders, the auction-house step inside farm and craft orders,
// and whose gold is spent stay exactly as today.
namespace TortoiseBots
{
namespace GuildOrders
{

// A Buy order a player gives directly is refused while the switch is off.
// Farm and craft orders always pass; the chain's own buying never reaches here.
inline bool PlayerBuyOrderRefused(OrderType type, bool playerBuyEnabled)
{
    return type == OrderType::Buy && !playerBuyEnabled;
}

// Wire code for the refusal: a stable token the addon can read, outside the
// reason set so old addons show the message instead of a state.
inline char const* PlayerBuyRefusalCode()
{
    return "buy-disabled";
}

// Plain refusal line for `.bot order <bot> buy` give.
inline char const* PlayerBuyRefusal()
{
    return "Buy orders are turned off on this server. Give a Farm or Craft order instead.";
}

// Capability token sent while player Buy orders are refused. Absent means
// allowed, so older servers without the token keep today's behaviour.
inline char const* PlayerBuyCapToken()
{
    return "guild-order-nobuy";
}

} // namespace GuildOrders
} // namespace TortoiseBots
