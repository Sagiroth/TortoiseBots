// Standalone check for ai/playerbot/DungeonPlanPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_plan_policy.cpp -o /tmp/test_dungeon_plan_policy && /tmp/test_dungeon_plan_policy
#include "../ai/playerbot/DungeonPlanPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static PlanStep Step(std::uint8_t leg, std::uint16_t seq, PlanStepKind kind, float x = 0, float y = 0, float z = 0)
{
    PlanStep step;
    step.leg = leg;
    step.seq = seq;
    step.kind = kind;
    step.x = x;
    step.y = y;
    step.z = z;
    return step;
}

static void TestKinds()
{
    const char* names[] = {"move", "move_direct", "pull", "boss", "loot", "open", "use_item", "wait_object",
                           "wait_clear", "rest", "ritual"};
    for (const char* name : names)
    {
        PlanStepKind kind = ParsePlanStepKind(name);
        CHECK(kind != PlanStepKind::None);
        CHECK(std::string(PlanStepKindName(kind)) == name);
    }
    CHECK(ParsePlanStepKind("Move") == PlanStepKind::None);
    CHECK(ParsePlanStepKind("") == PlanStepKind::None);
}

static void TestEntries()
{
    std::vector<std::uint32_t> out;
    CHECK(ParsePlanEntries("", out) && out.empty());
    CHECK(ParsePlanEntries("  ", out) && out.empty());
    CHECK(ParsePlanEntries("4851", out) && out.size() == 1 && out[0] == 4851);
    CHECK(ParsePlanEntries("4851, 4852 ,7175", out) && out.size() == 3 && out[2] == 7175);
    CHECK(!ParsePlanEntries("4851,", out));
    CHECK(!ParsePlanEntries("4851,,4852", out));
    CHECK(!ParsePlanEntries("abc", out));
    CHECK(!ParsePlanEntries("0", out));
    CHECK(!ParsePlanEntries("-5", out));
    CHECK(!ParsePlanEntries("1234567890", out));
    std::string many;
    for (int i = 1; i <= 17; ++i)
        many += (i > 1 ? "," : "") + std::to_string(i);
    CHECK(!ParsePlanEntries(many, out));
}

static void TestNormalize()
{
    PlanStep pull = Step(0, 1, PlanStepKind::Pull);
    CHECK(NormalizePlanStep(pull) == PlanStepRefusal::MissingEntry);
    pull.entries = {4851};
    CHECK(NormalizePlanStep(pull) == PlanStepRefusal::None);
    CHECK(pull.radius == kPlanDefaultPackRadius);
    CHECK(pull.timeoutS == kPlanDefaultTimeoutS);

    PlanStep boss = Step(0, 2, PlanStepKind::Boss);
    CHECK(NormalizePlanStep(boss) == PlanStepRefusal::MissingEntry);
    boss.entry = 7228;
    CHECK(NormalizePlanStep(boss) == PlanStepRefusal::None && boss.radius == kPlanDefaultBossRadius);

    PlanStep wait = Step(0, 3, PlanStepKind::WaitObject);
    wait.entry = 124372;
    wait.param = 3;
    CHECK(NormalizePlanStep(wait) == PlanStepRefusal::BadParam);
    wait.param = 1;
    wait.timeoutS = 601;
    CHECK(NormalizePlanStep(wait) == PlanStepRefusal::BadTimeout);
    wait.timeoutS = 600;
    CHECK(NormalizePlanStep(wait) == PlanStepRefusal::None && wait.timeoutS == 600);

    PlanStep clear = Step(0, 4, PlanStepKind::WaitClear);
    CHECK(NormalizePlanStep(clear) == PlanStepRefusal::BadRadius);
    clear.radius = 101.0f;
    CHECK(NormalizePlanStep(clear) == PlanStepRefusal::BadRadius);
    clear.radius = 20.0f;
    CHECK(NormalizePlanStep(clear) == PlanStepRefusal::None);

    PlanStep move = Step(0, 5, PlanStepKind::Move, std::nanf(""), 0, 0);
    CHECK(NormalizePlanStep(move) == PlanStepRefusal::BadPoint);
    PlanStep none = Step(0, 6, PlanStepKind::None);
    CHECK(NormalizePlanStep(none) == PlanStepRefusal::UnknownKind);
    PlanStep item = Step(0, 7, PlanStepKind::UseItem);
    CHECK(NormalizePlanStep(item) == PlanStepRefusal::MissingEntry);
}

static void TestTable()
{
    DungeonPlanTable table;
    CHECK(table.AddPlan("uldaman", 70));
    CHECK(!table.AddPlan("uldaman", 70));
    CHECK(!table.AddPlan("", 70));
    CHECK(!table.AddPlan("zero", 0));
    CHECK(table.AddLeg("uldaman", 1, "revelosh"));
    CHECK(table.AddLeg("uldaman", 0, "baelog"));
    CHECK(!table.AddLeg("uldaman", 0, "again"));
    CHECK(!table.AddLeg("nope", 0, "x"));

    PlanStep pull = Step(1, 2, PlanStepKind::Pull, -281.8f, 163.8f, -47.3f);
    pull.entries = {4851, 4852};
    CHECK(table.AddStep("uldaman", pull) == PlanStepRefusal::None);
    CHECK(table.AddStep("uldaman", Step(1, 1, PlanStepKind::Move, -286.7f, 143.2f, -47.2f)) == PlanStepRefusal::None);
    CHECK(table.AddStep("uldaman", Step(0, 9, PlanStepKind::Move, -236.5f, 54.9f, -46.0f)) == PlanStepRefusal::None);
    CHECK(table.AddStep("uldaman", Step(0, 9, PlanStepKind::Rest)) == PlanStepRefusal::Duplicate);
    CHECK(table.AddStep("other", Step(0, 1, PlanStepKind::Rest)) == PlanStepRefusal::UnknownPlan);
    CHECK(std::string(PlanStepRefusalName(PlanStepRefusal::BadEntries)) == "BadEntries");
    CHECK(std::string(PlanStepRefusalName(PlanStepRefusal::Duplicate)) == "Duplicate");

    // Nothing is readable before the seal.
    CHECK(table.Find("uldaman") == nullptr);
    CHECK(table.PlansOnMap(70).empty());
    table.Seal();
    CHECK(table.AddStep("uldaman", Step(2, 1, PlanStepKind::Rest)) == PlanStepRefusal::Sealed);
    CHECK(!table.AddPlan("late", 1));

    const DungeonPlan* plan = table.Find("uldaman");
    CHECK(plan && plan->map == 70 && plan->steps.size() == 3);
    // Run order: leg, then seq.
    CHECK(plan->steps[0].leg == 0 && plan->steps[0].seq == 9);
    CHECK(plan->steps[1].leg == 1 && plan->steps[1].seq == 1);
    CHECK(plan->steps[2].kind == PlanStepKind::Pull);
    CHECK(plan->LegName(0) == "baelog" && plan->LegName(1) == "revelosh" && plan->LegName(7).empty());
    CHECK(table.PlansOnMap(70).size() == 1 && table.PlansOnMap(1).empty());
    CHECK(table.StepCount() == 3 && table.PlanCount() == 1 && table.All().size() == 1);
}

static void TestLaterOwner()
{
    DungeonPlan plan;
    PlanStep a = Step(0, 1, PlanStepKind::Pull, 0, 0, 0);
    a.entries = {100};
    a.radius = 10;
    PlanStep b = Step(0, 2, PlanStepKind::Move, 50, 0, 0);
    PlanStep c = Step(0, 3, PlanStepKind::Pull, 60, 0, 0);
    c.entries = {100, 101};
    c.radius = 10;
    PlanStep d = Step(0, 4, PlanStepKind::Boss, 100, 0, 0);
    d.entry = 200;
    d.entries = {201};
    d.radius = 30;
    plan.steps = {a, b, c, d};
    // From step 0: a unit of 100 at the second pack belongs to step 2.
    CHECK(OwnedByLaterStep(plan, 0, 100, 62, 0, 0));
    // Its own pack is not "later".
    CHECK(!OwnedByLaterStep(plan, 0, 100, 1, 0, 0));
    // From step 2 the first pack is behind us.
    CHECK(!OwnedByLaterStep(plan, 2, 100, 1, 0, 0));
    // Boss and its listed adds.
    CHECK(OwnedByLaterStep(plan, 0, 200, 110, 0, 0));
    CHECK(OwnedByLaterStep(plan, 2, 201, 90, 0, 0));
    CHECK(!OwnedByLaterStep(plan, 0, 999, 60, 0, 0));
    CHECK(!OwnedByLaterStep(plan, 3, 200, 100, 0, 0));
    // A later step for the other team owns nothing for this party.
    plan.steps[2].flags = kPlanFlagHorde;
    CHECK(OwnedByLaterStep(plan, 0, 100, 62, 0, 0));
    CHECK(OwnedByLaterStep(plan, 0, 100, 62, 0, 0, kPlanFlagHorde));
    CHECK(!OwnedByLaterStep(plan, 0, 100, 62, 0, 0, kPlanFlagAlliance));
}

static void TestLongHops()
{
    DungeonPlan plan;
    plan.steps = {Step(0, 1, PlanStepKind::Move, 0, 0, 0), Step(0, 2, PlanStepKind::Rest),
                  Step(0, 3, PlanStepKind::Move, 50, 0, 0), Step(0, 4, PlanStepKind::MoveDirect, 130, 0, 0)};
    CHECK(CountLongHops(plan, 60.0f) == 1);
    CHECK(CountLongHops(plan, 100.0f) == 0);
}

int main()
{
    TestKinds();
    TestEntries();
    TestNormalize();
    TestTable();
    TestLaterOwner();
    TestLongHops();
    std::cout << "test_dungeon_plan_policy: ok\n";
    return 0;
}
