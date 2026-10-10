// Standalone regression test for the trinket-use filters (mod-playerbots
// parity, CD-1): positive-only, aura-or-mana-restore-only, mana/health
// gates, cooldown memory.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_trinket_use_policy.cpp -o /tmp/test_trinket_use
//   /tmp/test_trinket_use

#include "../ai/playerbot/TrinketUsePolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace ai;
using TEC = TrinketEffectClass;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestSpellAndEffectGates()
{
    CHECK(TrinketSpellAllowed(true));
    CHECK(!TrinketSpellAllowed(false));
    CHECK(TrinketEffectAllowed(TEC::Aura));
    CHECK(TrinketEffectAllowed(TEC::ManaRestore));
    CHECK(!TrinketEffectAllowed(TEC::None));
    CHECK(!TrinketEffectAllowed(TEC::ManaEfficiency));
    CHECK(!TrinketEffectAllowed(TEC::Defensive));
    std::printf("  [PASS] spell/effect gates\n");
}

static void TestManaGates()
{
    // Restore fires below medium (40), not at/above.
    CHECK(TrinketManaAllowed(TEC::ManaRestore, true, 39, 40));
    CHECK(!TrinketManaAllowed(TEC::ManaRestore, true, 40, 40));
    CHECK(!TrinketManaAllowed(TEC::ManaRestore, false, 10, 40));
    // Efficiency fires below high (65), not at/above.
    CHECK(TrinketManaAllowed(TEC::ManaEfficiency, true, 64, 40));
    CHECK(!TrinketManaAllowed(TEC::ManaEfficiency, true, 65, 40));
    CHECK(!TrinketManaAllowed(TEC::ManaEfficiency, false, 10, 40));
    // Aura/defensive ignore mana entirely.
    CHECK(TrinketManaAllowed(TEC::Aura, false, 100, 40));
    CHECK(TrinketManaAllowed(TEC::Defensive, false, 100, 40));
    std::printf("  [PASS] mana gates\n");
}

static void TestHealthGate()
{
    CHECK(TrinketHealthAllowed(TEC::Defensive, 20, 20));
    CHECK(!TrinketHealthAllowed(TEC::Defensive, 21, 20));
    CHECK(TrinketHealthAllowed(TEC::Aura, 100, 20));
    CHECK(TrinketHealthAllowed(TEC::ManaRestore, 100, 20));
    std::printf("  [PASS] health gate\n");
}

static void TestCooldowns()
{
    CHECK(TrinketCooldownAllowed(false, false));
    CHECK(!TrinketCooldownAllowed(true, false));
    CHECK(!TrinketCooldownAllowed(false, true));
    CHECK(!TrinketCooldownAllowed(true, true));
    std::printf("  [PASS] cooldown memory\n");
}

int main()
{
    std::printf("Starting TortoiseBots trinket-use policy tests...\n");
    TestSpellAndEffectGates();
    TestManaGates();
    TestHealthGate();
    TestCooldowns();
    std::printf("All trinket-use policy tests passed (%d checks).\n", checks);
    return 0;
}
