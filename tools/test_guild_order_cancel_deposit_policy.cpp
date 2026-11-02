// Standalone test for runtime/GuildOrderCancelDepositPolicy.h (a cancelled
// sub-order with its finished items in the bags still deposits them).
// No core, no DB: feeds the policy plain row facts.
#include "../runtime/GuildOrderCancelDepositPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Live case: a working child holding its full quota deposits on cancel.
    CHECK(CancelledChildDeposits(OrderState::Working, OrderReason::None, 0, 4, 0, 4));
    // Already delivering stays delivering.
    CHECK(CancelledChildDeposits(OrderState::Delivering, OrderReason::None, 0, 4, 0, 4));
    // Short of the quota: nothing to deposit, the row just ends.
    CHECK(!CancelledChildDeposits(OrderState::Working, OrderReason::None, 0, 3, 0, 4));
    // Empty bags: nothing to deposit.
    CHECK(!CancelledChildDeposits(OrderState::Working, OrderReason::None, 0, 0, 0, 4));
    // Mailbox-only stock still needs the pickup leg the cancelled row no
    // longer runs, so it does not count for the deposit.
    CHECK(!CancelledChildDeposits(OrderState::Working, OrderReason::None, 0, 0, 4, 4));
    // Mailbox items on top of a full bag quota do not block the deposit.
    CHECK(CancelledChildDeposits(OrderState::Working, OrderReason::None, 0, 4, 2, 4));
    // Partially delivered plus the rest in bags reaches the quota.
    CHECK(CancelledChildDeposits(OrderState::Working, OrderReason::None, 1, 3, 0, 4));
    // Nothing to top up: the quota is already delivered.
    CHECK(!CancelledChildDeposits(OrderState::Working, OrderReason::None, 4, 1, 0, 4));
    // Finished rows are not working children: they never reach the branch.
    CHECK(!CancelledChildDeposits(OrderState::Cant, OrderReason::NoProgress, 0, 4, 0, 4));
    CHECK(!CancelledChildDeposits(OrderState::Done, OrderReason::None, 4, 0, 0, 4));
    // A grouped child waits on the group, it never handled items.
    CHECK(!CancelledChildDeposits(OrderState::Working, OrderReason::Grouped, 0, 4, 0, 4));
    // Zero quota or non-order rows deposit nothing.
    CHECK(!CancelledChildDeposits(OrderState::Working, OrderReason::None, 0, 4, 0, 0));
    CHECK(!CancelledChildDeposits(OrderState::Waiting, OrderReason::NeedItems, 0, 4, 0, 4));

    if (failures == 0)
        std::printf("test_guild_order_cancel_deposit_policy ok\n");
    return failures ? 1 : 0;
}
