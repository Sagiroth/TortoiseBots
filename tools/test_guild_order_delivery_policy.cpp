// Standalone test for runtime/GuildOrderDeliveryPolicy.h (guild order delivery)
// and runtime/GuildOrderDeliveryQueue.h (world-thread handoff).
// No core, no DB: feeds ClassifyDelivery plain counters.
#include "../runtime/GuildOrderDeliveryPolicy.h"
#include "../runtime/GuildOrderDeliveryQueue.h"

#include <cstdio>
#include <vector>

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

    // Bank refusal needs consecutive empty passes.
    CHECK(!BankRefused(0));
    CHECK(!BankRefused(2));
    CHECK(BankRefused(3));
    CHECK(BankRefused(9));

    // Amount reached without mail: done; with any mailed stack: mailed.
    CHECK(ClassifyDelivery(10, 10, false, true) == DeliveryOutcome::Done);
    CHECK(ClassifyDelivery(12, 10, false, true) == DeliveryOutcome::Done);
    CHECK(ClassifyDelivery(10, 10, true, true) == DeliveryOutcome::Mailed);
    CHECK(ClassifyDelivery(12, 10, true, true) == DeliveryOutcome::Mailed);
    CHECK(ClassifyDelivery(0, 5, false, true) == DeliveryOutcome::BackToWork);
    CHECK(ClassifyDelivery(9, 10, true, true) == DeliveryOutcome::BackToWork);
    CHECK(ClassifyDelivery(9, 10, false, true) == DeliveryOutcome::BackToWork);

    // Nothing moved at all: stuck, never back to work.
    CHECK(ClassifyDelivery(0, 5, false, false) == DeliveryOutcome::Stuck);
    CHECK(ClassifyDelivery(9, 10, true, false) == DeliveryOutcome::Stuck);
    CHECK(ClassifyDelivery(0, 1, false, false) == DeliveryOutcome::Stuck);

    // The delivery ends when the quota is reached, when the bank refused
    // after retries, or when the bot holds no more of the order item.
    CHECK(DeliveryFinished(0, 5, 10, 10));
    CHECK(DeliveryFinished(0, 5, 12, 10));
    CHECK(!DeliveryFinished(0, 5, 9, 10));
    CHECK(!DeliveryFinished(2, 5, 9, 10));
    CHECK(DeliveryFinished(3, 5, 9, 10));
    CHECK(DeliveryFinished(3, 5, 0, 10));

    // Partial delivery over several passes: the last stack banked in an
    // earlier pass, so this pass moves nothing, yet the bank accepted
    // items during the delivery. Empty bags end the delivery at once and
    // the order goes back to work for the missing remainder.
    CHECK(DeliveryFinished(1, 0, 80, 100));
    CHECK(ClassifyDelivery(80, 100, false, true) == DeliveryOutcome::BackToWork);
    // Same shape when the pass moved the last carried stack itself.
    CHECK(DeliveryFinished(0, 0, 80, 100));

    // A bank that took nothing over the retries still mails or ends
    // stuck: held items mean more passes (or the mail leg), never work.
    CHECK(ClassifyDelivery(0, 10, false, false) == DeliveryOutcome::Stuck);
    CHECK(ClassifyDelivery(0, 10, true, false) == DeliveryOutcome::Stuck);

    // A timed-out delivery never goes back to work.
    CHECK(DeliveryTimeoutEnds(OrderReason::Timeout, DeliveryOutcome::BackToWork));
    CHECK(DeliveryTimeoutEnds(OrderReason::Timeout, DeliveryOutcome::Stuck));
    CHECK(!DeliveryTimeoutEnds(OrderReason::Timeout, DeliveryOutcome::Done));
    CHECK(!DeliveryTimeoutEnds(OrderReason::Timeout, DeliveryOutcome::Mailed));
    CHECK(!DeliveryTimeoutEnds(OrderReason::BagsFull, DeliveryOutcome::BackToWork));
    CHECK(!DeliveryTimeoutEnds(OrderReason::None, DeliveryOutcome::BackToWork));

    // The drain never touches more than the order still needs.
    CHECK(DeliveryNeeded(0, 10) == 10);
    CHECK(DeliveryNeeded(7, 10) == 3);
    CHECK(DeliveryNeeded(10, 10) == 0);
    CHECK(DeliveryNeeded(12, 10) == 0);

    // Whole stacks move only while the quota is unmet.
    CHECK(DeliveryMovesOn(0, 10));
    CHECK(DeliveryMovesOn(9, 10));
    CHECK(!DeliveryMovesOn(10, 10));
    CHECK(!DeliveryMovesOn(12, 10));

    // Smallest stack first, first wins ties, empty means none.
    {
        uint32_t counts[] = { 20, 5, 12 };
        CHECK(SmallestStackAt(counts, 3) == 1);
        uint32_t tied[] = { 5, 5 };
        CHECK(SmallestStackAt(tied, 2) == 0);
        CHECK(SmallestStackAt(nullptr, 0) == 0);
    }

    // One stack per mail.
    CHECK(DeliveryMailCount(0) == 0);
    CHECK(DeliveryMailCount(1) == 1);
    CHECK(DeliveryMailCount(7) == 7);

    // Three banker searches in a row that offer nothing flip the delivery
    // to the mail leg; anything offered resets the count.
    CHECK(kDeliveryNoBankerMails == 3);
    CHECK(!DeliveryMailsAfterNoBanker(0));
    CHECK(!DeliveryMailsAfterNoBanker(2));
    CHECK(DeliveryMailsAfterNoBanker(3));
    CHECK(DeliveryMailsAfterNoBanker(9));

    // One request per bot pending; the drain takes all in arrival order.
    DeliveryQueue queue;
    CHECK(queue.PendingCount() == 0);
    CHECK(queue.Enqueue(7, 100));
    CHECK(!queue.Enqueue(7, 200));
    CHECK(queue.Enqueue(8, 300));
    CHECK(queue.PendingCount() == 2);
    {
        std::vector<PendingDelivery> batch = queue.TakeAll();
        CHECK(batch.size() == 2);
        CHECK(batch[0].botGuidLow == 7 && batch[0].itemId == 100);
        CHECK(batch[1].botGuidLow == 8 && batch[1].itemId == 300);
    }
    CHECK(queue.PendingCount() == 0);
    CHECK(queue.TakeAll().empty());
    CHECK(queue.Enqueue(7, 200));
    queue.Clear();
    CHECK(queue.PendingCount() == 0);
    if (failures == 0)
        std::printf("test_guild_order_delivery_policy ok\n");
    return failures ? 1 : 0;
}
