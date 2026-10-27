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
    TestPlanErrors();
    TestRunRules();
    std::printf("raid layout policy: %d checks passed\n", checks);
    return 0;
}
