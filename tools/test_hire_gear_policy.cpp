// Standalone regression test for issue #490 PR 1: static best-in-slot sets
// for level-60 hires. Guards
//  - the spec-key table (28 menu options; druid bear/cat get distinct keys
//    even though they share the feral premade path),
//  - role-only fallback resolution (`.bot hire` without a spec word),
//  - the effective-tier and eligibility rules (level 60, tier >= 1, enabled),
//  - the race-override lookup (race row wins, else race 0, else no row).
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_gear_policy.cpp -o /tmp/test_hire_gear
//   /tmp/test_hire_gear

#include "../runtime/policy/hire/HireGearPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static uint8_t const kClasses[] = {
    kHireClassWarrior, kHireClassPaladin, kHireClassHunter, kHireClassRogue, kHireClassPriest,
    kHireClassShaman, kHireClassMage, kHireClassWarlock, kHireClassDruid,
};

// All 29 menu options resolve to a non-empty key; druid gets 4 distinct keys
// with bear != cat.
static void TestSpecKeys()
{
    size_t total = 0;
    for (uint8_t cls : kClasses)
    {
        uint32_t count = 0;
        HireSpecOptions(cls, count);
        CHECK(count == ((cls == kHireClassDruid || cls == kHireClassShaman) ? 4u : 3u));
        for (uint32_t i = 0; i < count; ++i)
        {
            char const* key = HireGearSpecKey(cls, static_cast<int>(i));
            CHECK(key != nullptr && key[0] != '\0');
            CHECK(std::strlen(key) <= 15);
            ++total;
        }
    }
    CHECK(total == 29);
    CHECK(std::string(HireGearSpecKey(kHireClassDruid, 0)) == "bear");
    CHECK(std::string(HireGearSpecKey(kHireClassDruid, 3)) == "cat");
    CHECK(std::string(HireGearSpecKey(kHireClassShaman, 3)) == "tank");
    CHECK(std::string(HireGearSpecKey(kHireClassWarrior, 1)) == "arms");
    CHECK(std::string(HireGearSpecKey(kHireClassWarrior, 2)) == "fury");
    CHECK(HireGearSpecKey(kHireClassWarrior, -1) == nullptr);
    CHECK(HireGearSpecKey(kHireClassWarrior, 3) == nullptr);
    CHECK(HireGearSpecKey(6, 0) == nullptr);
}

// Role-only requests pin to the menu's first option for the role.
static void TestRoleFallback()
{
    CHECK(std::string(ResolveHireGearSpecKey(kHireClassWarrior, 1, kHireRoleDps)) == "arms");
    CHECK(std::string(ResolveHireGearSpecKey(kHireClassWarrior, -1, kHireRoleTank)) == "protection");
    CHECK(std::string(ResolveHireGearSpecKey(kHireClassWarrior, -1, kHireRoleDps)) == "arms");
    CHECK(std::string(ResolveHireGearSpecKey(kHireClassPriest, -1, kHireRoleHealer)) == "holy");
    CHECK(std::string(ResolveHireGearSpecKey(kHireClassShaman, -1, kHireRoleTank)) == "tank");
    CHECK(std::string(ResolveHireGearSpecKey(kHireClassShaman, -1, kHireRoleHealer)) == "restoration");
    CHECK(std::string(ResolveHireGearSpecKey(kHireClassDruid, -1, kHireRoleDps)) == "balance");
    CHECK(ResolveHireGearSpecKey(6, -1, kHireRoleDps) == nullptr);
}

// Master switch gates everything; StartTier clamps to 0..7.
static void TestEffectiveTier()
{
    CHECK(EffectiveHireGearTier(false, 7) == 0);
    CHECK(EffectiveHireGearTier(true, 0) == 0);
    CHECK(EffectiveHireGearTier(true, 3) == 3);
    CHECK(EffectiveHireGearTier(true, 7) == 7);
    CHECK(EffectiveHireGearTier(true, 70) == 7);
}

// Only enabled level-60 hires at tier >= 1 take the static pass.
static void TestEligibility()
{
    CHECK(HireGearTierEligible(60, true, 1));
    CHECK(HireGearTierEligible(60, true, 7));
    CHECK(!HireGearTierEligible(60, true, 0));
    CHECK(!HireGearTierEligible(59, true, 7));
    CHECK(!HireGearTierEligible(60, false, 7));
    CHECK(!HireGearTierEligible(60, true, 8));
}

static HireGearRow MakeRow(uint8_t cls, char const* spec, uint8_t race, uint8_t tier, uint8_t slot,
    uint32_t item, uint32_t enchant)
{
    HireGearRow row;
    row.classId = cls;
    std::strncpy(row.spec, spec, sizeof(row.spec) - 1);
    row.race = race;
    row.tier = tier;
    row.slot = slot;
    row.itemId = item;
    row.enchantSpell = enchant;
    return row;
}

// Race-specific row wins; else race 0; else no row. Same input, same row.
static void TestLookup()
{
    std::vector<HireGearRow> rows;
    rows.push_back(MakeRow(1, "fury", 0, 1, 15, 1001, 2001));
    rows.push_back(MakeRow(1, "fury", 1, 1, 15, 1002, 2002));
    rows.push_back(MakeRow(1, "fury", 0, 1, 0, 1003, 0));
    rows.push_back(MakeRow(1, "arms", 0, 1, 15, 1004, 0));

    HireGearRow const* out = nullptr;
    CHECK(LookupHireGearRow(rows.data(), rows.size(), 1, "fury", 1, 1, 15, out));
    CHECK(out && out->itemId == 1002 && out->enchantSpell == 2002);
    CHECK(LookupHireGearRow(rows.data(), rows.size(), 1, "fury", 2, 1, 15, out));
    CHECK(out && out->itemId == 1001 && out->enchantSpell == 2001);
    CHECK(LookupHireGearRow(rows.data(), rows.size(), 1, "fury", 2, 1, 0, out));
    CHECK(out && out->itemId == 1003);
    CHECK(!LookupHireGearRow(rows.data(), rows.size(), 1, "fury", 2, 1, 1, out));
    CHECK(out == nullptr);
    CHECK(!LookupHireGearRow(rows.data(), rows.size(), 1, "fury", 2, 2, 15, out));
    CHECK(!LookupHireGearRow(rows.data(), rows.size(), 1, "protection", 1, 1, 15, out));
    // Deterministic: repeated lookups find the same row.
    HireGearRow const* first = nullptr;
    HireGearRow const* again = nullptr;
    CHECK(LookupHireGearRow(rows.data(), rows.size(), 1, "fury", 1, 1, 15, first));
    CHECK(LookupHireGearRow(rows.data(), rows.size(), 1, "fury", 1, 1, 15, again));
    CHECK(first && again && first->itemId == again->itemId && first->itemId == 1002);
}

// A bot's own premade path picks its kit; the role decides only when the path
// cannot be read. Menu option 0 is the tank spec, so a DPS warrior must not
// resolve to it.
static void TestOwnSpecKey()
{
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassWarrior, "fury", kHireRoleDps)) == "fury");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassWarrior, "arms", kHireRoleDps)) == "arms");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassWarrior, "protection", kHireRoleTank)) == "protection");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassPaladin, "holy", kHireRoleHealer)) == "holy");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassDruid, "restoration", kHireRoleHealer)) == "restoration");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassDruid, "feral", kHireRoleTank)) == "bear");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassDruid, "feral", kHireRoleDps)) == "cat");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassWarrior, nullptr, kHireRoleDps)) == "arms");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassWarrior, "", kHireRoleDps)) == "arms");
    CHECK(std::string(ResolveHireGearKeyForPath(kHireClassPaladin, "nonsense", kHireRoleHealer)) == "holy");
    CHECK(ResolveHireGearKeyForPath(6, "fury", kHireRoleDps) == nullptr);
}

int main()
{
    TestSpecKeys();
    TestRoleFallback();
    TestOwnSpecKey();
    TestEffectiveTier();
    TestEligibility();
    TestLookup();
    std::printf("hire gear policy OK (%d checks)\n", checks);
    return 0;
}
