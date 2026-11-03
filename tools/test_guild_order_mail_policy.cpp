// Standalone test for runtime/GuildOrderMailPolicy.h: mailbox collection
// for guild orders.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_mail_policy.cpp -o /tmp/test_guild_order_mail_policy
//   /tmp/test_guild_order_mail_policy
#include "../runtime/GuildOrderMailPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // No mail waiting: farm, buy or deliver as usual.
    CHECK(!CollectOrderMailFirst(0, 0, 0, 3, false, false));
    CHECK(!CollectOrderMailFirst(2, 0, 0, 3, true, true));

    // Mail waiting and bags short: walk to the mailbox first, whatever the
    // order type (bought items arrive by mail; anyone can mail a bot).
    CHECK(CollectOrderMailFirst(0, 2, 2, 5, false, false));
    CHECK(CollectOrderMailFirst(2, 0, 2, 3, false, false));
    CHECK(CollectOrderMailFirst(0, 0, 1, 1, true, false));

    // Bags already cover the amount: the mailbox leg is done.
    CHECK(!CollectOrderMailFirst(3, 0, 2, 3, false, false));
    CHECK(!CollectOrderMailFirst(1, 2, 2, 3, false, false));

    // An open auction visit buys first while an auctioneer is in reach.
    CHECK(!CollectOrderMailFirst(0, 0, 2, 3, true, true));
    // Away from the auctioneer the mailbox wins even mid-visit.
    CHECK(CollectOrderMailFirst(0, 0, 2, 3, true, false));
    // A farm rest (or farm/craft order) never holds for the auctioneer.
    CHECK(CollectOrderMailFirst(0, 0, 2, 3, false, true));

    if (failures == 0)
        std::printf("test_guild_order_mail_policy ok\n");
    return failures ? 1 : 0;
}
