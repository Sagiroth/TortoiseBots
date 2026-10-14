// Standalone check for runtime/DungeonRunPolicy.h (the run leader's step machine):
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_run_policy.cpp -o /tmp/test_dungeon_run_policy && /tmp/test_dungeon_run_policy
#include "../runtime/DungeonRunPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::DungeonRun;
using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

static PlanStep Step(PlanStepKind kind, float x = 0, float y = 0, float z = 0)
{
    PlanStep s;
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.z = z;
    ai::NormalizePlanStep(s);
    return s;
}

static PlanStep PullStep(float x, std::vector<std::uint32_t> entries, std::uint32_t flags = 0)
{
    PlanStep s;
    s.kind = PlanStepKind::Pull;
    s.x = x;
    s.entries = entries;
    s.flags = flags;
    ai::NormalizePlanStep(s);
    return s;
}

static Facts Party(float x = 0)
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.leadX = x;
    f.carrier = "Tanky";
    return f;
}

static UnitFact Unit(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u;
    u.guid = guid;
    u.entry = entry;
    u.distLead = dist;
    return u;
}

static void TestMove()
{
    DungeonPlan plan;
    plan.name = "t";
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::MoveDirect, 52, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    CHECK(r.phase == Phase::Walk);
    // Progress: no new order.
    f.leadX = 20;
    CHECK(Tick(r, f).empty());
    // Arrived: next step, the direct move goes out at once.
    f.leadX = 48;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1);
    CHECK(Tick(r, f) == "move 52.0 0.0 0.0 direct");
    f.leadX = 52;
    Tick(r, f);
    CHECK(r.state == State::Done);
    CHECK(StatusLine(r) == "TBM:RUN|t|done|2/2|0|move_direct|begin|plan done");
}

static void TestWalkStuckIsBounded()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    int orders = 0;
    for (int i = 0; i < 200 && r.state == State::Running; ++i)
        if (!Tick(r, f).empty())
            ++orders;
    CHECK(r.state == State::Paused);
    CHECK(orders == static_cast<int>(kWalkTries));
    CHECK(r.note.find("stuck") != std::string::npos);
}

// Live bug: a move point just off the mesh; the lead bot stopped 4.6 yd short.
static void TestStalledNearPointCountsAsArrived()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(!Tick(r, f).empty());
    f.leadX = 45.4f;
    for (int i = 0; i < 30 && r.step == 0 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1 && r.note == "arrived");
    // Still farther than kStalledArriveYd: retries, then the stuck pause.
    Run r2 = Start(&plan);
    f.leadX = 0;
    Tick(r2, f);
    f.leadX = 40;
    for (int i = 0; i < 200 && r2.state == State::Running; ++i)
        Tick(r2, f);
    CHECK(r2.state == State::Paused && r2.note.find("stuck") != std::string::npos);
}

// Live bug: a scorpid in the Sentinel pit was pulled, fought and leashed home
// over and over for minutes.
static void TestPullLoopOnOneTargetPauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {7078})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(42, 7078, 10)};
    int pulls = 0;
    for (int i = 0; i < 2000 && r.state == State::Running; ++i)
    {
        f.anyInCombat = false;
        if (Tick(r, f) == "pullback 42")
        {
            ++pulls;
            // Every pull starts a short fight; the unit survives it.
            f.anyInCombat = true;
            for (int k = 0; k < 5; ++k)
                Tick(r, f);
        }
    }
    CHECK(pulls == static_cast<int>(kMaxPullsPerTarget));
    CHECK(r.state == State::Paused && r.note.find("survived 3 pulls") != std::string::npos);
}

static void TestPullGatherAndFight()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851, 4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(11, 4851, 45), Unit(12, 4852, 38)};
    // Scattered party: gather first (the lead holds where it stands).
    f.maxMemberYd = 35;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(r.phase == Phase::Gather);
    CHECK(Tick(r, f).empty());
    f.maxMemberYd = 5;
    // Nearest first.
    CHECK(Tick(r, f) == "pullback 12");
    CHECK(r.phase == Phase::AwaitPull);
    // The fight: no orders while it lasts.
    f.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.note == "fighting");
    // Both died; low mana afterwards: rest, then the step is done.
    f.anyInCombat = false;
    f.units.clear();
    f.minHealerManaPct = 20;
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest);
    f.minHealerManaPct = 85;
    Tick(r, f);
    CHECK(r.state == State::Done || r.step == 1);
}

static void TestUnseenPackIsApproachedNotCleared()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = false;
    // The glue gives the next hop on the navmesh path; one move per hop.
    f.approachKnown = true;
    f.approachX = 35;
    CHECK(Tick(r, f) == "move 35.0 0.0 0.0");
    f.leadX = 10;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && r.state == State::Running);
    // At the hop and still not in sight: the next hop.
    f.leadX = 34;
    CHECK(Tick(r, f).empty());
    f.approachX = 70;
    CHECK(Tick(r, f) == "move 70.0 0.0 0.0");
    f.leadX = 70;
    f.seen = true;
    Tick(r, f);
    CHECK(r.state == State::Done);
    // No path toward it at all: pause, never a straight-line guess.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("no walkable path") == 0);
    // Hops that never bring it in sight are bounded.
    Run r3 = Start(&plan);
    g.approachKnown = true;
    g.approachX = 1;  // within kArriveYd: every hop "arrives" at once
    for (int i = 0; i < 50 && r3.state == State::Running; ++i)
        Tick(r3, g);
    CHECK(r3.state == State::Paused && r3.note.find("approach hops") != std::string::npos);
}

static void TestPullThatStartsNoFightIsPassed()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(7, 4851, 10)};
    int pulls = 0;
    for (int i = 0; i < 120 && r.state == State::Running && r.step == 0; ++i)
    {
        std::string order = Tick(r, f);
        if (order == "pullback 7")
            ++pulls;
    }
    CHECK(pulls == static_cast<int>(kPullTriesPerUnit));
    // The unit is passed, so the step ends (the unit is alive: it is not "cleared", it is passed).
    CHECK(r.state == State::Done);
}

static void TestAmbushWalksThrough()
{
    DungeonPlan plan;
    plan.steps = {PullStep(20, {4850}, ai::kPlanFlagAmbush)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");
    f.leadX = 19;
    Tick(r, f);
    CHECK(r.state == State::Done);
}

static void TestBossKillOrderAndTeamGate()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 10);
    boss.entry = 6906;
    boss.entries = {6907, 6908};
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(1, 6906, 5), Unit(2, 6908, 9), Unit(3, 6907, 12)};
    // First listed add (6907) before the nearer boss.
    CHECK(Tick(r, f) == "pullback 3");
    // Not hostile (an Alliance party and the Lost Dwarves): left alone, step done.
    Run r2 = Start(&plan);
    for (UnitFact& u : f.units)
        u.hostile = false;
    Tick(r2, f);
    CHECK(r2.step == 1 && r2.note.find("friendly") != std::string::npos);
}

static void TestHopPullsNearUnitsFirst()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.hopUnits = {Unit(9, 4852, 35)};
    CHECK(Tick(r, f) == "pullback 9");
    f.hopUnits.clear();
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
}

static void TestWipeAndLeadDeathPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.note.find("3 dead") == 0);
    // Resume after the regroup: the step starts over.
    f.dead = 0;
    f.alive = 5;
    CHECK(Resume(r));
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("lead bot is dead") != std::string::npos);
}

static void TestFightCeiling()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("fight lasted") != std::string::npos);
}

static void TestTaskSteps()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 123329;
    loot.item = 7740;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    PlanStep wait = Step(PlanStepKind::WaitObject, 7, 8, 9);
    wait.entry = 124372;
    wait.param = 1;
    wait.timeoutS = 60;
    plan.steps = {loot, use, open, wait};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.minHealthPct = 100;
    CHECK(Tick(r, f) == "loot entry:123329@1.0,2.0,3.0 Tanky 7740");
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    // Task over without the item: pause, naming it.
    f.carrierBusy = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("7740") != std::string::npos);
    // With the item it goes on.
    CHECK(Resume(r));
    f.carrierHasItem = true;
    Tick(r, f);
    CHECK(r.step == 1);
    // use_item: the staff is not there yet; the medallion is.
    f.carrierHasItem = false;
    f.carrierHasEntryItem = true;
    CHECK(Tick(r, f) == "use 7740 Tanky");
    f.carrierHasItem = true;
    Tick(r, f);
    CHECK(r.step == 2);
    CHECK(Tick(r, f) == "open entry:124371@4.0,5.0,6.0 Tanky");
    Tick(r, f);
    CHECK(r.step == 3);
    // The seal opens after a while; then the plan is done.
    f.objectState = 0;
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    CHECK(r.step == 3 && r.state == State::Running);
    f.objectState = 1;
    Tick(r, f);
    CHECK(r.state == State::Done);
}

static void TestOpenWithoutKeyAndTaskError()
{
    DungeonPlan plan;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    plan.steps = {open};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("lock needs it") != std::string::npos);
    CHECK(Resume(r));
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.carrierBusy = true;
    f.taskReply = 2;
    f.taskText = "locked: needs key";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("locked") != std::string::npos);
}

static void TestWaitObjectTimeoutIsBounded()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject);
    wait.entry = 5;
    wait.timeoutS = 600;
    plan.steps = {wait};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::uint32_t ticks = 0;
    while (r.state == State::Running && ticks < 2000)
    {
        Tick(r, f);
        ++ticks;
    }
    // The wait's own timeout ends it, not the generic step ceiling (300 s).
    CHECK(r.state == State::Paused && ticks >= 600 && ticks <= 602);
    CHECK(r.note.find("not found") != std::string::npos);
}

static void TestUnsupportedPausesAndSkip()
{
    DungeonPlan plan;
    PlanStep ritual = Step(PlanStepKind::Ritual);
    ritual.entry = 1;
    plan.steps = {ritual, Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("not supported yet") != std::string::npos);
    CHECK(!Supported(PlanStepKind::Ritual) && Supported(PlanStepKind::Move));
    CHECK(Skip(r));
    CHECK(r.state == State::Paused && r.step == 1);
    CHECK(Resume(r));
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
}

static void TestPlayerOrderAndRefusalPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    CHECK(PauseForPlayer(r, "cmd attack"));
    CHECK(!PauseForPlayer(r, "cmd attack"));
    CHECK(r.state == State::Paused && r.note.find("cmd attack") != std::string::npos);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
    CHECK(Resume(r));
    OrderRefused(r, "rest", "failed: No bot of the group accepted the order.");
    CHECK(r.state == State::Running && r.note.find("rest refused") == 0);
    OrderRefused(r, "move 10.0 0.0 0.0", "no-path: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Paused);
    // The status line keeps the protocol's field count.
    r.note = "a|b";
    std::string line = StatusLine(r);
    int bars = 0;
    for (char c : line)
        bars += c == '|';
    CHECK(bars == 7);
}

static void TestStepIdleCeiling()
{
    DungeonPlan plan;
    PlanStep clear = Step(PlanStepKind::WaitClear, 0, 0, 0);
    clear.radius = 20;
    ai::NormalizePlanStep(clear);
    plan.steps = {clear};
    CHECK(StepIdleCapMs(plan.steps[0]) == kStepIdleCapMs);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.hostilesNear = 2;
    for (int i = 0; i < 100 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("hostiles still near") != std::string::npos);
}

static void TestTeamFlags()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {4851}, ai::kPlanFlagHorde), Step(PlanStepKind::Rest)};
    Facts f = Party(0);
    f.team = ai::kPlanFlagAlliance;
    // Not seen yet: a pack for the other team is skipped without a walk.
    Run r = Start(&plan);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note.find("other team") != std::string::npos);
    // Seen and nothing hostile there: skipped too.
    Run r2 = Start(&plan);
    f.seen = true;
    f.units = {Unit(5, 4851, 10)};
    f.units[0].hostile = false;
    Tick(r2, f);
    CHECK(r2.step == 1);
    // Seen and hostile anyway: fought like any pack.
    Run r3 = Start(&plan);
    f.units[0].hostile = true;
    CHECK(Tick(r3, f) == "pullback 5");
    // The party's own team, or a step for both: normal.
    Run r4 = Start(&plan);
    f.team = ai::kPlanFlagHorde;
    CHECK(Tick(r4, f) == "pullback 5");
    CHECK(ai::StepForTeam(plan.steps[0], 0) && ai::StepForTeam(plan.steps[1], ai::kPlanFlagAlliance));
}

// Live bug (first bots-only Uldaman run): the Lost Dwarves (boss flagged
// horde) were skipped for an Alliance party before anybody looked at them.
static void TestOtherTeamBossIsLookedAtAndFoughtUnlessFriendly()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 100);
    boss.entry = 6906;
    boss.entries = {6907, 6908};
    boss.flags = ai::kPlanFlagHorde;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Facts f = Party(0);
    f.team = ai::kPlanFlagAlliance;
    // Not in sight: walk to it, never skip.
    Run r = Start(&plan);
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.step == 0);
    // In sight, the dwarves neutral or hostile: fought, Eric first.
    f.seen = true;
    f.leadX = 70;
    f.units = {Unit(1, 6906, 30), Unit(3, 6907, 35)};
    Run r2 = Start(&plan);
    CHECK(Tick(r2, f) == "pullback 3");
    // In sight and all friendly: skipped, saying why.
    for (UnitFact& u : f.units)
        u.hostile = false;
    Run r3 = Start(&plan);
    Tick(r3, f);
    CHECK(r3.step == 1 && r3.note.find("friendly") != std::string::npos);
    // In sight and gone: the boss is down.
    f.units.clear();
    Run r4 = Start(&plan);
    Tick(r4, f);
    CHECK(r4.step == 1 && r4.note == "boss down");
}

static void TestEmptyPlan()
{
    DungeonPlan plan;
    Run r = Start(&plan);
    CHECK(r.state == State::Done);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
}

int main()
{
    TestMove();
    TestWalkStuckIsBounded();
    TestStalledNearPointCountsAsArrived();
    TestPullLoopOnOneTargetPauses();
    TestPullGatherAndFight();
    TestUnseenPackIsApproachedNotCleared();
    TestPullThatStartsNoFightIsPassed();
    TestAmbushWalksThrough();
    TestBossKillOrderAndTeamGate();
    TestHopPullsNearUnitsFirst();
    TestWipeAndLeadDeathPause();
    TestFightCeiling();
    TestTaskSteps();
    TestOpenWithoutKeyAndTaskError();
    TestWaitObjectTimeoutIsBounded();
    TestUnsupportedPausesAndSkip();
    TestPlayerOrderAndRefusalPause();
    TestStepIdleCeiling();
    TestTeamFlags();
    TestOtherTeamBossIsLookedAtAndFoughtUnlessFriendly();
    TestEmptyPlan();
    std::cout << "test_dungeon_run_policy: ok\n";
    return 0;
}
