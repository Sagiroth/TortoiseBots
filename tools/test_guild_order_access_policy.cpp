// Standalone test for runtime/GuildOrderAccessPolicy.h: bot-rank rule,
// rank clamping, tab split and the gold rule behind TBM:ORDER_ACCESS.
#include "../runtime/GuildOrderAccessPolicy.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Durable bot rule, online and offline alike: claimed or on a pool
    // account counts; autonomous alt-bots never do, even when claimed.
    CHECK(IsGuildOrderBot(true, false, false));
    CHECK(IsGuildOrderBot(false, true, false));
    CHECK(!IsGuildOrderBot(false, false, false));
    CHECK(!IsGuildOrderBot(true, true, true));
    CHECK(!IsGuildOrderBot(false, false, true));

    // Default is the rank below the master; a master-only guild keeps rank 0.
    CHECK(DefaultBotRank(5) == 1);
    CHECK(DefaultBotRank(2) == 1);
    CHECK(DefaultBotRank(1) == 0);
    CHECK(DefaultBotRank(0) == 0);

    // The chosen rank is clamped into the guild's ranks, never the master.
    CHECK(ClampBotRank(1, 5) == 1);
    CHECK(ClampBotRank(3, 5) == 3);
    CHECK(ClampBotRank(0, 5) == 1);
    CHECK(ClampBotRank(4, 5) == 4);
    CHECK(ClampBotRank(9, 5) == 4);
    CHECK(ClampBotRank(0, 1) == 0);

    // The guild master is never moved, even when it is a bot.
    CHECK(!BotRankMayMoveMember(0));
    CHECK(BotRankMayMoveMember(1));
    CHECK(BotRankMayMoveMember(4));

    // The bot rank applies only in guilds with a real player member, and
    // never when the feature is off.
    CHECK(BotRankAppliesInGuild(true, 1));
    CHECK(!BotRankAppliesInGuild(false, 1));
    CHECK(!BotRankAppliesInGuild(true, 0));
    CHECK(!BotRankAppliesInGuild(false, 0));

    // Tabs at or above the bot rank (lower index wins) are reachable.
    std::vector<uint32_t> tabsOk, tabsDenied;
    SplitBankTabs(1, {1, 1, 4, 0, 2}, tabsOk, tabsDenied);
    CHECK(tabsOk.size() == 4 && tabsOk[0] == 1 && tabsOk[1] == 2 && tabsOk[2] == 3 && tabsOk[3] == 5);
    CHECK(tabsDenied.size() == 1 && tabsDenied[0] == 4);
    SplitBankTabs(4, {1, 4}, tabsOk, tabsDenied);
    CHECK(tabsOk.size() == 1 && tabsOk[0] == 2);
    CHECK(tabsDenied.size() == 1 && tabsDenied[0] == 1);

    CHECK(FormatTabList(tabsOk) == "2");
    tabsDenied.clear();
    CHECK(FormatTabList(tabsDenied) == "-");

    // Only ranks 0 and 1 may withdraw bank gold.
    CHECK(BotRankMayWithdrawGold(0));
    CHECK(BotRankMayWithdrawGold(1));
    CHECK(!BotRankMayWithdrawGold(2));

    // The red header shows when neither tabs nor gold are reachable.
    CHECK(BotRankHasNoAccess({}, false));
    CHECK(!BotRankHasNoAccess({1}, false));
    CHECK(!BotRankHasNoAccess({}, true));

    if (failures == 0)
        std::printf("test_guild_order_access_policy ok\n");
    return failures ? 1 : 0;
}
