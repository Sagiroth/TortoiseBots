// Standalone regression test for the attack order result
// (runtime/AttackOrderPolicy.h): an order on the victim the bot already
// attacks the asked way is fulfilled, not failed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_attack_order_policy.cpp -o /tmp/test_attack_order
//   /tmp/test_attack_order

#include "../runtime/AttackOrderPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

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
    // Already swinging at the ordered target: the live "melee - FAILED" case.
    CHECK(IsAlreadyAttackingTarget(true, true, true));
    // Same victim, swing not on yet: Unit::Attack must still arm it.
    CHECK(!IsAlreadyAttackingTarget(true, true, false));
    // Ranged/caster order on the current victim: nothing left to do.
    CHECK(IsAlreadyAttackingTarget(true, false, false));
    CHECK(IsAlreadyAttackingTarget(true, false, true));
    // A different (or no) victim: a real target switch, Unit::Attack decides.
    CHECK(!IsAlreadyAttackingTarget(false, true, true));
    CHECK(!IsAlreadyAttackingTarget(false, true, false));
    CHECK(!IsAlreadyAttackingTarget(false, false, false));

    std::printf("test_attack_order_policy: %d checks passed\n", checks);
    return 0;
}
