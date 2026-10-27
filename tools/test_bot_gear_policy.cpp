// Standalone regression test for `.bot gear` (raid task A7): the pure
// grammar, preset expansion, item-level cap filter, bag purge decision and
// summary line in runtime/policy/hire/BotGearPolicy.h.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_bot_gear_policy.cpp -o /tmp/test_bot_gear_policy
//   /tmp/test_bot_gear_policy

#include "../runtime/policy/hire/BotGearPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots::BotGear;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestGearArgs()
{
    Args a = Parse("Tanky tier 3 ilvl=66", kPresetsDefault);
    CHECK(a.error.empty());
    CHECK(a.target == Target::Bot);
    CHECK(a.name == "Tanky");
    CHECK(a.tier == 3);
    CHECK(a.ilvlCap == 66);
    CHECK(a.enchants);

    Args r = Parse("raid Gbmage tier 2 ilvl=63 noenchants", kPresetsDefault);
    CHECK(r.error.empty());
    CHECK(r.target == Target::Raid);
    CHECK(r.name == "Gbmage");
    CHECK(r.tier == 2);
    CHECK(r.ilvlCap == 63);
    CHECK(!r.enchants);

    Args zg = Parse("raid Gbmage preset zg", kPresetsDefault);
    CHECK(zg.error.empty());
    CHECK(zg.target == Target::Raid);
    CHECK(zg.name == "Gbmage");
    CHECK(zg.tier == 2);
    CHECK(zg.ilvlCap == 63);

    Args t1 = Parse("Bob preset t1", kPresetsDefault);
    CHECK(t1.error.empty());
    CHECK(t1.target == Target::Bot);
    CHECK(t1.name == "Bob");
    CHECK(t1.tier == 3);
    CHECK(t1.ilvlCap == 66);

    CHECK(Parse("Bob tier 9", kPresetsDefault).error == "tier");
    CHECK(Parse("Bob tier 2 ilvl=0", kPresetsDefault).error == "ilvl");
    CHECK(Parse("Bob preset nope", kPresetsDefault).error == "preset");
    CHECK(Parse("Bob tier 0", kPresetsDefault).error == "nothing");
    CHECK(Parse("", kPresetsDefault).error == "usage");
    CHECK(Parse("raid tier 2", kPresetsDefault).error == "usage");

    Args cased = Parse("RAID gbmage TIER 2", kPresetsDefault);
    CHECK(cased.error.empty());
    CHECK(cased.target == Target::Raid);
    CHECK(cased.tier == 2);
}

static void TestCapFilter()
{
    CHECK(RowAllowed(63, 63));
    CHECK(!RowAllowed(64, 63));
    CHECK(RowAllowed(90, 0));
}

static void TestSummary()
{
    BotResult tank;
    tank.name = "Tanky";
    tank.role = "tank";
    tank.geared = true;
    tank.avgIlvl = 62.0f;
    tank.cappedSlots = 0;
    tank.purged = 3;
    BotResult healer;
    healer.name = "Healy";
    healer.role = "healer";
    healer.geared = false;
    healer.skip = "level";
    BotResult dps;
    dps.name = "Dpsy";
    dps.role = "dps";
    dps.geared = true;
    dps.avgIlvl = 61.5f;
    dps.cappedSlots = 2;
    dps.purged = 1;
    std::string line = SummaryLine(2, 63, {tank, healer, dps});
    CHECK(line == "TBM:GEAR|partial|2|3|2|63|62.0|-|61.5|2|Healy(level)|4");
}

static void TestPurgeBagItem()
{
    CHECK(PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_CHEST, 70, 63, 0));
    CHECK(!PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_CHEST, 63, 63, 0));
    CHECK(!PurgeBagItem(1, INVTYPE_CHEST, 90, 63, 0));
    CHECK(!PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_AMMO, 90, 63, 0));
    CHECK(PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_CHEST, 70, 0, 66));
    CHECK(!PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_CHEST, 66, 0, 66));
    CHECK(!PurgeBagItem(0, INVTYPE_CHEST, 90, 63, 0));
    CHECK(PurgeBagItem(ITEM_CLASS_WEAPON, INVTYPE_CHEST, 70, 63, 0));
    CHECK(!PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_NON_EQUIP, 90, 63, 0));
    CHECK(!PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_BAG, 90, 63, 0));
    CHECK(!PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_QUIVER, 90, 63, 0));
}

static void TestPresetsDefaultParse()
{
    std::string presets(kPresetsDefault);
    size_t start = 0;
    size_t count = 0;
    while (start < presets.size())
    {
        size_t end = presets.find(';', start);
        std::string entry = presets.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t eq = entry.find('=');
        CHECK(eq != std::string::npos);
        std::string name = entry.substr(0, eq);
        CHECK(!name.empty());
        Args a = Parse("X preset " + name, kPresetsDefault);
        CHECK(a.error.empty());
        ++count;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    CHECK(count > 0);
}

int main()
{
    TestGearArgs();
    TestCapFilter();
    TestSummary();
    TestPurgeBagItem();
    TestPresetsDefaultParse();
    std::printf("bot gear policy: %d checks passed\n", checks);
    return 0;
}
