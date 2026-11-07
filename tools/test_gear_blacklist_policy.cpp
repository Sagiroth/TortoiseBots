// Standalone regression test for the gear exclusion list: a blocked item id
// is never handed out by any auto-gear path. Guards
//  - the blacklist lookup (empty list blocks nothing),
//  - blacklist-over-whitelist precedence,
//  - the rules for gear players cannot really have (no source, retired or
//    test data, item level 0, legendary) and the whitelist override,
//  - worn forbidden gear counting as empty for pool/hired bots only; real
//    players and owned bots keep whatever they wear.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_gear_blacklist_policy.cpp -o /tmp/test_gear_blacklist
//   /tmp/test_gear_blacklist

#include "../runtime/policy/provision/GearBlacklistPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <list>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestLookup()
{
    std::list<uint32_t> empty;
    CHECK(!GearBlacklistBlocks(empty, 12345));

    std::list<uint32_t> blacklist = { 12345, 23456 };
    CHECK(GearBlacklistBlocks(blacklist, 12345));
    CHECK(GearBlacklistBlocks(blacklist, 23456));
    CHECK(!GearBlacklistBlocks(blacklist, 99999));
}

static void TestWhitelistPrecedence()
{
    std::list<uint32_t> empty;
    CHECK(GearWhitelistApplies(empty, true, 12345));
    CHECK(!GearWhitelistApplies(empty, false, 12345));

    // On both lists: the blacklist wins.
    std::list<uint32_t> blacklist = { 12345 };
    CHECK(!GearWhitelistApplies(blacklist, true, 12345));
    CHECK(GearWhitelistApplies(blacklist, true, 23456));
    CHECK(!GearWhitelistApplies(blacklist, false, 12345));
}

static void TestRetiredNames()
{
    CHECK(GearNameLooksRetired("Martin Thunder3 jesse test"));
    CHECK(GearNameLooksRetired("JYoo test item"));
    CHECK(GearNameLooksRetired("Zandalar Illusionist's Robe DEPRECATED"));
    CHECK(GearNameLooksRetired("Blessed Qiraji Naturalist Staff UNUSED"));
    CHECK(GearNameLooksRetired("Torka's Egg Cracker [UNUSED]"));
    CHECK(GearNameLooksRetired("[PH] Rising Dawn Gloves"));
    CHECK(GearNameLooksRetired("(OLD)Medium Throwing Knife"));
    CHECK(GearNameLooksRetired("ZZZZZ sword"));
    CHECK(GearNameLooksRetired("Test Fire Sword"));

    CHECK(!GearNameLooksRetired("Testament of Hope"));
    CHECK(!GearNameLooksRetired("The Greatest Wizzard Hat"));
    CHECK(!GearNameLooksRetired("Agatestone Crown"));
    CHECK(!GearNameLooksRetired("Lucky Top Hat"));
    CHECK(!GearNameLooksRetired(""));
}

static void TestForbiddenRules()
{
    BotGearFacts plain;
    CHECK(!BotGearForbidden(plain));

    BotGearFacts f = plain;
    f.blacklisted = true;
    CHECK(BotGearForbidden(f));
    f.whitelisted = true; // the blacklist still wins
    CHECK(BotGearForbidden(f));

    f = plain;
    f.knownSource = false;
    CHECK(BotGearForbidden(f));
    f.whitelisted = true; // the owner's explicit choice overrides
    CHECK(!BotGearForbidden(f));

    f = plain;
    f.retired = true;
    CHECK(BotGearForbidden(f));

    f = plain;
    f.itemLevel = 0;
    CHECK(BotGearForbidden(f));
    f.cosmeticSlot = true;
    CHECK(!BotGearForbidden(f));

    f = plain;
    f.quality = 4;
    CHECK(!BotGearForbidden(f));
    f.quality = kGearQualityLegendary;
    CHECK(BotGearForbidden(f));
    f.quality = 6;
    CHECK(BotGearForbidden(f));
}

static void TestWornCountsAsEmpty()
{
    // Pool and hired bots re-gear a forbidden worn piece.
    CHECK(GearWornCountsAsEmpty(false, true));
    CHECK(!GearWornCountsAsEmpty(false, false));

    // Real players and owned bots keep whatever they wear.
    CHECK(!GearWornCountsAsEmpty(true, true));
    CHECK(!GearWornCountsAsEmpty(true, false));
}

int main()
{
    TestLookup();
    TestWhitelistPrecedence();
    TestRetiredNames();
    TestForbiddenRules();
    TestWornCountsAsEmpty();
    std::printf("gear blacklist policy OK (%d checks)\n", checks);
    return 0;
}
