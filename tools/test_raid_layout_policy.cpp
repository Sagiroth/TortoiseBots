// Standalone tests for the raid layout policy:
// the shape of a bound raid (main tank, off-tanks, tank healers,
// raid healers, subgroups) and the run rules that scale with its size.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_raid_layout_policy.cpp -o /tmp/raid_layout
//   /tmp/raid_layout

#include "../runtime/RaidLayoutPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace TortoiseBots::RaidLayout;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static std::vector<Member> Raid(int tanks, int healers, int dps)
{
    std::vector<Member> m;
    for (int i = 0; i < tanks; ++i) m.push_back({"T" + std::to_string(i), Role::Tank, i == 0});
    for (int i = 0; i < healers; ++i) m.push_back({"H" + std::to_string(i), Role::Healer, false});
    for (int i = 0; i < dps; ++i) m.push_back({"D" + std::to_string(i), Role::Damage, false});
    return m;
}
static Placement const& P(Layout const& l, std::string const& n)
{
    for (auto const& p : l.placements) if (p.name == n) return p;
    std::fprintf(stderr, "missing %s\n", n.c_str()); std::exit(1);
}
static std::vector<int> Sizes(Layout const& l)
{
    std::vector<int> s(kSubgroups, 0);
    for (auto const& p : l.placements) ++s[p.subgroup];
    return s;
}
static uint32_t PeakOf(std::vector<SubgroupMember> const& start, std::vector<SubgroupOp> const& ops)
{
    std::vector<uint8_t> at(64, 0);
    for (SubgroupMember const& m : start)
        at[m.id] = m.current;
    auto count = [&](uint8_t sub) { uint32_t n = 0; for (SubgroupMember const& m : start) n += (at[m.id] == sub); return n; };
    uint32_t peak = 0;
    for (uint8_t s = 0; s < kSubgroups; ++s)
        peak = std::max(peak, count(s));
    for (SubgroupOp const& op : ops)
    {
        if (op.swap)
        {
            uint8_t a = at[op.member], b = at[op.other];
            CHECK(a != b);
            at[op.member] = b;
            at[op.other] = a;
        }
        else
        {
            CHECK(count(op.sub) < kSubgroupSize);
            at[op.member] = op.sub;
        }
        for (uint8_t s = 0; s < kSubgroups; ++s)
            peak = std::max(peak, count(s));
    }
    return peak;
}

static void Placed(std::vector<SubgroupMember> const& start, std::vector<SubgroupOp> const& ops)
{
    std::vector<uint8_t> at(64, 0);
    for (SubgroupMember const& m : start)
        at[m.id] = m.current;
    for (SubgroupOp const& op : ops)
    {
        if (op.swap)
        {
            uint8_t a = at[op.member], b = at[op.other];
            at[op.member] = b;
            at[op.other] = a;
        }
        else
            at[op.member] = op.sub;
    }
    for (SubgroupMember const& m : start)
        CHECK(at[m.id] == m.planned);
}

static void TestPlanSubgroupMoves()
{
    // placed and no subgroup ever holds 6 at any step.
    std::vector<SubgroupMember> raid;
    for (uint64_t id = 1; id <= 4; ++id)
        raid.push_back({id, 0, 1});
    raid.push_back({6, 0, 0});
    for (uint64_t id = 7; id <= 10; ++id)
        raid.push_back({id, 1, 0});
    raid.push_back({12, 1, 1});
    std::vector<SubgroupOp> ops = PlanSubgroupMoves(raid);
    CHECK(!ops.empty());
    Placed(raid, ops);
    CHECK(PeakOf(raid, ops) <= kSubgroupSize);
    // Already placed members get no op.
    std::vector<SubgroupMember> placed{{7, 2, 2}, {8, 2, 2}};
    CHECK(PlanSubgroupMoves(placed).empty());
    // Restore plan: members back to their recorded originals, same guarantee.
    std::vector<SubgroupMember> restore;
    for (uint64_t id = 1; id <= 4; ++id)
        restore.push_back({id, 1, 0});
    restore.push_back({6, 0, 0});
    for (uint64_t id = 7; id <= 10; ++id)
        restore.push_back({id, 0, 1});
    restore.push_back({12, 1, 1});
    std::vector<SubgroupOp> back = PlanSubgroupMoves(restore);
    Placed(restore, back);
    CHECK(PeakOf(restore, back) <= kSubgroupSize);
    // Three full subgroups rotating 0->1->2->0: no direct swap places
    // anyone, the relay through a non-target occupant does.
    std::vector<SubgroupMember> rotation;
    for (uint64_t id = 21; id <= 24; ++id)
        rotation.push_back({id, 0, 1});
    rotation.push_back({25, 0, 0});
    for (uint64_t id = 26; id <= 29; ++id)
        rotation.push_back({id, 1, 2});
    rotation.push_back({30, 1, 1});
    for (uint64_t id = 31; id <= 34; ++id)
        rotation.push_back({id, 2, 0});
    rotation.push_back({35, 2, 2});
    std::vector<SubgroupOp> turn = PlanSubgroupMoves(rotation);
    CHECK(!turn.empty());
    Placed(rotation, turn);
    CHECK(PeakOf(rotation, turn) <= kSubgroupSize);
}
static void TestTankHealerCounts()
{
    CHECK(RaidHealerMinimum(3) == 1 && RaidHealerMinimum(5) == 2 && RaidHealerMinimum(10) == 4);
    CHECK((TankHealerCounts(2, 3) == std::vector<uint32_t>{1, 1}));
    CHECK((TankHealerCounts(3, 5) == std::vector<uint32_t>{1, 1, 1}));
    CHECK((TankHealerCounts(4, 10) == std::vector<uint32_t>{2, 2, 1, 1}));
    CHECK((TankHealerCounts(1, 1) == std::vector<uint32_t>{0}));
    CHECK((TankHealerCounts(2, 12) == std::vector<uint32_t>{2, 2}));
}

static void TestPlanTen()
{
    Layout l = Plan(Raid(2, 3, 5));
    CHECK(l.ok);
    CHECK(P(l, "T0").tankIndex == 0 && P(l, "T0").subgroup == 0);
    CHECK(P(l, "T1").tankIndex == 1 && P(l, "T1").subgroup == 1);
    CHECK(P(l, "H0").healsTank == 0 && P(l, "H0").subgroup == 0);
    CHECK(P(l, "H1").healsTank == 1 && P(l, "H1").subgroup == 1);
    CHECK(P(l, "H2").healsTank == -1 && P(l, "H2").subgroup == 0);
    CHECK((Sizes(l) == std::vector<int>{5, 5, 0, 0, 0, 0, 0, 0}));
}

static void TestPlanTwenty()
{
    Layout l = Plan(Raid(3, 5, 12));
    CHECK(l.ok);
    CHECK(P(l, "H3").healsTank == -1 && P(l, "H3").subgroup == 0);
    CHECK(P(l, "H4").healsTank == -1 && P(l, "H4").subgroup == 1);
    CHECK((Sizes(l) == std::vector<int>{5, 5, 5, 5, 0, 0, 0, 0}));
}

static void TestPlanForty()
{
    Layout l = Plan(Raid(4, 10, 26));
    CHECK(l.ok);
    CHECK(P(l, "H0").healsTank == 0 && P(l, "H1").healsTank == 0);
    CHECK(P(l, "H2").healsTank == 1 && P(l, "H3").healsTank == 1);
    CHECK(P(l, "H4").healsTank == 2 && P(l, "H5").healsTank == 3);
    CHECK(P(l, "H6").healsTank == -1 && P(l, "H9").healsTank == -1);
    CHECK((Sizes(l) == std::vector<int>{5, 5, 5, 5, 5, 5, 5, 5}));
    int raidHealers = 0;
    for (auto const& p : l.placements) raidHealers += (p.role == Role::Healer && p.healsTank < 0) ? 1 : 0;
    CHECK(raidHealers == 4);
}

static void TestPlanErrors()
{
    std::vector<Member> noTank = Raid(2, 3, 5); noTank[0].role = Role::Damage;
    CHECK(!Plan(noTank).ok && Plan(noTank).error == "lead-not-tank");
    std::vector<Member> noLead = Raid(2, 3, 5); noLead[0].lead = false;
    CHECK(Plan(noLead).error == "no-lead");
    CHECK(Plan(Raid(4, 10, 27)).error == "too-many");
}

static void TestRunRules()
{
    CHECK(WipeDeadThreshold(5, false, 3) == 3);
    CHECK(WipeDeadThreshold(10, true, 3) == 4);
    CHECK(WipeDeadThreshold(20, true, 3) == 8);
    CHECK(WipeDeadThreshold(40, true, 3) == 16);
    CHECK(WipeDeadThreshold(6, true, 3) == 3);
    CHECK(CoreDown(2, 2, 3, 0) && !CoreDown(2, 1, 3, 1) && CoreDown(4, 0, 10, 6) && !CoreDown(4, 0, 10, 5));
    CHECK(RaidGathered(20, 18, 0) && !RaidGathered(20, 17, 0) && !RaidGathered(20, 20, 1));
    CHECK(RestFloor({}, 10) == 100);
    CHECK(RestFloor({50, 90, 95, 100, 100, 100, 100, 100, 100, 100}, 10) == 90);
    CHECK(RestFloor({50, 90, 95, 100, 100}, 10) == 50);
}

int main()
{
    TestTankHealerCounts();
    TestPlanTen();
    TestPlanTwenty();
    TestPlanForty();
    TestPlanSubgroupMoves();
    TestPlanErrors();
    TestRunRules();
    std::printf("raid layout policy: %d checks passed\n", checks);
    return 0;
}
