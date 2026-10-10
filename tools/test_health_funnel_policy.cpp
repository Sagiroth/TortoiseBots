// Standalone regression test for the Health Funnel gate
// (runtime/HealthFunnelPolicy.h, PET-6): channel only while the demon is
// below half and the owner can afford the drain, combat-only.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_health_funnel_policy.cpp -o /tmp/test_health_funnel
//   /tmp/test_health_funnel

#include "../runtime/HealthFunnelPolicy.h"

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

static HealthFunnelGateInputs Inputs(uint8_t pet, uint8_t owner, bool combat)
{
    HealthFunnelGateInputs inputs;
    inputs.hasPet = true;
    inputs.petAlive = true;
    inputs.petHealth = pet;
    inputs.ownerHealth = owner;
    inputs.ownerInCombat = combat;
    return inputs;
}

static void TestHurtingPetHealthyOwnerFires()
{
    CHECK(CanCastHealthFunnel(Inputs(30, 80, true)));
    CHECK(CanCastHealthFunnel(Inputs(49, 61, true)));
}

static void TestHealthyPetStaysQuiet()
{
    CHECK(!CanCastHealthFunnel(Inputs(50, 90, true)));
    CHECK(!CanCastHealthFunnel(Inputs(100, 100, true)));
}

static void TestDrainedOwnerStaysQuiet()
{
    CHECK(!CanCastHealthFunnel(Inputs(20, 60, true)));
    CHECK(!CanCastHealthFunnel(Inputs(20, 30, true)));
}

static void TestOutOfCombatStaysQuiet()
{
    CHECK(!CanCastHealthFunnel(Inputs(20, 100, false)));
}

static void TestNoPetStaysQuiet()
{
    HealthFunnelGateInputs inputs;
    CHECK(!CanCastHealthFunnel(inputs));
    inputs.hasPet = true;
    inputs.petAlive = false;
    CHECK(!CanCastHealthFunnel(inputs));
}

int main()
{
    TestHurtingPetHealthyOwnerFires();
    TestHealthyPetStaysQuiet();
    TestDrainedOwnerStaysQuiet();
    TestOutOfCombatStaysQuiet();
    TestNoPetStaysQuiet();
    std::printf("test_health_funnel_policy: %d checks passed\n", checks);
    return 0;
}
