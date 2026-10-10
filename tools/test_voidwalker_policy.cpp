// Standalone regression test for the Voidwalker Suffering + Consume
// Shadows gates (runtime/VoidwalkerPolicy.h, PET-8): AoE taunt only for a
// permitted tanking Voidwalker facing 3+ mobs; self-heal only out of combat
// on a hurt demon.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_voidwalker_policy.cpp -o /tmp/test_voidwalker
//   /tmp/test_voidwalker

#include "../runtime/VoidwalkerPolicy.h"

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

static SufferingGateInputs Suf(uint32_t entry, bool allowed, uint8_t attackers)
{
    SufferingGateInputs inputs;
    inputs.hasPet = true;
    inputs.currentPetEntry = entry;
    inputs.petTauntAllowed = allowed;
    inputs.attackerCount = attackers;
    return inputs;
}

static ConsumeShadowsGateInputs Con(uint8_t health, bool combat, bool mounted)
{
    ConsumeShadowsGateInputs inputs;
    inputs.hasPet = true;
    inputs.currentPetEntry = WARLOCK_VOIDWALKER_ENTRY;
    inputs.petAlive = true;
    inputs.petHealth = health;
    inputs.ownerInCombat = combat;
    inputs.mounted = mounted;
    return inputs;
}

static void TestSufferingFiresWhenOutnumbered()
{
    CHECK(CanCastSuffering(Suf(WARLOCK_VOIDWALKER_ENTRY, true, 3)));
    CHECK(CanCastSuffering(Suf(WARLOCK_VOIDWALKER_ENTRY, true, 5)));
}

static void TestSufferingNeedsPack()
{
    CHECK(!CanCastSuffering(Suf(WARLOCK_VOIDWALKER_ENTRY, true, 0)));
    CHECK(!CanCastSuffering(Suf(WARLOCK_VOIDWALKER_ENTRY, true, 2)));
}

static void TestSufferingRespectsTauntPermission()
{
    CHECK(!CanCastSuffering(Suf(WARLOCK_VOIDWALKER_ENTRY, false, 4)));
}

static void TestSufferingNeedsVoidwalker()
{
    CHECK(!CanCastSuffering(Suf(416, true, 4)));
    CHECK(!CanCastSuffering(Suf(417, true, 4)));
    SufferingGateInputs inputs;
    inputs.petTauntAllowed = true;
    inputs.attackerCount = 4;
    CHECK(!CanCastSuffering(inputs));
}

static void TestConsumeShadowsFiresOutOfCombat()
{
    CHECK(CanCastConsumeShadows(Con(50, false, false)));
    CHECK(CanCastConsumeShadows(Con(69, false, false)));
}

static void TestConsumeShadowsStaysQuiet()
{
    CHECK(!CanCastConsumeShadows(Con(70, false, false)));
    CHECK(!CanCastConsumeShadows(Con(50, true, false)));
    CHECK(!CanCastConsumeShadows(Con(50, false, true)));
    ConsumeShadowsGateInputs dead = Con(50, false, false);
    dead.petAlive = false;
    CHECK(!CanCastConsumeShadows(dead));
    ConsumeShadowsGateInputs imp = Con(50, false, false);
    imp.currentPetEntry = 416;
    CHECK(!CanCastConsumeShadows(imp));
}

int main()
{
    TestSufferingFiresWhenOutnumbered();
    TestSufferingNeedsPack();
    TestSufferingRespectsTauntPermission();
    TestSufferingNeedsVoidwalker();
    TestConsumeShadowsFiresOutOfCombat();
    TestConsumeShadowsStaysQuiet();
    std::printf("test_voidwalker_policy: %d checks passed\n", checks);
    return 0;
}
