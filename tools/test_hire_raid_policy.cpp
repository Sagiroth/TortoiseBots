// Standalone regression test for issue #900 (`.bot hire raid`): the pure
// composition grammar, spread/preset config parsing and roster builder in
// runtime/HireRaidPolicy.h. Guards
//  - role counts (3t 12h 24d, "3 tanks"), explicit classes (3 warrior:tank),
//    tier words and error cases,
//  - the shipped default spread and presets parse,
//  - preset expansion (a later tier word overrides the preset's),
//  - the roster: role order, round-robin spread, faction filtering, cap cut.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_raid_policy.cpp -o /tmp/test_hire_raid_policy
//   /tmp/test_hire_raid_policy

#include "../runtime/HireRaidPolicy.h"

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

// Mirrors HireProvisionService::ClassCanRole / DefaultRoleForClass.
static bool CanRole(uint8_t c, uint8_t role)
{
    if (role == kHireRoleTank)
        return c == kHireClassWarrior || c == kHireClassPaladin || c == kHireClassDruid;
    if (role == kHireRoleHealer)
        return c == kHireClassPriest || c == kHireClassPaladin || c == kHireClassShaman || c == kHireClassDruid;
    return role == kHireRoleDps && c != 0;
}

static uint8_t DefaultRole(uint8_t c)
{
    if (c == kHireClassPriest || c == kHireClassShaman || c == kHireClassDruid || c == kHireClassPaladin)
        return kHireRoleHealer;
    return kHireRoleDps;
}

static HireRaidClassRules Rules()
{
    HireRaidClassRules r;
    r.canRole = &CanRole;
    r.defaultRole = &DefaultRole;
    return r;
}

static uint32_t AllClasses()
{
    uint32_t m = 0;
    for (uint8_t c : { kHireClassWarrior, kHireClassPaladin, kHireClassHunter, kHireClassRogue, kHireClassPriest,
        kHireClassShaman, kHireClassMage, kHireClassWarlock, kHireClassDruid })
        m |= 1u << c;
    return m;
}

static bool Parse(std::string const& text, HireRaidRequest& out, std::string& err)
{
    return ParseHireRaidSpec(HireRaidWords(HireRaidLower(text)), out, err);
}

static void TestRoleCounts()
{
    HireRaidRequest r;
    std::string err;
    CHECK(Parse("3t 12h 24d t6", r, err));
    CHECK(r.entries.size() == 3 && r.tier == 6);
    CHECK(r.entries[0].count == 3 && r.entries[0].role == kHireRoleTank && r.entries[0].classId == 0);
    CHECK(r.entries[1].count == 12 && r.entries[1].role == kHireRoleHealer);
    CHECK(r.entries[2].count == 24 && r.entries[2].role == kHireRoleDps);
    CHECK(Parse("3 tanks 12 heal 24 dps mc", r, err));
    CHECK(r.entries.size() == 3 && r.tier == 3 && r.entries[1].count == 12 && r.entries[2].role == kHireRoleDps);
    CHECK(Parse("2tank 3healer", r, err) && r.entries.size() == 2 && r.tier == 0);
    CHECK(Parse("1t t3 t5", r, err) && r.tier == 5);
}

static void TestExplicit()
{
    HireRaidRequest r;
    std::string err;
    CHECK(Parse("3 warrior:tank 12 priest:healer 19 hunter 5 mage t3", r, err));
    CHECK(r.entries.size() == 4 && r.tier == 3);
    CHECK(r.entries[0].count == 3 && r.entries[0].classId == kHireClassWarrior && r.entries[0].role == kHireRoleTank);
    CHECK(r.entries[2].classId == kHireClassHunter && r.entries[2].role == 0);
    CHECK(Parse("2 pally:healer 1 lock", r, err) && r.entries[0].classId == kHireClassPaladin && r.entries[1].classId == kHireClassWarlock);
}

static void TestErrors()
{
    HireRaidRequest r;
    std::string err;
    CHECK(!Parse("", r, err) && !err.empty());
    CHECK(!Parse("t6", r, err));
    CHECK(!Parse("3", r, err));
    CHECK(!Parse("3 deathknight", r, err));
    CHECK(!Parse("3 warrior:healerish", r, err));
    CHECK(!Parse("0t", r, err));
    CHECK(!Parse("100t", r, err));
    CHECK(!Parse("3x", r, err));
    CHECK(!Parse("banana", r, err));
}

static void TestDefaultsParse()
{
    HireRaidSpread spread;
    CHECK(ParseHireRaidSpread(kHireRaidSpreadDefault, spread));
    CHECK(spread.tank.size() == 3 && spread.healer.size() == 5 && spread.dps.size() == 10);
    std::vector<HireRaidPreset> presets;
    CHECK(ParseHireRaidPresets(kHireRaidPresetsDefault, presets));
    CHECK(presets.size() == 7);
    CHECK(!ParseHireRaidSpread("tank=warrior;healer=priest", spread));
    CHECK(!ParseHireRaidSpread("tank=warrior;healer=priest;dps=banana", spread));
    CHECK(!ParseHireRaidSpread("tank=warrior;healer=priest;dps=mage;dps=rogue", spread));
    CHECK(!ParseHireRaidPresets("mc", presets));
    CHECK(!ParseHireRaidPresets("mc=3x", presets));
    CHECK(ParseHireRaidPresets("", presets) && presets.empty());
}

static void TestPresetExpansion()
{
    std::vector<HireRaidPreset> presets;
    CHECK(ParseHireRaidPresets(kHireRaidPresetsDefault, presets));
    std::vector<std::string> tokens = HireRaidWords("naxx");
    CHECK(ExpandHireRaidPreset(presets, tokens));
    HireRaidRequest r;
    std::string err;
    CHECK(ParseHireRaidSpec(tokens, r, err) && r.tier == 6 && r.entries.size() == 3);
    tokens = HireRaidWords("naxx t4");
    CHECK(ExpandHireRaidPreset(presets, tokens));
    CHECK(ParseHireRaidSpec(tokens, r, err) && r.tier == 4);
    tokens = HireRaidWords("3t 12h");
    CHECK(!ExpandHireRaidPreset(presets, tokens) && tokens.size() == 2);
    // "aq40" is both a tier word and a preset: the preset wins as first token.
    tokens = HireRaidWords("aq40");
    CHECK(ExpandHireRaidPreset(presets, tokens));
    CHECK(ParseHireRaidSpec(tokens, r, err) && r.entries.size() == 3 && r.tier == 5);
    uint32_t total = 0;
    for (HireRaidEntry const& e : r.entries)
        total += e.count;
    CHECK(total == 39);
}

static void TestRoster()
{
    HireRaidSpread spread;
    CHECK(ParseHireRaidSpread(kHireRaidSpreadDefault, spread));
    HireRaidRequest r;
    std::string err;
    std::vector<HireRaidSlot> slots;
    uint32_t requested = 0;

    // Role order is tank, healer, dps whatever the request order.
    CHECK(Parse("24d 12h 3t", r, err));
    CHECK(BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 39, slots, requested, err));
    CHECK(requested == 39 && slots.size() == 39);
    for (size_t i = 0; i < 3; ++i)
        CHECK(slots[i].role == kHireRoleTank);
    CHECK(slots[3].role == kHireRoleHealer && slots[14].role == kHireRoleHealer && slots[15].role == kHireRoleDps);
    // Round-robin: tanks warrior, druid, paladin.
    CHECK(slots[0].classId == kHireClassWarrior && slots[1].classId == kHireClassDruid && slots[2].classId == kHireClassPaladin);
    // Every slot is a legal class for its role.
    for (HireRaidSlot const& s : slots)
        CHECK(CanRole(s.classId, s.role));

    // Cap cuts from the dps end.
    CHECK(BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 10, slots, requested, err));
    CHECK(requested == 39 && slots.size() == 10 && slots[9].role == kHireRoleHealer);
    CHECK(BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 0, slots, requested, err) && slots.empty());

    // Faction without shaman and paladin: the spread skips them.
    uint32_t noPala = AllClasses() & ~(1u << kHireClassPaladin) & ~(1u << kHireClassShaman);
    CHECK(Parse("3t 5h 2d", r, err));
    CHECK(BuildHireRaidRoster(r, spread, noPala, Rules(), 39, slots, requested, err));
    for (HireRaidSlot const& s : slots)
        CHECK(s.classId != kHireClassPaladin && s.classId != kHireClassShaman);
    CHECK(slots[0].classId == kHireClassWarrior && slots[1].classId == kHireClassDruid && slots[2].classId == kHireClassWarrior);

    // Explicit entries: default role, illegal role, missing class, too many.
    CHECK(Parse("3 warrior:tank 12 priest 4 mage", r, err));
    CHECK(BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 39, slots, requested, err));
    CHECK(slots.size() == 19 && slots[0].role == kHireRoleTank && slots[3].role == kHireRoleHealer && slots[15].role == kHireRoleDps);
    CHECK(Parse("2 mage:tank", r, err));
    CHECK(!BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 39, slots, requested, err) && !err.empty());
    CHECK(Parse("2 paladin:tank", r, err));
    CHECK(!BuildHireRaidRoster(r, spread, noPala, Rules(), 39, slots, requested, err));
    CHECK(Parse("30t 20d", r, err));
    CHECK(!BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 39, slots, requested, err) && requested == 50);

    // A role whose spread has nothing for the faction is refused.
    CHECK(ParseHireRaidSpread("tank=paladin;healer=priest;dps=mage", spread));
    CHECK(Parse("1t", r, err));
    CHECK(!BuildHireRaidRoster(r, spread, noPala, Rules(), 39, slots, requested, err));
}

int main()
{
    TestRoleCounts();
    TestExplicit();
    TestErrors();
    TestDefaultsParse();
    TestPresetExpansion();
    TestRoster();
    std::printf("hire raid policy: %d checks passed\n", checks);
    return 0;
}
