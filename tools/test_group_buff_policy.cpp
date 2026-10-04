// Standalone regression test for the group-buff port (issue #468): refresh a
// buff inside the window before it expires, and upgrade singles to the group
// variant only at quorum, trained and stocked. Guards the rules that keep
// reagent-less bots off the group cast and blessings out of the map.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_group_buff_policy.cpp -o /tmp/test_group_buff
//   /tmp/test_group_buff

#include "../ai/playerbot/GroupBuffPolicy.h"

#include <cstdint>
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

// Refresh: missing always rebuffs; inside the window rebuffs; outside holds.
// Permanent auras (remaining <= 0) never count.
static void TestRefreshWindow()
{
    CHECK(BuffNeedsRefresh(false, 0));
    CHECK(BuffNeedsRefresh(false, -1));
    CHECK(BuffNeedsRefresh(true, 1));
    CHECK(BuffNeedsRefresh(true, 14000));
    CHECK(!BuffNeedsRefresh(true, 15000));
    CHECK(!BuffNeedsRefresh(true, 30 * 60 * 1000));
    CHECK(!BuffNeedsRefresh(true, 0));
    CHECK(!BuffNeedsRefresh(true, -1));
}

// The window and quorum are ported donor constants: pin them so a stray
// edit has to face this test.
static void TestPortedConstants()
{
    CHECK(kBuffRefreshWindowMs == 15000);
    CHECK(kGroupBuffMinMissing == 3);
}

// Variant map: the five 1.12 group buffs, blessings excluded.
static void TestVariantMap()
{
    CHECK(GroupBuffVariantFor("mark of the wild") == "gift of the wild");
    CHECK(GroupBuffVariantFor("arcane intellect") == "arcane brilliance");
    CHECK(GroupBuffVariantFor("power word: fortitude") == "prayer of fortitude");
    CHECK(GroupBuffVariantFor("divine spirit") == "prayer of spirit");
    CHECK(GroupBuffVariantFor("shadow protection") == "prayer of shadow protection");
    CHECK(GroupBuffVariantFor("blessing of might").empty());
    CHECK(GroupBuffVariantFor("blessing of wisdom").empty());
    CHECK(GroupBuffVariantFor("thorns").empty());
}

// Pair scope: only recognized pairs route through the quorum/reagent gates,
// so the paladin greater-blessing path keeps its own behavior untouched.
static void TestUpgradePairScope()
{
    CHECK(IsGroupBuffUpgradePair("gift of the wild", "mark of the wild"));
    CHECK(IsGroupBuffUpgradePair("arcane brilliance", "arcane intellect"));
    CHECK(IsGroupBuffUpgradePair("prayer of fortitude", "power word: fortitude"));
    CHECK(IsGroupBuffUpgradePair("prayer of spirit", "divine spirit"));
    CHECK(IsGroupBuffUpgradePair("prayer of shadow protection", "shadow protection"));
    CHECK(!IsGroupBuffUpgradePair("greater blessing of might", "blessing of might"));
    CHECK(!IsGroupBuffUpgradePair("gift of the wild", "arcane intellect"));
    CHECK(!IsGroupBuffUpgradePair("gift of the wild", ""));
}

// Upgrade: quorum plus trained plus stocked.
static void TestUpgradeQuorum()
{
    CHECK(ShouldUpgradeToGroupBuff(true, true, 3));
    CHECK(ShouldUpgradeToGroupBuff(true, true, 5));
    CHECK(!ShouldUpgradeToGroupBuff(true, true, 2));
    CHECK(!ShouldUpgradeToGroupBuff(true, true, 0));
}

// Untrained group spell or no reagent: singles.
static void TestUpgradeStock()
{
    CHECK(!ShouldUpgradeToGroupBuff(false, true, 5));
    CHECK(!ShouldUpgradeToGroupBuff(true, false, 5));
    CHECK(!ShouldUpgradeToGroupBuff(false, false, 5));
}

int main()
{
    TestRefreshWindow();
    TestPortedConstants();
    TestVariantMap();
    TestUpgradePairScope();
    TestUpgradeQuorum();
    TestUpgradeStock();
    std::printf("test_group_buff_policy: %d checks passed\n", checks);
    return 0;
}
