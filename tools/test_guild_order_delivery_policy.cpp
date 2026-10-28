// Standalone test for runtime/GuildOrderDeliveryPolicy.h (guild order delivery).
// No core, no DB: feeds ClassifyDelivery plain counters.
#include "../runtime/GuildOrderDeliveryPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    CHECK(kDeliveryFirstTab == 1);
    CHECK(kDeliveryTabCount == 5);
    CHECK(kDeliveryBankRetries == 3);

    // Tabs are tried in order, 1 through 5.
    CHECK(DeliveryTabAt(0) == 1);
    CHECK(DeliveryTabAt(4) == 5);
    for (size_t i = 1; i < kDeliveryTabCount; ++i)
        CHECK(DeliveryTabAt(i) == DeliveryTabAt(i - 1) + 1);

    // Amount reached without mail: done; with any mailed stack: mailed.
    CHECK(ClassifyDelivery(10, 10, false) == DeliveryOutcome::Done);
    CHECK(ClassifyDelivery(12, 10, false) == DeliveryOutcome::Done);
    CHECK(ClassifyDelivery(10, 10, true) == DeliveryOutcome::Mailed);
    CHECK(ClassifyDelivery(12, 10, true) == DeliveryOutcome::Mailed);
    CHECK(ClassifyDelivery(0, 5, false) == DeliveryOutcome::BackToWork);
    CHECK(ClassifyDelivery(9, 10, true) == DeliveryOutcome::BackToWork);

    // One stack per mail.
    CHECK(DeliveryMailCount(0) == 0);
    CHECK(DeliveryMailCount(1) == 1);
    CHECK(DeliveryMailCount(7) == 7);

    if (failures == 0)
        std::printf("test_guild_order_delivery_policy ok\n");
    return failures ? 1 : 0;
}
