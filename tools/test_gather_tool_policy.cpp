// Standalone regression test for the gathering-tool allowlist (E02,
// runtime/GatherToolPolicy.h): every donor pick/knife that exists in
// tw_world.item_template must satisfy the gate; the old single-item check
// (2901 / 7005 only) rejected bots carrying any other valid tool.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_gather_tool_policy.cpp -o /tmp/test_gather_tool
//   /tmp/test_gather_tool

#include "../runtime/GatherToolPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static bool HasItems(std::set<uint32_t> const& bag, bool mining)
{
    auto hasItem = [&bag](uint32_t id) { return bag.count(id) > 0; };
    return mining ? HasAnyTool(hasItem, kMiningPickIds)
                  : HasAnyTool(hasItem, kSkinningKnifeIds);
}

// The regression: a bot carrying a Kobold Excavation Pick (778) or Finkle's
// Skinner (12709) was refused the node under the old 2901/7005-only check.
static void TestAlternateToolsAdmitted()
{
    CHECK(HasItems({778}, true));
    CHECK(HasItems({12709}, false));
    CHECK(HasItems({19901}, false));
    CHECK(HasItems({9465}, true));
    CHECK(HasItems({20723}, true));
}

// The old single-item behaviour is preserved, not replaced.
static void TestLegacyToolsStillAdmitted()
{
    CHECK(HasItems({2901}, true));
    CHECK(!HasItems({2901}, false));
    CHECK(HasItems({7005}, false));
    CHECK(!HasItems({7005}, true));
}

// Empty bags and unrelated items still fail closed.
static void TestNoToolRejected()
{
    CHECK(!HasItems({}, true));
    CHECK(!HasItems({}, false));
    CHECK(!HasItems({12345}, true));
    CHECK(!HasItems({12345}, false));
}

// WotLK-only donor ids (40772/40892/40893) have no 1.12 rows and must not
// be in either list.
static void TestWotlkIdsExcluded()
{
    for (uint32_t id : kMiningPickIds)
        CHECK(id != 40772 && id != 40892 && id != 40893);
    for (uint32_t id : kSkinningKnifeIds)
        CHECK(id != 40772 && id != 40892 && id != 40893);
}

int main()
{
    std::printf("Starting gather tool policy tests...\n");
    TestAlternateToolsAdmitted();
    std::printf("  [PASS] alternate tools admitted\n");
    TestLegacyToolsStillAdmitted();
    std::printf("  [PASS] legacy tools still admitted\n");
    TestNoToolRejected();
    std::printf("  [PASS] missing tool rejected\n");
    TestWotlkIdsExcluded();
    std::printf("  [PASS] WotLK-only ids excluded\n");
    std::printf("All gather tool policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
