// Standalone test for runtime/GuildOrderKeepPolicy.h (keep-in-stock watch
// timing, the re-arm quota, and the contract tokens for stocked/keep).
// No core, no DB: feeds the policy plain counters.
#include "../runtime/GuildOrderKeepPolicy.h"
#include "../runtime/GuildOrderContract.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // The bank look runs at most every 10 minutes per stocked order.
    CHECK(kKeepBankCheckMs == 10 * 60 * 1000);
    CHECK(!KeepCheckDue(1000, 1000));
    CHECK(!KeepCheckDue(1000 + kKeepBankCheckMs - 1, 1000));
    CHECK(KeepCheckDue(1000 + kKeepBankCheckMs, 1000));
    CHECK(KeepCheckDue(1000 + kKeepBankCheckMs + 1, 1000));
    // A fresh stocked row (never looked) checks on the first pass.
    CHECK(KeepCheckDue(kKeepBankCheckMs, 0));

    // The missing stock below the target is the next round quota.
    CHECK(KeepMissing(20, 0) == 20);
    CHECK(KeepMissing(20, 7) == 13);
    CHECK(KeepMissing(20, 19) == 1);
    CHECK(KeepMissing(20, 20) == 0);
    CHECK(KeepMissing(20, 25) == 0);
    // Without a stock target there is nothing to keep.
    CHECK(KeepMissing(0, 0) == 0);

    // A delivered keep round watches the bank; anything else ends.
    CHECK(KeepWatchesBank(true, 20));
    CHECK(!KeepWatchesBank(false, 20));
    CHECK(!KeepWatchesBank(true, 0));

    // The stocked state token survives the row parser round trip.
    OrderState back = OrderState::Working;
    CHECK(std::string(StateToken(OrderState::Stocked)) == "stocked");
    CHECK(ParseState("stocked", back) && back == OrderState::Stocked);
    // Stocked counts as finished: nothing runs while the bank is watched.
    CHECK(IsFinished(OrderState::Stocked));
    CHECK(std::string(KeepToken(true)) == "keep");
    CHECK(std::string(KeepToken(false)) == "-");

    if (failures == 0)
        std::printf("test_guild_order_keep_policy ok\n");
    return failures ? 1 : 0;
}
