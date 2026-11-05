// Standalone test for ai/playerbot/policy/guild/EnchantGatherPolicy.h:
// the pure ready/shortfall rules behind the enchant gathering leg.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_enchant_gather_policy.cpp -o /tmp/test_enchant_gather_policy
//   /tmp/test_enchant_gather_policy
#include "../ai/playerbot/policy/guild/EnchantGatherPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Empty list (an enchant with no reagents) is complete at once.
    CHECK(EnchantMaterialsComplete(nullptr, 0));

    // All in the bags: complete, no missing rows.
    EnchantMaterialState full[] = { { 12811, 1, 1, false }, { 11137, 2, 3, false } };
    CHECK(EnchantMaterialsComplete(full, 2));
    CHECK(EnchantMissingMaterials(full, 2).empty());
    CHECK(EnchantMaterialShortfall(full[0]) == 0);

    // One short: incomplete, one missing row with the shortfall count.
    EnchantMaterialState shortOne[] = { { 12811, 1, 1, false }, { 11137, 2, 1, false } };
    CHECK(!EnchantMaterialsComplete(shortOne, 2));
    std::vector<EnchantMaterial> missing = EnchantMissingMaterials(shortOne, 2);
    CHECK(missing.size() == 1 && missing[0].itemId == 11137 && missing[0].need == 1);
    CHECK(EnchantMaterialShortfall(shortOne[1]) == 1);

    // Ticked reagent counts from bags, but its shortfall is the giver's:
    // unsourcable, and listed after the unticked ones.
    EnchantMaterialState ticked[] = { { 12811, 1, 0, true }, { 11137, 2, 1, false } };
    CHECK(!EnchantMaterialsComplete(ticked, 2));
    CHECK(!EnchantReagentSourcable(ticked[0]));
    CHECK(EnchantReagentSourcable(ticked[1]));
    std::vector<EnchantMaterial> tickedMissing = EnchantMissingMaterials(ticked, 2);
    CHECK(tickedMissing.size() == 2);
    CHECK(tickedMissing[0].itemId == 11137 && tickedMissing[0].need == 1);
    CHECK(tickedMissing[1].itemId == 12811 && tickedMissing[1].need == 1);

    // Ticked and fully in the bags: complete (the giver delivered early).
    EnchantMaterialState tickedHeld[] = { { 12811, 1, 1, true } };
    CHECK(EnchantMaterialsComplete(tickedHeld, 1));
    CHECK(EnchantMissingMaterials(tickedHeld, 1).empty());

    // Deposit quota on cancel/timeout: what the bags hold, capped at one
    // cast's need. Empty-handed reagents drop out.
    EnchantMaterialState deposit[] = { { 12811, 1, 0, false }, { 11137, 2, 1, false }, { 11129, 3, 7, false } };
    std::vector<EnchantDepositRow> due = EnchantDepositDue(deposit, 3);
    CHECK(due.size() == 2);
    CHECK(due[0].itemId == 11137 && due[0].quota == 1);
    CHECK(due[1].itemId == 11129 && due[1].quota == 3);

    // Exact stock deposits in full; a ticked reagent the giver left behind
    // banks like any other.
    EnchantMaterialState exact[] = { { 12811, 1, 1, false }, { 11137, 2, 2, true } };
    std::vector<EnchantDepositRow> exactDue = EnchantDepositDue(exact, 2);
    CHECK(exactDue.size() == 2);
    CHECK(exactDue[0].quota == 1 && exactDue[1].quota == 2);
    CHECK(EnchantDepositDue(nullptr, 0).empty());

    if (failures)
    {
        std::printf("enchant-gather policy: %d FAILURES\n", failures);
        return 1;
    }
    std::printf("enchant-gather policy: OK\n");
    return 0;
}
