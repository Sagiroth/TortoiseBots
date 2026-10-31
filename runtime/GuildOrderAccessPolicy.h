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
// bot: claimed into a guild or living on a random-pool account. Autonomous
// alt-bots and any other character on a real player's account never count:
// the rank rule must never move a player's own character. Online and
// offline members use this same durable rule.
inline bool IsGuildOrderBot(bool claimed, bool onPoolAccount, bool autonomousAlt)
{
    if (autonomousAlt)
        return false;
    return claimed || onPoolAccount;
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

// The guild master is never moved: in bot-led guilds the leader is a bot at
// rank 0, and moving it would leave the guild without a leader.
inline bool BotRankMayMoveMember(uint32_t currentRank)
{
    return currentRank != 0;
}

// The bot rank applies only in guilds with at least one real player member:
// all-bot pool guilds keep their ranks untouched (nobody gives orders there,
// and the bots' own guild management runs on those ranks). A disabled
// feature (rank 0) applies nowhere.
inline bool BotRankAppliesInGuild(bool hasRealPlayerMember, uint32_t botRank)
{
    return hasRealPlayerMember && botRank != 0;
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
