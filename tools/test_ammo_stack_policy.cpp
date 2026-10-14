// Standalone test for the ammo stack count InitAmmo tops up to: hunters keep
// 5 + level / 10 stacks, warriors and rogues carry the configured small number
// (default 2, owner decision 2026-10-14), the item cheat keeps one stack.
//
//   g++ -std=c++17 -Wall -Wextra tools/test_ammo_stack_policy.cpp -o /tmp/test_ammo_stack_policy

#include "../ai/playerbot/AmmoStackPolicy.h"

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

int main()
{
    // The hunter amount is what every ammo class got before.
    CHECK(HunterAmmoStacks(1) == 5);
    CHECK(HunterAmmoStacks(9) == 5);
    CHECK(HunterAmmoStacks(10) == 6);
    CHECK(HunterAmmoStacks(40) == 9);
    CHECK(HunterAmmoStacks(60) == 11);
    for (uint32_t level : { 1u, 15u, 40u, 60u })
        CHECK(AmmoStackTarget(true, level, kAmmoStacksNonHunterDefault, false) == HunterAmmoStacks(level));
    // A hunter ignores the non-hunter setting.
    CHECK(AmmoStackTarget(true, 40, 1, false) == 9);
    CHECK(AmmoStackTarget(true, 40, 10, false) == 9);

    // Warriors and rogues: the configured count, default 2.
    CHECK(kAmmoStacksNonHunterDefault == 2);
    CHECK(AmmoStackTarget(false, 40, kAmmoStacksNonHunterDefault, false) == 2);
    CHECK(AmmoStackTarget(false, 60, kAmmoStacksNonHunterDefault, false) == 2);
    CHECK(AmmoStackTarget(false, 1, kAmmoStacksNonHunterDefault, false) == 2);
    CHECK(AmmoStackTarget(false, 40, 1, false) == 1);
    // Never more than a hunter of the same level gets.
    CHECK(AmmoStackTarget(false, 1, 10, false) == 5);
    CHECK(AmmoStackTarget(false, 60, 10, false) == 10);
    // Never zero.
    CHECK(AmmoStackTarget(false, 40, 0, false) == 1);

    // The item cheat keeps one stack, for every class.
    CHECK(AmmoStackTarget(true, 60, 2, true) == 1);
    CHECK(AmmoStackTarget(false, 60, 2, true) == 1);

    // The config clamp: 1..10.
    CHECK(ClampNonHunterAmmoStacks(-3) == 1);
    CHECK(ClampNonHunterAmmoStacks(0) == 1);
    CHECK(ClampNonHunterAmmoStacks(1) == 1);
    CHECK(ClampNonHunterAmmoStacks(2) == 2);
    CHECK(ClampNonHunterAmmoStacks(10) == 10);
    CHECK(ClampNonHunterAmmoStacks(11) == 10);
    CHECK(ClampNonHunterAmmoStacks(1000000) == 10);
    CHECK(ClampNonHunterAmmoStacks(static_cast<int32_t>(kAmmoStacksNonHunterDefault)) == kAmmoStacksNonHunterDefault);

    std::printf("ammo stack policy: %d checks passed\n", checks);
    return 0;
}
