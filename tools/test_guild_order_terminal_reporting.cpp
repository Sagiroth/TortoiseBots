// Standalone test for the guild order reporting seam
// (runtime/GuildOrderGiveUpPolicy.h): a row reports the deposited count as
// have on every pass, and only a successful hand-off clears the carry. A row
// that ends cant with the stack still in the bags keeps the last scanned
// figure, so the final line, the stored row and both list replies agree.
#include "../runtime/GuildOrderContract.h"
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
    uint32_t reportedCarry = ReportedCarryThisPass(OrderState::Working, true, true, 4, 1, 0);
    CHECK(reportedCarry == 5);
    reportedHave = ReportedHaveThisPass(false, false, 0, 0, delivered, reportedHave);
    CHECK(reportedHave == 0);

    // Pass 2: the delivery drain banks the bags and the row goes done. A
    // finished row never scans, so this pass sees no counts and the row
    // still holds the pre-delivery carry.
    delivered = 4;
    reportedHave = ReportedHaveThisPass(false, false, 0, 0, delivered, reportedHave);
    CHECK(reportedHave == 4);
    bool const banked = HandOffSucceeded(OrderState::Done);
    CHECK(banked);
    reportedCarry = ReportedCarryThisPass(OrderState::Done, false, false, 0, 0, reportedCarry);
    CHECK(reportedCarry == 0);
    // The final line and the row write take exactly these figures.
    CHECK(ReportedHave(delivered, reportedHave) == 4);
    CHECK(ListedCarry(true, false, 0, reportedCarry) == 0);
    CHECK(ListedCarry(true, true, 0, reportedCarry) == 0);

    // Mailing the stack is the other successful hand-off: the items left the
    // bags, so nothing is carried for the row any more.
    CHECK(HandOffSucceeded(OrderState::Mailed));
    CHECK(ReportedCarryThisPass(OrderState::Mailed, false, true, 0, 0, 6) == 0);

    // The bank refused and there is no mail receiver: the row ends cant with
    // the stack still in its bags. That is not a hand-off, so the last
    // scanned carry stands and the list shows it online and offline alike.
    CHECK(IsTerminal(OrderState::Cant));
    CHECK(!HandOffSucceeded(OrderState::Cant));
    CHECK(!HandOffSucceeded(OrderState::NoSkill));
    uint32_t cantCarry = ReportedCarryThisPass(OrderState::Cant, false, false, 0, 0, 8);
    CHECK(cantCarry == 8);
    CHECK(ListedCarry(true, true, 0, cantCarry) == 8);
    CHECK(ListedCarry(true, false, 0, cantCarry) == 8);

    // A live row keeps its scanned carry, an off-cadence pass holds it, and
    // the list reads the live bags while the row runs.
    CHECK(ReportedCarryThisPass(OrderState::Working, true, true, 6, 2, 8) == 8);
    CHECK(ReportedCarryThisPass(OrderState::Working, false, true, 0, 0, 8) == 8);
    CHECK(!IsTerminal(OrderState::Working));
    CHECK(ListedCarry(false, true, 9, 8) == 9);
    CHECK(ListedCarry(false, false, 0, 8) == 8);

    // A stocked row is finished but not over: it watches the bank for the
    // next round, so it is no terminal row and no hand-off by itself.
    CHECK(!IsTerminal(OrderState::Stocked));
    CHECK(!HandOffSucceeded(OrderState::Stocked));

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
