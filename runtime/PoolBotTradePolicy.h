#pragma once

// Pure policy for pool-bot trade safety (issue #469), ported from
// mod-playerbots `AiPlayerbot.EnableRandomBotTrading` (0-3) and the
// `TradeActionExcludedPrefixes` guard.
//
// Header-only and free of core/database types on purpose: the pool-bot
// identity and trade-channel checks that must not regress live here where
// tools/test_pool_bot_trade_policy.cpp can exercise them standalone.
// Callers pass plain booleans (pool identity, master/group/hire state,
// mention state) so the policy never depends on Player/AI pointers.

#include <cstdint>
#include <string>

namespace TortoiseBots
{

// Pool-bot trade modes, matching the donor numbering:
//   Off     = pool bot trades with nobody (safest, default)
//   Trusted = only its master, group members, or its hire master
//   BuyOnly = trusted, plus buying from anyone (bot never gives items)
//   SellOnly= trusted, plus selling to anyone (bot never takes items)
//   On      = unrestricted (donor default; opt-in here)
enum class PoolBotTradeMode : int32_t
{
    Off = 0,
    Trusted = 1,
    BuyOnly = 2,
    SellOnly = 3,
    On = 4,
};

// Clamp raw config to a known mode. Out-of-range fails closed to Off,
// never open: a typo must not silently enable stranger trading.
inline PoolBotTradeMode ParsePoolBotTradeMode(int32_t raw)
{
    switch (raw)
    {
        case 0: return PoolBotTradeMode::Off;
        case 1: return PoolBotTradeMode::Trusted;
        case 2: return PoolBotTradeMode::BuyOnly;
        case 3: return PoolBotTradeMode::SellOnly;
        case 4: return PoolBotTradeMode::On;
        default: return PoolBotTradeMode::Off;
    }
}

// Inbound trade gate: may this pool bot keep a trade window open with
// the trader? Owned/hired bots (not pool bots) never reach this gate:
// the caller only invokes it for pool bots, so master/group/hire checks
// below describe stranger trust, not ownership.
inline bool PoolBotTradeAllowed(PoolBotTradeMode mode, bool isPoolBot,
    bool traderIsMaster, bool traderInGroup, bool traderIsHireMaster)
{
    if (!isPoolBot)
        return true;
    if (traderIsMaster || traderInGroup || traderIsHireMaster)
        return true;
    switch (mode)
    {
        case PoolBotTradeMode::Trusted:
        case PoolBotTradeMode::Off:
            return false;
        case PoolBotTradeMode::BuyOnly:
        case PoolBotTradeMode::SellOnly:
        case PoolBotTradeMode::On:
            return true;
    }
    return false;
}

// Settle gate: the window is open (or trusted); may this side complete?
// botGives = the bot's trade slots hold items/money, playerGives = the
// trader's slots hold items/money. Enchant-style handoffs (no items either
// way) always complete so a blocked mode cannot strand an open window.
inline bool PoolBotTradeSettleAllowed(PoolBotTradeMode mode, bool isPoolBot,
    bool botGives, bool playerGives)
{
    if (!isPoolBot)
        return true;
    if (!botGives && !playerGives)
        return true;
    switch (mode)
    {
        case PoolBotTradeMode::Off:
            return false;
        case PoolBotTradeMode::Trusted:
            return true;
        case PoolBotTradeMode::BuyOnly:
            return !botGives && playerGives;
        case PoolBotTradeMode::SellOnly:
            return botGives && !playerGives;
        case PoolBotTradeMode::On:
            return true;
    }
    return false;
}

// Trade-channel guard: a pool bot ignores Trade-channel chatter unless the
// speaker addresses it directly (message mentions the bot's name). Whispers,
// say, party and yell keep the old behaviour.
inline bool PoolBotTradeChatAllowed(bool isPoolBot, bool isTradeChannel, bool isMentioned)
{
    if (!isPoolBot)
        return true;
    if (!isTradeChannel)
        return true;
    return isMentioned;
}

// Excluded-prefix guard (donor TradeAction parity): addon chatter
// (Questie/DBM/RPLL-style prefixes) never drives the trade action, even
// when the bot is mentioned. Empty entries never match.
inline bool TradeTextExcluded(std::string const& text, std::string const& prefix)
{
    if (prefix.empty() || text.size() < prefix.size())
        return false;
    return text.compare(0, prefix.size(), prefix) == 0;
}

} // namespace TortoiseBots
