// Standalone regression test for the group-heal port (mod-playerbots
// parity): the hurt threshold scales with near-group size so a 5-man does
// not need a raid-sized clump and a raid does not fire on two scratches.
// Guards the donor scaling table plus the below-5 refusal.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_group_heal_policy.cpp -o /tmp/test_group_heal
//   /tmp/test_group_heal

#include "../ai/playerbot/GroupHealPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestThresholdTable()
{
    // Donor table: <=5 -> 3; <=10 -> min(n/2, 5); <=25 -> min(n/2, 10).
    CHECK(GroupHealHurtThreshold(5) == 3);
    CHECK(GroupHealHurtThreshold(4) == 3);
    CHECK(GroupHealHurtThreshold(6) == 3);
    CHECK(GroupHealHurtThreshold(8) == 4);
    CHECK(GroupHealHurtThreshold(10) == 5);
    CHECK(GroupHealHurtThreshold(12) == 6);
    CHECK(GroupHealHurtThreshold(20) == 10);
    CHECK(GroupHealHurtThreshold(25) == 10);
    CHECK(GroupHealHurtThreshold(30) == 15);
    CHECK(GroupHealHurtThreshold(40) == 15);
    std::printf("  [PASS] threshold table (%d checks)\n", checks);
}

static void TestShouldGroupHeal()
{
    // Below 5 near members: never fires, however hurt the few are.
    CHECK(!ShouldGroupHeal(0, 0));
    CHECK(!ShouldGroupHeal(1, 1));
    CHECK(!ShouldGroupHeal(4, 4));
    // 5-man: 3 hurt fires, 2 does not.
    CHECK(!ShouldGroupHeal(5, 2));
    CHECK(ShouldGroupHeal(5, 3));
    // 10-man: threshold 5.
    CHECK(!ShouldGroupHeal(10, 4));
    CHECK(ShouldGroupHeal(10, 5));
    // 25-man: threshold 10.
    CHECK(!ShouldGroupHeal(25, 9));
    CHECK(ShouldGroupHeal(25, 10));
    // 40-man: capped at 15.
    CHECK(!ShouldGroupHeal(40, 14));
    CHECK(ShouldGroupHeal(40, 15));
    std::printf("  [PASS] should-group-heal gate\n");
}

int main()
{
    std::printf("Starting TortoiseBots group-heal policy tests...\n");
    TestThresholdTable();
    TestShouldGroupHeal();
    std::printf("All group-heal policy tests passed (%d checks).\n", checks);
    return 0;
}
