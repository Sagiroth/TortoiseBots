#include "../ai/playerbot/SteepSlopePolicy.h"

#include <cstdint>
#include <iostream>
#include <cstdlib>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

// The core's NavTerrain flags this mirrors (MoveMapSharedDefines.h): steep
// ground is 0x10 on top of plain ground 0x01, magma 0x02, slime 0x04, water
// 0x08. The cmangos shim's 0x02 "steep" named magma and 0x18 "magma+slime"
// named water+steep in this core.
static void TestFlagValues()
{
    CHECK(ai::kNavGround == 0x01);
    CHECK(ai::kNavMagma == 0x02);
    CHECK(ai::kNavSlime == 0x04);
    CHECK(ai::kNavWater == 0x08);
    CHECK(ai::kNavSteepSlopes == 0x10);
}

// Exactly NOPATH retries: the bot standing on steep ground finds no start
// polygon and the core answers NOPATH, so the walk-off retry may change the
// answer. Anything else never retries: a merely longer path is still a path.
static void TestRetryRule()
{
    CHECK(ai::ShouldRetryPermissive(0x0008));
    CHECK(!ai::ShouldRetryPermissive(0x0000));
    CHECK(!ai::ShouldRetryPermissive(0x0001));
    CHECK(!ai::ShouldRetryPermissive(0x0002));
    CHECK(!ai::ShouldRetryPermissive(0x0004));
    CHECK(!ai::ShouldRetryPermissive(0x0009));
    CHECK(!ai::ShouldRetryPermissive(0x0011));
}

int main()
{
    TestFlagValues();
    TestRetryRule();
    std::cout << "steep slope policy: OK\n";
    return 0;
}
