// Standalone regression test for issue #900 (`.bot hire raid`): the pure
// composition grammar, spread/preset config parsing and roster builder in
// runtime/HireRaidPolicy.h. Guards
//  - role counts (3t 12h 24d, "3 tanks"), explicit classes (3 warrior:tank),
//    tier words and error cases,
//  - the shipped default spread and presets parse,
//  - preset expansion (a later tier word overrides the preset's),
//  - the roster: role order, round-robin spread, faction filtering, cap cut.
//  - C6: the bound raid's 40th seat ends with the binding (PickSurplusHires: the newest leaves) and a
//    seat hire is rechecked before it joins (RecheckBoundSeat: unbind, commander in group, last seat).
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_raid_policy.cpp -o /tmp/test_hire_raid_policy
//   /tmp/test_hire_raid_policy

#include "../runtime/HireRaidPolicy.h"

#include <algorithm>
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
        return c == kHireClassWarrior || c == kHireClassPaladin || c == kHireClassDruid ||
            c == kHireClassShaman;
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

static void TestLevel()
{
    HireRaidRequest r;
    std::string err;
    // `level=60` in the raid grammar, read as the single hire reads it.
    CHECK(Parse("3t 9h 27d level=60", r, err));
    CHECK(r.level == 60 && r.entries.size() == 3);
    CHECK(Parse("3t 9h 27d t3 lvl:42", r, err) && r.level == 42 && r.tier == 3);
    CHECK(Parse("3t 9h 27d level 55", r, err) && r.level == 55);
    CHECK(Parse("3t 9h 27d", r, err) && r.level == 0);
    // A bad level word refuses like the single hire; one level only.
    CHECK(!Parse("3t 9h 27d level=0", r, err));
    CHECK(!Parse("3t 9h 27d level=abc", r, err));
    CHECK(!Parse("3t 9h 27d level=60 level=59", r, err));
    CHECK(!Parse("3t 9h 27d level", r, err));
    // A preset keeps a level word typed after it.
    std::vector<HireRaidPreset> presets;
    CHECK(ParseHireRaidPresets(kHireRaidPresetsDefault, presets));
    std::vector<std::string> tokens = HireRaidWords("onyxia level=60");
    CHECK(ExpandHireRaidPreset(presets, tokens));
    CHECK(ParseHireRaidSpec(tokens, r, err) && r.level == 60 && r.tier == 3);
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
    // The onyxia hire preset carries tier 3, like the gear preset of the same name.
    tokens = HireRaidWords("onyxia");
    CHECK(ExpandHireRaidPreset(presets, tokens));
    CHECK(ParseHireRaidSpec(tokens, r, err) && r.entries.size() == 3 && r.tier == 3);
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

// C6 (Onyxia kill plan): one more hire into a bound raid. A bound raid's commander
// stands OUTSIDE its group, so the raid holds 40 bots; a player in his own raid keeps
// player + 39 companions.
static HireSeatFacts Seat(uint32_t configMax, uint32_t owned, bool commandsBoundRaid, bool inBoundRaidGroup, uint32_t raidSeatsTaken)
{
    HireSeatFacts f;
    f.configMax = configMax;
    f.owned = owned;
    f.commandsBoundRaid = commandsBoundRaid;
    f.inBoundRaidGroup = inBoundRaidGroup;
    f.boundRaidSeatsTaken = raidSeatsTaken;
    return f;
}

static void TestBoundRaidSeat()
{
    CHECK(kHireBoundRaidMaxCompanions == 40);

    // Only the commander of a bound raid who stands outside its group hires into it.
    CHECK(HireIntoBoundRaid(true, false));
    CHECK(!HireIntoBoundRaid(true, true));   // inside the group: the ordinary raid
    CHECK(!HireIntoBoundRaid(false, false)); // an unbound commander / a player
    CHECK(!HireIntoBoundRaid(false, true));

    // The companion limit: 39 for everyone but the bound commander outside, who gets 40.
    CHECK(HireMaxCompanions(39, false) == 39);
    CHECK(HireMaxCompanions(50, false) == 39);
    CHECK(HireMaxCompanions(4, false) == 4);
    CHECK(HireMaxCompanions(39, true) == 40);
    CHECK(HireMaxCompanions(50, true) == 40);
    CHECK(HireMaxCompanions(38, true) == 38); // an operator who capped lower stays capped
    CHECK(HireMaxCompanions(4, true) == 4);
    CHECK(HireMaxCompanions(0, true) == 0);

    // A player in his own raid: the 40th companion is refused, the 39th allowed.
    HireSeatDecision d = DecideHireSeat(Seat(39, 38, false, false, 0));
    CHECK(d.verdict == HireSeatVerdict::Ok && d.maxCompanions == 39 && !d.intoBoundRaid);
    d = DecideHireSeat(Seat(39, 39, false, false, 0));
    CHECK(d.verdict == HireSeatVerdict::CapReached && d.maxCompanions == 39 && !d.message.empty());
    // The same for a player who is in the bound raid's group (the commander inside it).
    d = DecideHireSeat(Seat(39, 39, true, true, 39));
    CHECK(d.verdict == HireSeatVerdict::CapReached && d.maxCompanions == 39 && !d.intoBoundRaid);
    d = DecideHireSeat(Seat(39, 38, true, true, 39));
    CHECK(d.verdict == HireSeatVerdict::Ok && d.maxCompanions == 39);
    // An unbound commander: 39, whatever the raid-seat fact says.
    d = DecideHireSeat(Seat(39, 39, false, false, 39));
    CHECK(d.verdict == HireSeatVerdict::CapReached && d.maxCompanions == 39);
    d = DecideHireSeat(Seat(39, 3, false, false, 40));
    CHECK(d.verdict == HireSeatVerdict::Ok); // seats of a raid he does not command are not his concern here

    // The bound commander outside the group: the 40th is allowed, a 41st is not.
    d = DecideHireSeat(Seat(39, 38, true, false, 38));
    CHECK(d.verdict == HireSeatVerdict::Ok && d.maxCompanions == 40 && d.intoBoundRaid);
    d = DecideHireSeat(Seat(39, 39, true, false, 39));
    CHECK(d.verdict == HireSeatVerdict::Ok && d.maxCompanions == 40 && d.intoBoundRaid);
    d = DecideHireSeat(Seat(39, 40, true, false, 39));
    CHECK(d.verdict == HireSeatVerdict::CapReached && d.maxCompanions == 40);
    // The operator's lower cap holds for the commander too.
    d = DecideHireSeat(Seat(10, 10, true, false, 10));
    CHECK(d.verdict == HireSeatVerdict::CapReached && d.maxCompanions == 10);

    // A raid already at 40 bots (members plus companions still mustering): refused, with a clear message.
    d = DecideHireSeat(Seat(39, 35, true, false, 40));
    CHECK(d.verdict == HireSeatVerdict::RaidFull);
    CHECK(d.message.find("40") != std::string::npos && d.message.find("raid") != std::string::npos);
    d = DecideHireSeat(Seat(39, 35, true, false, 39));
    CHECK(d.verdict == HireSeatVerdict::Ok);
    d = DecideHireSeat(Seat(39, 20, true, false, 45)); // never more than 40 seats
    CHECK(d.verdict == HireSeatVerdict::RaidFull);

    // The roster: a bound raid may plan 40 slots, everything else is cut at 39.
    HireRaidSpread spread;
    CHECK(ParseHireRaidSpread(kHireRaidSpreadDefault, spread));
    HireRaidRequest r;
    std::string err;
    std::vector<HireRaidSlot> slots;
    uint32_t requested = 0;
    CHECK(Parse("4t 10h 26d", r, err));
    CHECK(!BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 40, slots, requested, err));
    CHECK(requested == 40 && err.find("39") != std::string::npos);
    CHECK(BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 40, slots, requested, err, kHireBoundRaidMaxCompanions));
    CHECK(requested == 40 && slots.size() == 40);
    CHECK(Parse("4t 10h 27d", r, err));
    CHECK(!BuildHireRaidRoster(r, spread, AllClasses(), Rules(), 40, slots, requested, err, kHireBoundRaidMaxCompanions));
    CHECK(requested == 41 && err.find("40") != std::string::npos);
}

// C6 H1: hire 40 into a bound raid, release the binding: 39 stay, the newest one leaves.
static void TestSurplusOnRelease()
{
    uint32_t const cap = HireMaxCompanions(39, false);
    CHECK(cap == 39);
    // Hire order numbers as HireLifecycle::Claim gives them, stored in map order (not hire order).
    std::vector<uint64_t> owned;
    for (uint64_t n = 1; n <= 40; ++n)
        owned.push_back((n * 17) % 41); // 1..40 shuffled, unique
    std::vector<size_t> leave = PickSurplusHires(owned, cap);
    CHECK(leave.size() == 1);
    CHECK(owned[leave[0]] == 40); // the most recently hired
    // Apply the pick: 39 owned stay, the newest is gone.
    std::vector<uint64_t> kept;
    for (size_t i = 0; i < owned.size(); ++i)
        if (std::find(leave.begin(), leave.end(), i) == leave.end())
            kept.push_back(owned[i]);
    CHECK(kept.size() == 39);
    CHECK(std::find(kept.begin(), kept.end(), 40u) == kept.end());
    CHECK(std::find(kept.begin(), kept.end(), 39u) != kept.end());
    // Within the limit: nobody leaves.
    kept.pop_back();
    CHECK(PickSurplusHires(kept, cap).empty());
    CHECK(PickSurplusHires(std::vector<uint64_t>(39, 1), cap).empty());
    // Two over (a lower operator cap): the two newest, newest first.
    std::vector<uint64_t> few = { 5, 9, 2, 7 };
    leave = PickSurplusHires(few, 2);
    CHECK(leave.size() == 2 && few[leave[0]] == 9 && few[leave[1]] == 7);
    CHECK(PickSurplusHires({}, 0).empty());
}

static BoundSeatGrant Grant()
{
    BoundSeatGrant g;
    g.commanderGuid = 100;
    g.leadGuid = 200;
    g.groupId = 7;
    return g;
}

// The binding as it stood when the 40th hire was authorized: unchanged, commander outside, 39 bots seated.
static BoundSeatNow Standing()
{
    BoundSeatNow n;
    n.commanderGuid = 100;
    n.commanderOnline = true;
    n.bound = true;
    n.leadGuid = 200;
    n.groupId = 7;
    n.commanderInGroup = false;
    n.members = 39;
    n.botInGroup = false;
    n.owned = 40;
    n.configMax = 39;
    return n;
}

// C6 H2: a bound-raid seat hire is revalidated right before it joins; anything changed cancels it.
static void TestBoundSeatRecheck()
{
    CHECK(RecheckBoundSeat(Grant(), Standing()).verdict == BoundSeatVerdict::Join);
    CHECK(RecheckBoundSeat(Grant(), Standing()).reason.empty());

    // Mustering + unbind: cancelled.
    BoundSeatNow n = Standing();
    n.bound = false;
    BoundSeatCheck c = RecheckBoundSeat(Grant(), n);
    CHECK(c.verdict == BoundSeatVerdict::Cancel && !c.reason.empty());
    // Mustering + the commander joins the raid group: cancelled.
    n = Standing();
    n.commanderInGroup = true;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Cancel);
    // Another binding (unbind + bind another lead or group): cancelled.
    n = Standing();
    n.leadGuid = 201;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Cancel);
    n = Standing();
    n.groupId = 8;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Cancel);
    n = Standing();
    n.commanderGuid = 101;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Cancel);
    // The commander went offline: cancelled.
    n = Standing();
    n.commanderOnline = false;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Cancel);
    // Over the 40 entitlement, or the entitlement shrank (operator cap lowered): cancelled.
    n = Standing();
    n.owned = 41;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Cancel);
    n = Standing();
    n.configMax = 38;
    n.owned = 39;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Cancel);
    // No free seat: cancelled; a bot already seated in the raid takes no new seat.
    n = Standing();
    n.members = 40;
    c = RecheckBoundSeat(Grant(), n);
    CHECK(c.verdict == BoundSeatVerdict::Cancel && c.reason.find("full") != std::string::npos);
    n.botInGroup = true;
    CHECK(RecheckBoundSeat(Grant(), n).verdict == BoundSeatVerdict::Join);

    // Last-seat race: two pending seat hires for the one free seat. The first passes and joins (the seat is
    // taken), the second fails the same recheck and is cancelled.
    BoundSeatNow raid = Standing();
    raid.owned = 40;
    int joined = 0;
    int cancelled = 0;
    for (int hire = 0; hire < 2; ++hire)
    {
        if (RecheckBoundSeat(Grant(), raid).verdict == BoundSeatVerdict::Join)
        {
            ++joined;
            ++raid.members; // JoinBoundRaid seats it
        }
        else
            ++cancelled;
    }
    CHECK(joined == 1 && cancelled == 1 && raid.members == 40);

    // A normal hire is never rechecked: the mode it was authorized in decides.
    CHECK(!HireNeedsSeatRecheck(HireSeatMode::Normal, false));
    CHECK(HireNeedsSeatRecheck(HireSeatMode::BoundRaidSeat, false));
    CHECK(!HireNeedsSeatRecheck(HireSeatMode::BoundRaidSeat, true)); // already joined: a bound hire now
    CHECK(HireSeatModeFor(true) == HireSeatMode::BoundRaidSeat);
    CHECK(HireSeatModeFor(false) == HireSeatMode::Normal);
}

int main()
{
    TestRoleCounts();
    TestExplicit();
    TestLevel();
    TestErrors();
    TestDefaultsParse();
    TestPresetExpansion();
    TestRoster();
    TestBoundRaidSeat();
    TestSurplusOnRelease();
    TestBoundSeatRecheck();
    std::printf("hire raid policy: %d checks passed\n", checks);
    return 0;
}
