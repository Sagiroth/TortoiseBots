#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../ai/playerbot/policy/gear/SellRefusalPolicy.h"

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x << "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    // Nothing grey in the bags wins over the vendor check.
    CHECK(SellRefusalReason(0, true) == "nothing grey to sell");
    CHECK(SellRefusalReason(0, false) == "nothing grey to sell");

    // Grey items but no vendor in reach.
    CHECK(SellRefusalReason(3, false) == "no vendor in reach");

    // Grey items and a vendor, yet nothing sold: the item is one the bot will not sell here.
    CHECK(SellRefusalReason(3, true) == "no grey item the bot will sell here");

    // No refusals recorded still gives a complete sentence.
    CHECK(FormatSellRefusal({}) == "No bot sold anything.");

    CHECK(FormatSellRefusal({ "nothing grey to sell" }) ==
        "No bot sold anything: nothing grey to sell.");

    // Repeated reasons are listed once, in first-seen order.
    CHECK(FormatSellRefusal({ "nothing grey to sell", "no vendor in reach", "nothing grey to sell" }) ==
        "No bot sold anything: nothing grey to sell; no vendor in reach.");
    CHECK(FormatSellRefusal({ "no vendor in reach", "nothing grey to sell" }) ==
        "No bot sold anything: no vendor in reach; nothing grey to sell.");

    std::cout << "sell refusal policy: OK\n";
    return 0;
}
