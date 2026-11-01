// Standalone test for runtime/GuildOrderNeedPolicy.h: need-list recovery.
#include "../runtime/GuildOrderNeedPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // The slow lane waits a full interval between checks.
    CHECK(NeedLadderCheckDue(600000, 0));
    CHECK(NeedLadderCheckDue(kNeedLadderCheckMs, 0));
    CHECK(!NeedLadderCheckDue(kNeedLadderCheckMs - 1, 0));
    CHECK(!NeedLadderCheckDue(1000, 900));
    // Vendor first (the deployed-later rule), then bank, then AH.
    CHECK(NeedResumeLegFor(true, true, true) == NeedResumeLeg::Vendor);
    CHECK(NeedResumeLegFor(false, true, true) == NeedResumeLeg::Bank);
    CHECK(NeedResumeLegFor(false, false, true) == NeedResumeLeg::AuctionHouse);
    CHECK(NeedResumeLegFor(false, false, false) == NeedResumeLeg::None);

    if (failures == 0)
        std::printf("guild order need policy: all tests passed\n");
    return failures != 0;
}
