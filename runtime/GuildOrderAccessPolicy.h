#pragma once

// Which guild members count as order bots for the bot-rank rule, and how the
// ORDER_ACCESS line is computed from the bot rank and the bank tabs.
// Header-only and free of core/database types on purpose: the rules are
// exercised standalone in tools/test_guild_order_access_policy.cpp. The
// command and store code feeds plain values it already queries.
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// A member counts as a bot for the rank rule when the module owns it as a
// bot and no real player owns the character: random-pool characters
// (claimed, hired or wandering), live module bot sessions, and durable
// ownership rows pointing at a module-owned account. Same-account
// characters of a real player and autonomous alt-bots never count: the
// rank rule must never move a player's own character.
inline bool IsGuildOrderBot(bool isModuleBot, bool ownedByPlayer)
{
    return isModuleBot && !ownedByPlayer;
}

// The rank newly joined bots get, and the startup sweep falls back to:
// the highest rank below the guild master. Rank 0 is the master; a guild
// with only a master row keeps its bots at the lowest rank.
inline uint32_t DefaultBotRank(uint32_t rankCount)
{
    if (rankCount < 2)
        return 0;
    return 1;
}

// Clamp a player-chosen bot rank to the guild's ranks. Rank 0 stays the
// master's own: choosing it would mix bots into the leader rank.
inline uint32_t ClampBotRank(uint32_t wanted, uint32_t rankCount)
{
    if (rankCount < 2)
        return DefaultBotRank(rankCount);
    if (wanted < 1)
        return 1;
    if (wanted >= rankCount)
        return rankCount - 1;
    return wanted;
}

// One bank tab's reachability for the ORDER_ACCESS line: tab numbers are
// 1-based on the wire. A tab whose minimum rank is at or below the bot rank
// (lower index outranks) is reachable from that rank.
inline void SplitBankTabs(uint32_t botRank, std::vector<uint32_t> const& tabMinRanks,
    std::vector<uint32_t>& tabsOk, std::vector<uint32_t>& tabsDenied)
{
    tabsOk.clear();
    tabsDenied.clear();
    uint32_t tab = 0;
    for (uint32_t minRank : tabMinRanks)
    {
        ++tab;
        if (botRank <= minRank)
            tabsOk.push_back(tab);
        else
            tabsDenied.push_back(tab);
    }
}

// Comma-separated 1-based tab list for ORDER_ACCESS, or "-" when empty.
inline std::string FormatTabList(std::vector<uint32_t> const& tabs)
{
    if (tabs.empty())
        return "-";
    std::string out;
    for (uint32_t tab : tabs)
    {
        if (!out.empty())
            out += ",";
        out += std::to_string(tab);
    }
    return out;
}

// Only the guild master rank may withdraw bank gold: the core money rule
// lets ranks 0 and 1 through.
inline bool BotRankMayWithdrawGold(uint32_t botRank)
{
    return botRank <= 1;
}

// The red header shows when the bot rank reaches no bank tab at all.
inline bool BotRankHasNoAccess(std::vector<uint32_t> const& tabsOk, bool goldOk)
{
    return tabsOk.empty() && !goldOk;
}

} // namespace GuildOrders
} // namespace TortoiseBots
