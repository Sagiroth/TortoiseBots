#include "../ai/playerbot/SteepSlopePolicy.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <vector>

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
    CHECK(ai::kSteepGradeRisePerRun == 1.19f);
    CHECK(ai::kSteepGradeWindowRunYd == 3.0f);
    CHECK(ai::kSteepGradeRiseMinYd == 1.5f);
}

// The retry runs when strict did not reach (no path or a partial one) and
// never when strict completed: a complete path costs no second query.
static void TestRetryRule()
{
    CHECK(ai::ShouldRetryPermissive(0x0008));
    CHECK(ai::ShouldRetryPermissive(0x0004));
    CHECK(ai::ShouldRetryPermissive(0x000c));
    CHECK(!ai::ShouldRetryPermissive(0x0000));
    CHECK(!ai::ShouldRetryPermissive(0x0001));
    CHECK(!ai::ShouldRetryPermissive(0x0002));
    CHECK(!ai::ShouldRetryPermissive(0x0009));
    CHECK(!ai::ShouldRetryPermissive(0x0011));
}

static std::vector<ai::SteepGradePoint> Line(std::vector<std::vector<float>> const& xyz)
{
    std::vector<ai::SteepGradePoint> line;
    for (auto const& p : xyz)
        line.push_back({p[0], p[1], p[2]});
    return line;
}

// Stairs at ~35 deg (1 yd up every 1.5 yd) pass: the flagged tiles the lead
// measured (Stormwind harbor, Orgrimmar leatherworkers) stay reachable.
static void TestStairsPass()
{
    std::vector<std::vector<float>> pts;
    for (int i = 0; i <= 10; ++i)
        pts.push_back({i * 1.5f, 0.0f, i * 1.0f});
    CHECK(ai::PermissivePathWalkable(Line(pts)));
}

// A cliff face at ~70 deg (3 yd up every 1 yd) fails.
static void TestCliffFails()
{
    std::vector<std::vector<float>> pts;
    for (int i = 0; i <= 10; ++i)
        pts.push_back({static_cast<float>(i), 0.0f, i * 3.0f});
    CHECK(!ai::PermissivePathWalkable(Line(pts)));
}

// Dropping down a steep face passes: only the climb is graded.
static void TestDownhillPasses()
{
    std::vector<std::vector<float>> pts;
    for (int i = 0; i <= 10; ++i)
        pts.push_back({static_cast<float>(i), 0.0f, 30.0f - i * 3.0f});
    CHECK(ai::PermissivePathWalkable(Line(pts)));
}

// One tall riser (2 yd up in one step, flat after) passes: windows span 3 yd
// of ground, so a single step never trips the rule.
static void TestSingleRiserPasses()
{
    CHECK(ai::PermissivePathWalkable(Line({{0, 0, 0}, {0.5f, 0, 2.0f}, {5.5f, 0, 2.0f}})));
}

// A short flat walk passes.
static void TestFlatPasses()
{
    CHECK(ai::PermissivePathWalkable(Line({{0, 0, 0}, {10, 0, 0}})));
}

int main()
{
    TestFlagValues();
    TestRetryRule();
    TestStairsPass();
    TestCliffFails();
    TestDownhillPasses();
    TestSingleRiserPasses();
    TestFlatPasses();
    std::cout << "steep slope policy: OK\n";
    return 0;
}
