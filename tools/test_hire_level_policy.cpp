// Standalone regression test for issue #936: the hire-level policy
// (runtime/HireLevelPolicy.h). Guards
//  - `.bot hire` level-word parsing (level=<n>, level:<n>, lvl=<n>, bare 12+),
//  - refusal of level words without a usable number,
//  - the clamp: 1 .. hirer level + allowance, never above 60,
//  - the GM exemption from the hirer-relative cap,
//  - no level requested = the hirer's level (today's behaviour).
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_level_policy.cpp -o /tmp/test_hire_level_policy
//   /tmp/test_hire_level_policy

#include "../runtime/HireLevelPolicy.h"

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

static void TestLevelWord()
{
    uint32_t lv = 0;
    CHECK(ParseHireLevelWord("level=42", lv) == HireLevelWord::Ok && lv == 42);
    CHECK(ParseHireLevelWord("level:45", lv) == HireLevelWord::Ok && lv == 45);
    CHECK(ParseHireLevelWord("lvl=7", lv) == HireLevelWord::Ok && lv == 7);
    CHECK(ParseHireLevelWord("lvl:60", lv) == HireLevelWord::Ok && lv == 60);
    CHECK(ParseHireLevelWord("level=1", lv) == HireLevelWord::Ok && lv == 1);
    // Above 60 still parses; the clamp lowers it.
    CHECK(ParseHireLevelWord("level=99", lv) == HireLevelWord::Ok && lv == 99);
    CHECK(ParseHireLevelWord("level=999", lv) == HireLevelWord::Ok && lv == 999);

    // A level word with no usable number is refused, not ignored.
    lv = 0;
    CHECK(ParseHireLevelWord("level=", lv) == HireLevelWord::Bad);
    CHECK(ParseHireLevelWord("level=0", lv) == HireLevelWord::Bad);
    CHECK(ParseHireLevelWord("level=abc", lv) == HireLevelWord::Bad);
    CHECK(ParseHireLevelWord("level=-5", lv) == HireLevelWord::Bad);
    CHECK(ParseHireLevelWord("level=4x", lv) == HireLevelWord::Bad);
    CHECK(ParseHireLevelWord("level=1000", lv) == HireLevelWord::Bad);
    CHECK(ParseHireLevelWord("level=99999999999", lv) == HireLevelWord::Bad);
    CHECK(ParseHireLevelWord("lvl:", lv) == HireLevelWord::Bad);
    CHECK(lv == 0);

    // Everything else is not a level word, so old input parses as before.
    CHECK(ParseHireLevelWord("level", lv) == HireLevelWord::NotLevel);
    CHECK(ParseHireLevelWord("levelling", lv) == HireLevelWord::NotLevel);
    CHECK(ParseHireLevelWord("lvl", lv) == HireLevelWord::NotLevel);
    CHECK(ParseHireLevelWord("mage", lv) == HireLevelWord::NotLevel);
    CHECK(ParseHireLevelWord("t3", lv) == HireLevelWord::NotLevel);
    CHECK(ParseHireLevelWord("", lv) == HireLevelWord::NotLevel);
    CHECK(ParseHireLevelWord("45", lv) == HireLevelWord::NotLevel);
}

static void TestBareLevel()
{
    uint32_t lv = 0;
    CHECK(ParseBareHireLevel("45", lv) && lv == 45);
    CHECK(ParseBareHireLevel("12", lv) && lv == 12);
    CHECK(ParseBareHireLevel("60", lv) && lv == 60);
    // Numbers a race/gender/class id can be keep their old meaning.
    lv = 0;
    CHECK(!ParseBareHireLevel("0", lv));
    CHECK(!ParseBareHireLevel("1", lv));
    CHECK(!ParseBareHireLevel("5", lv));
    CHECK(!ParseBareHireLevel("11", lv));
    CHECK(!ParseBareHireLevel("", lv));
    CHECK(!ParseBareHireLevel("4x", lv));
    CHECK(!ParseBareHireLevel("-20", lv));
    CHECK(!ParseBareHireLevel("1000", lv));
    CHECK(lv == 0);
}

static void TestNoLevelKeepsHirerLevel()
{
    for (uint32_t gm = 0; gm < 2; ++gm)
    {
        HireLevelResult r = ResolveHireLevel(43, 0, 0, gm != 0);
        CHECK(r.level == 43 && !r.clamped);
        r = ResolveHireLevel(60, 0, 5, gm != 0);
        CHECK(r.level == 60 && !r.clamped);
        r = ResolveHireLevel(1, 0, 0, gm != 0);
        CHECK(r.level == 1 && !r.clamped);
        // Odd hirer levels are folded into 1..60 as before.
        r = ResolveHireLevel(0, 0, 0, gm != 0);
        CHECK(r.level == 1);
        r = ResolveHireLevel(70, 0, 0, gm != 0);
        CHECK(r.level == 60);
    }
}

static void TestDefaultNoHigherThanHirer()
{
    // Default allowance 0: up to the hirer's level, hiring down is free.
    HireLevelResult r = ResolveHireLevel(50, 42, 0, false);
    CHECK(r.level == 42 && !r.clamped);
    r = ResolveHireLevel(50, 1, 0, false);
    CHECK(r.level == 1 && !r.clamped);
    r = ResolveHireLevel(50, 50, 0, false);
    CHECK(r.level == 50 && !r.clamped);
    r = ResolveHireLevel(50, 51, 0, false);
    CHECK(r.level == 50 && r.clamped && r.maxAllowed == 50);
    r = ResolveHireLevel(50, 60, 0, false);
    CHECK(r.level == 50 && r.clamped);
    r = ResolveHireLevel(50, 255, 0, false);
    CHECK(r.level == 50 && r.clamped);
    // A low-level hirer cannot reach a dungeon band.
    r = ResolveHireLevel(20, 44, 0, false);
    CHECK(r.level == 20 && r.clamped);
}

static void TestAllowanceAboveHirer()
{
    HireLevelResult r = ResolveHireLevel(40, 45, 5, false);
    CHECK(r.level == 45 && !r.clamped);
    r = ResolveHireLevel(40, 46, 5, false);
    CHECK(r.level == 45 && r.clamped && r.maxAllowed == 45);
    // Never above 60, whatever the allowance.
    r = ResolveHireLevel(58, 60, 10, false);
    CHECK(r.level == 60 && !r.clamped);
    r = ResolveHireLevel(58, 200, 59, false);
    CHECK(r.level == 60 && r.clamped && r.maxAllowed == 60);
    r = ResolveHireLevel(1, 60, 59, false);
    CHECK(r.level == 60 && !r.clamped);
}

static void TestGmIsNotLimitedByHirer()
{
    HireLevelResult r = ResolveHireLevel(10, 60, 0, true);
    CHECK(r.level == 60 && !r.clamped);
    r = ResolveHireLevel(10, 44, 0, true);
    CHECK(r.level == 44 && !r.clamped);
    r = ResolveHireLevel(10, 1, 0, true);
    CHECK(r.level == 1);
    // Still never above 60.
    r = ResolveHireLevel(10, 61, 0, true);
    CHECK(r.level == 60 && r.clamped && r.maxAllowed == 60);
    r = ResolveHireLevel(60, 255, 0, true);
    CHECK(r.level == 60 && r.clamped);
}

static void TestMaxAllowed()
{
    CHECK(HireMaxAllowedLevel(43, 0, false) == 43);
    CHECK(HireMaxAllowedLevel(43, 3, false) == 46);
    CHECK(HireMaxAllowedLevel(59, 5, false) == 60);
    CHECK(HireMaxAllowedLevel(0, 0, false) == 1);
    CHECK(HireMaxAllowedLevel(5, 0, true) == 60);
}

int main()
{
    TestLevelWord();
    TestBareLevel();
    TestNoLevelKeepsHirerLevel();
    TestDefaultNoHigherThanHirer();
    TestAllowanceAboveHirer();
    TestGmIsNotLimitedByHirer();
    TestMaxAllowed();
    std::printf("test_hire_level_policy: %d checks passed\n", checks);
    return 0;
}
