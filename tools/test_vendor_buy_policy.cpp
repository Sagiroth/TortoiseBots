#include "../ai/playerbot/strategy/values/VendorBuyPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::VendorBuyRanksFirst;
using ai::VendorBuyUsesGearBudget;
using ai::VENDOR_BUY_USAGE_BAD_EQUIP;
using ai::VENDOR_BUY_USAGE_BROKEN_EQUIP;
using ai::VENDOR_BUY_USAGE_EQUIP;

int main()
{
    std::cout << "Starting TortoiseBots vendor buy policy tests...\n";

    // Gear-budget mapping (#427, donor BuyAction.cpp:140-147): EQUIP, BAD
    // and BROKEN all spend the gear budget. Everything else keeps its own
    // budget or stays unbought (BROKEN_AH is ours only — donor has no such
    // usage — and is never bought, only kept until repaired).
    CHECK(VendorBuyUsesGearBudget(VENDOR_BUY_USAGE_EQUIP) == true);
    CHECK(VendorBuyUsesGearBudget(VENDOR_BUY_USAGE_BAD_EQUIP) == true);
    CHECK(VendorBuyUsesGearBudget(VENDOR_BUY_USAGE_BROKEN_EQUIP) == true);
    CHECK(VendorBuyUsesGearBudget(0) == false);   // NONE
    CHECK(VendorBuyUsesGearBudget(4) == false);   // QUEST
    CHECK(VendorBuyUsesGearBudget(5) == false);   // SKILL
    CHECK(VendorBuyUsesGearBudget(6) == false);   // USE
    CHECK(VendorBuyUsesGearBudget(9) == false);   // AH
    CHECK(VendorBuyUsesGearBudget(10) == false);  // BROKEN_AH
    CHECK(VendorBuyUsesGearBudget(11) == false);  // KEEP
    CHECK(VendorBuyUsesGearBudget(12) == false);  // VENDOR
    CHECK(VendorBuyUsesGearBudget(13) == false);  // AMMO
    std::cout << "  [PASS] gear budget covers EQUIP/BAD/BROKEN only\n";

    // Score ordering (#427, donor BuyAction.cpp:78-86): higher weighted
    // score ranks first; when either side scores 0 (unweighted or
    // not-yet-usable stock) item level decides, as before.
    CHECK(VendorBuyRanksFirst(900, 20, 100, 40) == true);   // score wins over level
    CHECK(VendorBuyRanksFirst(100, 40, 900, 20) == false);
    CHECK(VendorBuyRanksFirst(0, 40, 500, 20) == true);      // fallback: level decides
    CHECK(VendorBuyRanksFirst(500, 20, 0, 40) == false);
    CHECK(VendorBuyRanksFirst(0, 30, 0, 25) == true);       // both 0: higher level first
    CHECK(VendorBuyRanksFirst(0, 25, 0, 30) == false);
    // Exact ties rank neither first, so std::sort keeps vendor order.
    CHECK(VendorBuyRanksFirst(300, 20, 300, 20) == false);
    CHECK(VendorBuyRanksFirst(0, 20, 0, 20) == false);
    std::cout << "  [PASS] score orders stock, item level breaks unweighted ties\n";

    std::cout << "All vendor buy policy checks PASSED!\n";
    return 0;
}
