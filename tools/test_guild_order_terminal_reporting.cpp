// Standalone test for the guild order reporting seam
// (runtime/GuildOrderGiveUpPolicy.h): a row reports the deposited count as
// have on every pass, and a finished row reports zero carry even when no
// scan ran after the deposit, so the final line and the stored row agree
// with the offline list.
#include "../runtime/GuildOrderGiveUpPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Pass 1: a scan finds four items in the bags and one in the mail; the
    // row has delivered nothing yet.
    uint32_t delivered = 0;
    uint32_t reportedHave = 0;
    uint32_t reportedCarry = ReportedCarryThisPass(false, true, true, 4, 1, 0);
    CHECK(reportedCarry == 5);
    reportedHave = ReportedHaveThisPass(false, false, 0, 0, delivered, reportedHave);
    CHECK(reportedHave == 0);

    // Pass 2: the delivery drain banks the bags and the row goes done. A
    // finished row never scans, so this pass sees no counts and the row
    // still holds the pre-delivery carry.
    delivered = 4;
    reportedHave = ReportedHaveThisPass(false, false, 0, 0, delivered, reportedHave);
    CHECK(reportedHave == 4);
    uint32_t finishedCarry = ReportedCarryThisPass(true, false, false, 0, 0, reportedCarry);
    CHECK(finishedCarry == 0);
    // The final line and the row write take exactly these figures.
    reportedCarry = finishedCarry;
    CHECK(ReportedHave(delivered, reportedHave) == 4);
    CHECK(ListedCarry(false, 0, reportedCarry) == 0);

    // A cant row with the stack still in the bags goes the same way: the
    // order ended, so nothing is pending for it.
    CHECK(ReportedCarryThisPass(true, false, true, 6, 2, 8) == 0);
    CHECK(ReportedCarryThisPass(true, true, true, 6, 2, 0) == 0);

    // A live row keeps the scanned carry, and an off-cadence pass holds it.
    CHECK(ReportedCarryThisPass(false, true, true, 6, 2, 8) == 8);
    CHECK(ReportedCarryThisPass(false, false, true, 0, 0, 8) == 8);

    // The stocked row reports the bank stock a fresh look saw and holds the
    // last figure when this pass read no bank.
    CHECK(ReportedHaveThisPass(true, true, 9, 5, 0, 0) == 5);
    CHECK(ReportedHaveThisPass(true, true, 3, 5, 0, 0) == 3);
    CHECK(ReportedHaveThisPass(true, false, 0, 5, 0, 2) == 2);

    // Have never drops below what already reached the guild.
    CHECK(ReportedHaveThisPass(false, false, 0, 0, 4, 7) == 7);

    if (failures == 0)
        std::printf("test_guild_order_terminal_reporting ok\n");
    return failures ? 1 : 0;
}
