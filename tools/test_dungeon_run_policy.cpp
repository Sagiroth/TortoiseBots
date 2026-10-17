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
    f.seen = true;  // the step's point within kSeeYd; tests of far steps clear it
    return f;
}

// A living member `yd` from the lead bot, standing on the x axis at that
// distance (the stand-still watch reads its position).
static MemberFact Mem(const std::string& name, float yd, float dz = 0.0f)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.dz = dz;
    m.x = yd;
    return m;
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
    plan.steps = {PullStep(10, {7078}), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(42, 7078, 10)};
    int pulls = 0;
    for (int i = 0; i < 2000 && r.state == State::Running && r.step == 0; ++i)
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
    // Left as out of reach: the step ends "skipped, not cleared", the run goes on.
    CHECK(r.state == State::Running && r.step == 1 && r.note.find("skipped, not cleared") != std::string::npos);
    CHECK(r.unreachable.count(42) == 1);
}

// Live: ten Cleft Scorpids in a pit, each pull fought one that leashed home.
static void TestOutOfReachPackIsSkippedNotCleared()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {7078}), Step(PlanStepKind::Move, 30)};
    Run r = Start(&plan);
    Facts f = Party(0);
    for (std::uint64_t g = 1; g <= 10; ++g)
        f.units.push_back(Unit(g, 7078, 10 + g));
    int pulls = 0;
    for (int i = 0; i < 3000 && r.state == State::Running && r.step == 0; ++i)
    {
        f.anyInCombat = false;
        if (Tick(r, f).compare(0, 9, "pullback ") == 0)
        {
            ++pulls;
            f.anyInCombat = true;
            for (int k = 0; k < 5; ++k)
                Tick(r, f);
        }
    }
    CHECK(pulls == static_cast<int>(kFruitlessPulls));
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note.find("skipped, not cleared: 10 units") != std::string::npos);
    // Never pulled again on the way (hop pulls skip them too).
    f.units.clear();
    f.hopUnits = {Unit(3, 7078, 13)};
    CHECK(Tick(r, f) == "move 30.0 0.0 0.0");
}

// Live cascade: no Shaft -> no Staff -> Keystone -> Seal wait, each failing.
static void TestMissingItemPausesOnce()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    PlanStep wait = Step(PlanStepKind::WaitObject, 7, 8, 9);
    wait.entry = 124372;
    wait.param = 1;
    plan.steps = {loot, use, open, wait, Step(PlanStepKind::Move, 1)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0);
    // The corpse is gone: the order is refused. One pause, naming the item.
    OrderRefused(r, order, "not-found: No spawned game object or corpse of that entry is near the point");
    CHECK(r.state == State::Paused && r.note.find("item 7741 missing") != std::string::npos);
    CHECK(Skip(r));
    CHECK(r.missing.count(7741) == 1 && r.state == State::Running && r.step == 1);
    // The medallion is there, but its use needs the Shaft: refused, no pause.
    f.carrierHasEntryItem = true;
    order = Tick(r, f);
    CHECK(order == "use 7740 Tanky");
    OrderRefused(r, order, "busy: The cast failed (spell check 40).");
    CHECK(r.state == State::Running && r.step == 2 && r.missing.count(7733) == 1);
    // No staff: the Keystone and the Seal wait are left out with notes.
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 3 && r.note.find("left out") != std::string::npos);
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 4 && r.note.find("open before it") != std::string::npos);
}

// After a wipe: resume revives the party and gathers it before the step.
static void TestResumeRegroups()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    r.autoRegroups = kAutoRegroupsPerStep; // the regroups by itself are used up: it pauses
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(Resume(r));
    CHECK(r.autoRegroups == 0);
    CHECK(Tick(r, f) == "revive party");
    CHECK(r.state == State::Running && r.note.find("regroup") == 0);
    CHECK(Tick(r, f).empty());
    // Up again but spread out: gather on the lead bot, then the step.
    f.dead = 0;
    f.alive = 5;
    f.maxMemberYd = 80;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // Revive that never works is bounded.
    Run r2 = Start(&plan);
    r2.autoRegroups = kAutoRegroupsPerStep;
    Facts g = Party(0);
    g.dead = 3;
    g.alive = 2;
    Tick(r2, g);
    Resume(r2);
    for (int i = 0; i < 400 && r2.state == State::Running; ++i)
        Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("still dead") != std::string::npos);
}

// A far move point (the lead came back from the entrance): navmesh hops.
static void TestFarMoveWalksBackInHops()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.leadX = 40;
    CHECK(Tick(r, f).empty());
    f.seen = true;
    CHECK(Tick(r, f) == "move 200.0 0.0 0.0");
}

// The Sentinel ramp: a direct step gathers the party first.
static void TestDirectStepGathersFirst()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 30;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(Tick(r, f).empty());
    f.maxMemberYd = 4;
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
}

// `.bot run skip` while running goes on running.
static void TestSkipWhileRunning()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(Skip(r));
    CHECK(r.state == State::Running && r.step == 1 && !r.regroup);
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
}

// Live: `cmd move -159.5 281.6 -49.1` refused unreachable (a point up a
// ledge). The glue finds a reachable point near it; the step walks there.
static void TestUnreachableMoveDetours()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    CHECK(order == "move 50.0 0.0 0.0");
    // Too far from the plan's point: not a stand-in.
    CHECK(!UseDetour(r, 30, 0, 0));
    CHECK(UseDetour(r, 44, 2, 0));
    CHECK(r.state == State::Running && r.detour);
    // Only once per step.
    CHECK(!UseDetour(r, 45, 0, 0));
    CHECK(Tick(r, f) == "move 44.0 2.0 0.0");
    f.leadX = 44;
    f.leadY = 2;
    Tick(r, f);
    CHECK(r.step == 1 && r.note.find("nearest reachable point") != std::string::npos);
    // The detour is per step.
    CHECK(!r.detour);
    // Not for a step that is not a move.
    CHECK(!UseDetour(r, 1, 1, 1));
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
    g.seen = false;
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

// Ragefire 19z13, trogg cave pull 13: the pack is 70 yd down a ramp. The hop
// toward it was broken off by the pull's gather as soon as the pack came in
// sight, while the anchor was still the previous stand 11 yd higher (more than
// kStragglerDzYd): the gather "went back" to it, the next hop came forward
// again, eight times, then the hop cap. The hop is walked to its end first, and
// its point becomes the anchor.
static void TestFarPackHopIsNotUndoneByTheGatherBack()
{
    DungeonPlan plan;
    plan.steps = {PullStep(-238.0f, {11318})};
    plan.steps[0].y = -36.0f;
    plan.steps[0].z = -56.4f;
    Run r = Start(&plan);
    r.restDue = false;
    r.anchorKnown = true; // the previous step's stand, at the top of the ramp
    r.anchorX = -169.1f; r.anchorY = -34.9f; r.anchorZ = -40.9f;
    Facts f = Party(-169.1f);
    f.leadY = -34.9f; f.leadZ = -40.9f;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -205; f.approachY = -34; f.approachZ = -52;
    CHECK(Tick(r, f) == "move -205.0 -34.0 -52.0");
    // Half way down the pack comes in sight (60 yd): the hop goes on.
    f.leadX = -200.7f; f.leadY = -35.6f; f.leadZ = -48.5f;
    f.seen = true;
    f.units = {Unit(1, 11318, 38)};
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Walk && r.note == "approaching the pack");
    // At the hop's point: it is the anchor and the pull goes out, no gather back.
    f.leadX = -205; f.leadY = -34; f.leadZ = -52;
    f.units = {Unit(1, 11318, 34)};
    CHECK(Tick(r, f) == "pullback 1");
    CHECK(r.anchorX == -205.0f && r.anchorZ == -52.0f);
    CHECK(r.note.find("gathering back") == std::string::npos);
    // A unit of the pack within kHopClearYd: no walking on, the pull goes from
    // here and the anchor stays (the party is not gathered next to the pack).
    Run q = Start(&plan);
    q.restDue = false;
    q.anchorKnown = true;
    q.anchorX = -169.1f; q.anchorY = -34.9f; q.anchorZ = -40.9f;
    Facts g = Party(-169.1f);
    g.leadY = -34.9f; g.leadZ = -40.9f;
    g.seen = false;
    g.approachKnown = true;
    g.approachX = -205; g.approachY = -34; g.approachZ = -52;
    Tick(q, g);
    g.leadX = -222; g.leadY = -34; g.leadZ = -54;
    g.seen = true;
    g.maxMemberYd = 40;
    g.units = {Unit(1, 11318, 17)};
    CHECK(Tick(q, g).compare(0, 5, "move ") == 0);
    CHECK(q.note.find("gathering back at -169.1 -34.9 -40.9") == 0);
    CHECK(q.anchorX == -169.1f);
    // A player's move next to the visible pack, the party behind: the anchor stays too.
    Run m = Start(&plan);
    m.restDue = false;
    m.anchorKnown = true;
    m.anchorX = -169.1f; m.anchorY = -34.9f; m.anchorZ = -40.9f;
    Facts h = g;
    h.units = {Unit(1, 11318, 15)};
    Tick(m, h);
    CHECK(m.anchorX == -169.1f && m.anchorZ == -40.9f);
}

// A hop loop that really gets nowhere still pauses after kMaxApproachHops, with
// how near it got; a long way walked in hops that do get nearer is not capped.
static void TestApproachHopCapCountsHopsWithoutProgress()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1000, {1})};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    for (int hop = 1; hop <= 3 * static_cast<int>(kMaxApproachHops); ++hop)
    {
        f.approachX = f.leadX + 40;
        CHECK(Tick(r, f) == "move " + Num(f.approachX) + " 0.0 0.0");
        f.leadX = f.approachX; // the hop is walked, 40 yd nearer
        CHECK(Tick(r, f).empty());
        CHECK(r.state == State::Running);
    }
    // The lead bot is pushed back each time: no progress, 8 hops, the pause.
    Run s = Start(&plan);
    s.restDue = false;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    for (int i = 0; i < 100 && s.state == State::Running; ++i)
    {
        g.approachX = 40;
        Tick(s, g);
        g.leadX = 40;
        Tick(s, g);
        g.leadX = 0; // dragged back
    }
    CHECK(s.state == State::Paused);
    CHECK(s.note.find("8 approach hops did not bring the party within 60.0 yd of the pack") == 0);
    CHECK(s.note.find("nearest it got: 1000.0 yd") != std::string::npos);
}

// Uldaman 19z13, Revelosh loot step 20: the lead bot may not loot the corpse
// (quest item 7741, another member's turn). The step is done once any party bot
// carries the item; else the bot whose turn it is, then each living bot, is
// tried before the run pauses.
static void TestLootStepIsDoneWhenAnyPartyBotHasTheItem()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    // Before the order.
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5)};
    f.partyHasItem = true;
    f.itemHolder = "Healy";
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note.find("Healy already carries item 7741") != std::string::npos);
    // After a refusal (Healy took the item while the order was out).
    Run q = Start(&plan);
    Facts g = Party(0);
    g.members = f.members;
    std::string const order = Tick(q, g);
    CHECK(order.compare(0, 5, "loot ") == 0);
    OrderRefused(q, order, "failed: This corpse has nothing this bot may loot under the group's loot rules");
    CHECK(q.state == State::Running && q.step == 0);
    g.partyHasItem = true;
    g.itemHolder = "Healy";
    CHECK(Tick(q, g).empty());
    CHECK(q.step == 1 && q.state == State::Running);
}

static void TestLootRefusalTriesTheTurnBotThenEachBot()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    char const* rights = "failed: This corpse has nothing this bot may loot under the group's loot rules (another "
        "member's turn, already looted, or not tapped by the group)";
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6)};
    f.corpseChecked = true;
    f.corpseHasItem = true; // still on the corpse
    std::string order = Tick(r, f);
    CHECK(order.find(" Tanky 7741") != std::string::npos);
    OrderRefused(r, order, rights);
    CHECK(r.state == State::Running);
    // The corpse's round-robin turn is Stabby's: he is tried first.
    f.lootTurn = "Stabby";
    order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Stabby 7741") != std::string::npos);
    CHECK(r.state == State::Running && r.taskBot == "Stabby");
    OrderRefused(r, order, rights);
    // Then each bot not tried yet.
    order = Tick(r, f);
    CHECK(order.find(" Healy 7741") != std::string::npos);
    // His task is accepted and the item lands in his bags: done.
    f.taskReply = 1;
    f.partyHasItem = true;
    f.itemHolder = "Healy";
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);

    // Everybody refused: one pause naming who was tried and the skip hint.
    Run p = Start(&plan);
    Facts g = f;
    g.partyHasItem = false;
    g.taskReply = 0;
    g.lootTurn.clear();
    for (int i = 0; i < 3; ++i)
    {
        order = Tick(p, g);
        CHECK(order.compare(0, 5, "loot ") == 0);
        OrderRefused(p, order, rights);
    }
    Tick(p, g);
    CHECK(p.state == State::Paused);
    CHECK(p.note.find("(tried Tanky Healy Stabby)") != std::string::npos);
    CHECK(p.note.find("item 7741 missing") != std::string::npos);

    // The item is gone from the corpse and in no bag: no point in trying the
    // rest; the pause says so plainly.
    Run e = Start(&plan);
    Facts h = f;
    h.partyHasItem = false;
    h.taskReply = 0;
    h.corpseHasItem = false;
    order = Tick(e, h);
    OrderRefused(e, order, rights);
    Tick(e, h);
    CHECK(e.state == State::Paused);
    CHECK(e.note.find("item 7741 not on corpse and not in any bag") != std::string::npos);
    CHECK(e.note.find("item 7741 missing") != std::string::npos);
    // A refusal that is not about the loot rules still pauses at once.
    Run n = Start(&plan);
    order = Tick(n, f);
    OrderRefused(n, order, "not-found: No corpse of that entry is near the point");
    CHECK(n.state == State::Paused && n.note.find("not-found") != std::string::npos);
}

// open / use_item go to the bot that carries every input of the step: its item
// (or the item to use) and `param`. Loot another bot keeps for other reasons
// does not count.
static void TestItemCarrierIsPickedFromTheStepsOwnInputs()
{
    std::vector<BagFact> bags(3);
    bags[0].name = "Tanky";
    bags[1].name = "Healy";
    bags[2].name = "Stabby";
    std::string split;
    // The Medallion use: 7740, and the Shaft 7741 in `param`.
    std::vector<std::uint32_t> inputs = {7740, 7741};
    bags[0].held = {7740};
    bags[1].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Healy");
    // The lead bot carries all: it keeps the step.
    bags[0].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    // The Keystone: only its key matters; two bots carry unrelated loot (the
    // glue lists only the step's own inputs, so those do not show up at all).
    std::vector<std::uint32_t> key = {7733};
    bags[0].held.clear();
    bags[1].held.clear();
    bags[2].held = {7733};
    CHECK(PickItemCarrier(bags, key, "Tanky", &split) == "Stabby");
    CHECK(split.empty());
    // Split: the lead bot stays the carrier and the note says who holds what.
    bags[0].held = {7740};
    bags[1].held = {7741};
    bags[2].held.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    CHECK(split == "Tanky holds 7740; Healy holds 7741");
    // Nobody has the first input: no split note (the missing-item rules apply).
    split.clear();
    bags[0].held.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky" && split.empty());
}

// The use_item pauses, naming who holds what, when the inputs are split.
static void TestUseItemPausesOnSplitInputs()
{
    DungeonPlan plan;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    use.param = 7741;
    plan.steps = {use};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.carrierHasEntryItem = true;
    f.itemsSplit = "Tanky holds 7740; Healy holds 7741";
    Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("no bot carries every item it needs: Tanky holds 7740; Healy holds 7741") != std::string::npos);
    // Healy carries both: the glue names her the carrier and the use goes to her.
    Run q = Start(&plan);
    Facts g = Party(0);
    g.carrier = "Healy";
    g.carrierHasEntryItem = true;
    CHECK(Tick(q, g) == "use 7740 Healy");
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

// Uldaman rerun pull 58 (build 19z11): the fight ended with the priest and the
// mage dead (fewer than kWipeDead) and the next pullback went out at once. Now
// the dead are revived and the party gathers before the pull.
static void TestFewDeadReviveBeforePull()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 2;
    f.alive = 3;
    // No pull while two are dead: the regroup's revive goes out instead.
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for 2 dead: reviving and gathering") == 0);
    CHECK(Tick(r, f) == "revive party");
    CHECK(Tick(r, f).empty());
    // Up again and gathered: the pull follows.
    f.dead = 0;
    f.alive = 5;
    std::string order;
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 21");
    // In a fight with one dead nothing changes: the fight is the bots' own.
    Run r2 = Start(&plan);
    f.dead = 1;
    f.alive = 4;
    f.anyInCombat = true;
    CHECK(Tick(r2, f).empty());
    CHECK(!r2.regroup && r2.note.find("fighting") == 0);
}

// Review of #1055: the 1-2 dead rule stays out of scripted events (a quiet
// gap between waves is not the end of the fight).
static void TestFewDeadRuleSkipsEvents()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    r.eventOn = true;
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(!r.regroup);
    // A move leading into a hold counts as the event too.
    DungeonPlan holdPlan;
    holdPlan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Hold, 40)};
    Run h = Start(&holdPlan);
    Tick(h, f);
    CHECK(!h.regroup);
}

// Review of #1055: a fight during a regroup (a pack met on the corpse run)
// gets the combat block: fight note, fight ceiling, and a third death reaches
// the regroup by itself.
static void TestRegroupFightsFirst()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    BeginRegroup(r);
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.note == "fighting" && r.fighting);
    // The fight ceiling still holds inside the regroup.
    for (std::uint32_t ms = 0; ms <= kFightCapMs && r.state == State::Running; ms += kTickMs)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("a fight lasted") == 0);
    // Three dead in the regroup's fight: the regroup by itself follows the fight.
    Run w = Start(&plan);
    BeginRegroup(w);
    f.dead = 1;
    f.alive = 4;
    CHECK(Tick(w, f).empty());
    f.dead = 3;
    f.alive = 2;
    CHECK(Tick(w, f).empty());
    CHECK(w.note.find("3 dead: fighting on") == 0);
    f.anyInCombat = false;
    Tick(w, f);
    CHECK(w.regroup && w.autoRegroups == 1);
}

// Review of #1055: a living rezzer gets kPartyRezWaitMs to raise the dead
// before `revive party` releases them; without one the revive goes out at once.
static void TestRegroupWaitsForPartyRez()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 1;
    f.alive = 4;
    f.rezzer = true;
    CHECK(Tick(r, f).empty()); // the regroup starts
    std::uint32_t waited = 0;
    std::string order;
    while (order.empty() && waited < 60000)
    {
        order = Tick(r, f);
        waited += kTickMs;
        if (order.empty())
            CHECK(r.note.find("a resurrection in the party first") != std::string::npos);
    }
    CHECK(order == "revive party" && waited >= kPartyRezWaitMs && waited <= kPartyRezWaitMs + 2 * kTickMs);
    // Raised in time: no revive at all, the pull follows.
    Run q = Start(&plan);
    Tick(q, f);
    Tick(q, f);
    f.dead = 0;
    f.alive = 5;
    order.clear();
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(q, f);
    CHECK(order == "pullback 21");
    // No rezzer: the revive goes out on the regroup's first tick.
    Run n = Start(&plan);
    f.dead = 1;
    f.alive = 4;
    f.rezzer = false;
    Tick(n, f);
    CHECK(Tick(n, f) == "revive party");
}

// The regroup's revive is bounded: still dead after kRegroupReviveMs pauses.
static void TestRegroupRevivePauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 2;
    f.alive = 3;
    BeginRegroup(r);
    int revives = 0;
    for (std::uint32_t ms = 0; ms <= kRegroupReviveMs + kTickMs && r.state == State::Running; ms += kTickMs)
        if (Tick(r, f) == "revive party")
            ++revives;
    CHECK(r.state == State::Paused && r.note.find("2 still dead after 240 s") != std::string::npos);
    CHECK(revives >= 4); // the first, then again every 60 s
}

// Review of #1055 (owner decision): a bot eating or drinking after the fight
// is resting. Uldaman pulls 17 and 61 (19z11): the tank sat eating at 82 %
// (above the 80 % start line) and the pullback found it seated.
static void TestMealWaitBeforePull()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(31, 4852, 54.7f)};
    f.minHealthPct = 82;
    f.eating = 1;
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Rest && r.note.find("1 eating") != std::string::npos);
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).find("pullback") == std::string::npos);
    // The meal ends (the tank stood up full): the pull goes out.
    f.eating = 0;
    f.minHealthPct = 100;
    std::string order;
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 31");
    // Eating on and on: kRestCapMs at most, then the pull; once per fight.
    Run c = Start(&plan);
    f.eating = 1;
    f.minHealthPct = 82;
    order.clear();
    std::uint32_t ms = 0;
    for (; ms <= kRestCapMs + 5 * kTickMs && order.find("pullback") == std::string::npos; ms += kTickMs)
        order = Tick(c, f);
    CHECK(order == "pullback 31" && ms >= kRestCapMs);
    // Eating starts a tick after the fight ends (bots sit on their next AI
    // tick), as at pull 17: "pack cleared" on one tick, the next step's pull on
    // the next. The wait still holds that pull.
    DungeonPlan two;
    two.steps = {PullStep(40, {4851}), PullStep(40, {4852})};
    Run late = Start(&two);
    f.eating = 0;
    f.minHealthPct = 100;
    f.units = {Unit(30, 4851, 20)};
    CHECK(Tick(late, f) == "pullback 30");
    f.anyInCombat = true;
    Tick(late, f);
    f.anyInCombat = false;
    f.units.clear();
    f.minHealthPct = 82;
    std::string first;
    for (int i = 0; i < 3 && late.step == 0; ++i)
        first = Tick(late, f);
    CHECK(late.step == 1 && first.find("pullback") == std::string::npos);
    f.units = {Unit(31, 4852, 54.7f)};
    f.eating = 1;
    CHECK(Tick(late, f).empty());
    CHECK(late.phase == Phase::Rest && late.restForEating);
    // The same step: the fight ends with the party scattered (a gather goes
    // out, nobody eats yet), and the tank sits down on the next tick.
    Run same = Start(&plan);
    f.eating = 0;
    f.minHealthPct = 100;
    f.units = {Unit(30, 4852, 20), Unit(31, 4852, 54.7f)};
    CHECK(Tick(same, f) == "pullback 30");
    f.anyInCombat = true;
    Tick(same, f);
    f.anyInCombat = false;
    f.units = {Unit(31, 4852, 54.7f)};
    f.minHealthPct = 82;
    f.maxMemberYd = 35;
    CHECK(Tick(same, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    f.eating = 1;
    CHECK(Tick(same, f).empty());
    CHECK(same.phase == Phase::Rest && same.restForEating);
    // One budget per fight: after kRestCapMs of meal wait the pull goes out
    // even if the bot sits down again.
    order.clear();
    for (std::uint32_t t = 0; t <= kRestCapMs + 5 * kTickMs && order.find("pullback") == std::string::npos; t += kTickMs)
        order = Tick(late, f);
    CHECK(order == "pullback 31" && late.eatWaitMs >= kRestCapMs);
    // At the end lines already: no wait.
    Run e = Start(&plan);
    f.minHealthPct = 95;
    CHECK(Tick(e, f) == "pullback 31");
    // Not inside an event.
    Run v = Start(&plan);
    v.eventOn = true;
    f.minHealthPct = 82;
    Tick(v, f);
    CHECK(v.phase != Phase::Rest);
}

static void TestWipeAndLeadDeathPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    r.autoRegroups = kAutoRegroupsPerStep;
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
    // The lead bot dead (the owner's wait rule): the party waits for it, a
    // regroup the run counts; past kAutoRegroupsPerStep it pauses.
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.walkBackDue);
    CHECK(r.note.find("waiting for 1 dead: the lead bot is dead") == 0);
    r.regroup = false; // as if the regroup ended and the lead bot fell again
    r.autoRegroups = kAutoRegroupsPerStep;
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
    // A kind the run does not know (the loader never lets one through; the
    // machine still pauses on it rather than guess).
    PlanStep unknown;
    unknown.kind = PlanStepKind::None;
    plan.steps = {unknown, Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("not supported yet") != std::string::npos);
    CHECK(!Supported(PlanStepKind::None) && Supported(PlanStepKind::Move));
    CHECK(Supported(PlanStepKind::Ritual) && Supported(PlanStepKind::Hold));
    // Skip from a pause goes on running (after the regroup).
    CHECK(Skip(r));
    CHECK(r.state == State::Running && r.step == 1 && r.regroup);
    CHECK(!Resume(r));
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

// Issue #999: a gather refused (a live ZF run: "cmd move 1420.7 801.1 11.0
// refused unreachable" with the tank on that very spot) is sent once more,
// then the pull goes on without it instead of pausing the run.
static void TestRefusedGatherIsRetriedOnceThenLeftOut()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 30;
    f.units = {Unit(7, 1, 20), Unit(8, 1, 25)};
    std::string const gather = "move 0.0 0.0 0.0";
    CHECK(Tick(r, f) == gather);
    OrderRefused(r, gather, "unreachable: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Running && r.note.find("once more") != std::string::npos);
    CHECK(Tick(r, f) == gather);
    OrderRefused(r, gather, "unreachable: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Running && r.note.find("without it") != std::string::npos);
    CHECK(Tick(r, f) == "pullback 7");
    // The next pull gathers again: the waiver held for one pull only.
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(8, 1, 25)};
    CHECK(Tick(r, f) == gather);
    CHECK(r.state == State::Running);

    // A direct step: refused twice, the direct walk goes out ungathered.
    DungeonPlan direct;
    direct.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run d = Start(&direct);
    Facts g = Party(0);
    g.maxMemberYd = 30;
    CHECK(Tick(d, g) == gather);
    OrderRefused(d, gather, "unreachable");
    CHECK(Tick(d, g) == gather);
    OrderRefused(d, gather, "unreachable");
    CHECK(d.state == State::Running && Tick(d, g) == "move 5.0 0.0 0.0 direct");

    // Other refusals still pause, also a gather's text sent on an earlier tick.
    Run o = Start(&plan);
    OrderRefused(o, gather, "unreachable");
    CHECK(o.state == State::Paused);
}

// Issue #999: a wipe regroups by itself once the fight is over (revive,
// gather), then walks the plan's own move points back from the entrance to
// the step; it counts the wipe; the third at one step pauses as before.
static void TestWipeRegroupsAndWalksBack()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 50), Step(PlanStepKind::Move, 90),
        PullStep(130, {1})};
    Run r = Start(&plan, 3);
    Facts f = Party(130);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1 && r.autoRegroups == 1);
    CHECK(r.note.find("(1 wipe this run)") != std::string::npos);
    CHECK(Tick(r, f) == "revive party");
    for (int i = 0; i < 30; ++i)
        Tick(r, f); // the corpse run: still one wipe
    CHECK(r.wipes == 1 && r.state == State::Running);
    // Alive at the entrance, spread: gather, then the walk back.
    f = Party(0);
    f.maxMemberYd = 40;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    CHECK(Tick(r, f).empty()); // regrouped; the walk back starts with the facts of its point
    // 19z5: straight to the last point before the step (90), in navmesh hops.
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 2 && FactStep(r) == &plan.steps[2] && OwnerFrom(r) == 2);
    f.seen = false; // FactStep's point, 90 yd away
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.note.find("walk back: approaching") == 0);
    f.leadX = 40;
    CHECK(Tick(r, f).empty()); // at the hop
    // A pack that came back by the next hop is pulled before the move.
    f.hopUnits = {Unit(9, 4, 30)};
    CHECK(Tick(r, f) == "pullback 9");
    CHECK(r.walkBack && r.note.find("walk back: pulling") == 0);
    f.hopUnits.clear();
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    // The hops fail (no path from here): the plan's points are replayed from
    // the nearest one (50, from 40).
    f.approachKnown = false;
    Tick(r, f);
    CHECK(r.walkBack && !r.walkBackDirect && r.walkBackIdx == 1 && r.note.find("replaying") != std::string::npos);
    f.seen = true;
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    // A point the navmesh refuses now is passed for the next one.
    OrderRefused(r, "move 50.0 0.0 0.0", "unreachable");
    CHECK(r.state == State::Running);
    CHECK(Tick(r, f) == "move 90.0 0.0 0.0");
    f.leadX = 90;
    CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f).empty() && !r.walkBack && r.note.find("walked back") == 0);
    CHECK(FactStep(r) == &plan.steps[3] && OwnerFrom(r) == 3);
    f.units = {Unit(7, 1, 40)};
    CHECK(Tick(r, f) == "pullback 7");
    CHECK(r.step == 3);

    // Second wipe at the step: regroups once more; the third pauses.
    f = Party(130);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.wipes == 2 && r.autoRegroups == 2);
    f = Party(130); // revived where it stands, near the step: no walk back
    f.units = {Unit(7, 1, 40)};
    CHECK(Tick(r, f) == "pullback 7");
    CHECK(!r.regroup && !r.walkBack);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.wipes == 3 && r.note.find("did not hold") != std::string::npos);
    CHECK(r.note.find("(cmd revive party)") != std::string::npos);
    // A resume gives the step its regroups back; a new step too.
    CHECK(Resume(r) && r.autoRegroups == 0);
}

// Three dead while the other two still fight: the fight goes on (bounded by
// its ceiling), the regroup starts when it ends; no wipe is counted.
static void TestThreeDeadRegroupAfterTheFight()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    f.leadAlive = false;
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.regroup && r.note.find("regroup follows") != std::string::npos);
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.wipes == 0 && r.nearWipes == 1);
    CHECK(r.note.find("(1 near wipe this run)") != std::string::npos);
    CHECK(Tick(r, f) == "revive party");
    // No earlier move point: nothing to walk back, the step goes on.
    f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.walkBack);
    // The done note carries the wipes.
    r.wipes = 2;
    f.leadX = 40;
    Tick(r, f);
    CHECK(r.state == State::Done && r.note == "plan done (2 wipes, 1 near wipe)");
}

// Issue #999 (third observe run, step 35): the healer stood 82 yd behind and
// did not come; the gather paused the run again and again. At the gather's
// ceiling the farthest bot over 40 yd that came less than 5 yd closer since
// the last sample is summoned to the lead bot, once; one on its way is not.
static void TestGatherSummonsAStalledStraggler()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 90;
    f.members = {Mem("Walky", 90), Mem("Healy", 82), Mem("Nearby", 30), Mem("Stuck", 50)};
    f.units = {Unit(7, 1, 20)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather: first sample
    // By the ceiling Walky came 30 yd closer; Healy and Stuck did not.
    f.members = {Mem("Walky", 60), Mem("Healy", 81), Mem("Nearby", 30), Mem("Stuck", 50)};
    f.maxMemberYd = 81;
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f); // Walky keeps walking (not standing still)
        order = Tick(r, f);
    }
    CHECK(order == "summon Healy"); // the farthest stalled one
    CHECK(r.state == State::Running && r.phase == Phase::Gather && r.note.find("Healy") != std::string::npos);
    // A refused summon does not pause: the gather's ceiling does.
    OrderRefused(r, order, "lead-unavailable");
    CHECK(r.state == State::Running);
    // Next ceiling: Walky still comes (no summon), Stuck stalled since the last sample: summoned.
    f.members = {Mem("Walky", 50), Mem("Healy", 81), Mem("Nearby", 30), Mem("Stuck", 49)};
    order.clear();
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f); // Walky keeps walking (not standing still)
        order = Tick(r, f);
    }
    CHECK(order == "summon Stuck");
    // Nobody left to summon (Walky still makes progress): the next ceiling pauses.
    f.members = {Mem("Walky", 44), Mem("Healy", 81), Mem("Nearby", 30), Mem("Stuck", 49)};
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f);
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);

    // The summoned bot arrives: the pull goes out.
    Run g = Start(&plan);
    Facts h = Party(0);
    h.maxMemberYd = 82;
    h.members = {Mem("Healy", 82)};
    h.units = {Unit(7, 1, 20)};
    Tick(g, h);
    std::string got;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && got.empty(); ++i)
        got = Tick(g, h);
    CHECK(got == "summon Healy");
    h.maxMemberYd = 5;
    h.members.clear();
    CHECK(Tick(g, h) == "pullback 7");
}

// Codex on #1000: a walk-back point refused for the lead bot's state of the
// moment is sent again (3 times, then pause); only a path refusal passes it.
static void TestWalkBackRefusalKinds()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 50), PullStep(130, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    r.walkBackIdx = 0;
    r.gatherWaived = true;
    r.gatherRefusals = 2;
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
    for (std::uint32_t i = 1; i <= kWalkBackRetries; ++i)
    {
        OrderRefused(r, "move 10.0 0.0 0.0", "lead-unavailable: The lead bot is dead, on a taxi or teleporting.");
        CHECK(r.state == State::Running && r.walkBackIdx == 0 && r.note.find("again") != std::string::npos);
        CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
    }
    OrderRefused(r, "move 10.0 0.0 0.0", "busy: a task runs");
    CHECK(r.state == State::Paused && r.walkBackIdx == 0);

    Run p = Start(&plan, 2);
    p.walkBack = true;
    p.gatherWaived = true;
    p.gatherRefusals = 2;
    CHECK(Tick(p, f) == "move 10.0 0.0 0.0");
    OrderRefused(p, "move 10.0 0.0 0.0", "lead-unavailable: teleporting");
    OrderRefused(p, "move 10.0 0.0 0.0", "off-mesh: The point is not on walkable ground.");
    // Passed: the next point, its own refusal count and gather waiver fresh.
    CHECK(p.state == State::Running && p.walkBackIdx == 1 && p.walkBackRefusals == 0);
    CHECK(!p.gatherWaived && p.gatherRefusals == 0);
    CHECK(Tick(p, f) == "move 50.0 0.0 0.0");
}

// Cold review on #1000: a pullback drags the boss to the party, so his
// corpse 30 yd from his spot is normal (radius + kSeeYd); and a boss seen
// alive in this step and now dead near the lead bot is down wherever he fell.
static void TestBossKilledAwayFromHisSpot()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 6910;             // Uldaman: Revelosh, his adds first
    boss.entries = {7076, 7077};
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(5, 6910, 30)};
    CHECK(Tick(r, f) == "pullback 5");
    CHECK(r.bossSeenAlive);
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units.clear();
    f.bossDeadNearSpot = true; // the glue: his corpse 30 yd from the spot, within radius + kSeeYd
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");

    // Fell far from his spot but near the lead bot: down, because he was seen alive.
    Run n = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(5, 6910, 30)};
    CHECK(Tick(n, g) == "pullback 5");
    g.anyInCombat = true;
    Tick(n, g);
    g.anyInCombat = false;
    g.units.clear();
    g.bossDeadNearLead = true;
    Tick(n, g);
    CHECK(n.step == 1 && n.note == "boss down");
    // Never seen alive this step: a corpse near the lead bot alone is not enough.
    Run m = Start(&plan);
    Facts h = Party(0);
    h.bossDeadNearLead = true;
    Tick(m, h);
    CHECK(m.step == 0 && m.note.find("not at his spot") != std::string::npos);
}

// Cold review on #1000: after a wipe the party stands at the entrance and the
// nearest walk-back point is more than ReactDistance away: it is approached in
// navmesh hops (as a resume does), not sent as one refused move; with no path
// toward it at all it is passed. Walk-back pulls have their own count.
static void TestWalkBackFarPointIsApproached()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 230), PullStep(400, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    Facts f = Party(0);
    f.seen = false; // FactStep: the walk-back point, 200 yd away
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.note.find("walk back: approaching") == 0);
    f.leadX = 40;
    CHECK(Tick(r, f).empty()); // at the hop: the next one next tick
    f.approachX = 80;
    CHECK(Tick(r, f) == "move 80.0 0.0 0.0");
    f.leadX = 80;
    Tick(r, f);
    f.seen = true;
    CHECK(Tick(r, f) == "move 200.0 0.0 0.0");
    // A hostile on the way: pulled, counted apart from the step's pulls.
    f.hopUnits = {Unit(9, 3, 20)};
    CHECK(Tick(r, f) == "pullback 9");
    CHECK(r.pulls == 0 && r.walkBackPulls == 1);

    // No path toward a far point: passed for the next one.
    Run p = Start(&plan, 2);
    p.walkBack = true;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = false;
    CHECK(Tick(p, g).empty());
    CHECK(p.state == State::Running && p.walkBackIdx == 1 && p.note.find("no walkable path") != std::string::npos);
}

// Codex on #1000 / 19z5 step 57: only an invalid-target refusal of a pullback
// leaves the unit out (for the run, no pause); a busy tank or an unavailable
// lead bot pauses as before.
static void TestRefusedPullOnlyForInvalidTarget()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    OrderRefused(r, "pullback 7", "busy: the tank is pulling");
    CHECK(r.state == State::Paused && !r.unreachable.count(7));
    Run l = Start(&plan);
    OrderRefused(l, "pullback 7", "lead-unavailable: The lead bot is dead");
    CHECK(l.state == State::Paused && !l.unreachable.count(7));
    Run i = Start(&plan);
    OrderRefused(i, "pullback 7", "invalid-target: The group cannot attack that target.");
    CHECK(i.state == State::Running && i.unreachable.count(7) && i.note.find("left out") != std::string::npos);
}

// Codex on #1001: the dead lead bot already released to the graveyard map;
// the glue lets the tick run (its map guard pauses only a living lead bot off
// the map), and the wipe starts the regroup.
static void TestWipeWithTheLeadOnTheGraveyardMap()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(-9000); // the lead's ghost far away on another map
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    f.maxMemberYd = 0;
    f.seen = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1);
    CHECK(Tick(r, f) == "revive party");
}

// Codex on #1000: a near wipe that becomes a wipe, then the revive one by one
// (4 dead, 3 dead with someone up again), is one near wipe and one wipe.
static void TestNearWipeLatchHoldsThroughTheWipe()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.nearWipes == 1 && r.wipes == 0);
    f.anyInCombat = false;
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.nearWipes == 1 && r.wipes == 1);
    for (std::uint32_t dead : {4u, 3u})
    {
        f.dead = dead;
        f.alive = 5 - dead;
        Tick(r, f);
        CHECK(r.nearWipes == 1 && r.wipes == 1);
    }
    // All up again: a later near wipe counts anew.
    f = Party(0);
    f.units = {Unit(7, 1, 20)};
    Tick(r, f);
    CHECK(r.state == State::Running);
    f.anyInCombat = true;
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.nearWipes == 2 && r.wipes == 1);
}

// 19z5 steps 27 and 81: the lead bot died first; the run paused "the lead bot
// is dead" and the rest died while it waited. Now the others fight on; when
// they fall too it is a wipe and the run regroups by itself. A dead lead bot
// alone after the fight still pauses.
static void TestLeadDiesFirstThenTheWipeRegroups()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(130);
    f.units = {Unit(7, 1, 10)};
    f.anyInCombat = true;
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.note.find("the others fight on") != std::string::npos);
    f.dead = 5;
    f.alive = 0;
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1);

    Run p = Start(&plan, 1);
    Facts g = Party(130);
    g.units = {Unit(7, 1, 10)};
    g.leadAlive = false;
    g.dead = 1;
    g.alive = 4;
    Tick(p, g);
    CHECK(p.state == State::Running && p.regroup && p.note.find("the lead bot is dead") != std::string::npos);
}

// 19z5 step 80 (15:43): 4 dead and the tank alone past the fight ceiling
// paused as "a fight lasted more than 180 s". It is a wipe: the run regroups
// by itself, within the step's budget.
static void TestFightCeilingWithFourDeadRegroups()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    f.dead = 4;
    f.alive = 1;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && !r.regroup; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.autoRegroups == 1 && r.note.find("went past") != std::string::npos);
    // Over budget: the ceiling pauses as before.
    Run p = Start(&plan);
    p.autoRegroups = kAutoRegroupsPerStep;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && p.state == State::Running; ++i)
        Tick(p, f);
    CHECK(p.state == State::Paused);
}

// 19z5 step 83: Draezur 29 yd away but on another floor (z 21 against 43);
// the gather paused twice. A bot more than 8 yd up or down that comes no
// closer is a straggler too.
static void TestStragglerOnAnotherFloorIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 29.1f;
    MemberFact healer{"Draezur", 29.1f};
    healer.dz = 22;
    MemberFact near{"Nearby", 25};
    near.dz = 2;
    f.members = {near, healer};
    f.units = {Unit(7, 1, 20)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Draezur");
}

// 19z5 step 27 and 81: after a revive by hand and `.bot run resume` the party
// stood at the entrance and the step's approach paused (no path / 8 hops).
// A resume walks back too: straight to the last plan point, in hops.
static void TestResumeFromTheEntranceWalksBack()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 300), PullStep(330, {1})};
    Run r = Start(&plan, 2);
    Pause(r, "the lead bot is dead: revive it, then .bot run resume");
    CHECK(Resume(r));
    Facts f = Party(0);
    CHECK(Tick(r, f).empty()); // regrouped
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 1);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
}

// 19z5 steps 83-84: the approach hop left the party at about 1622 1051 z 18;
// the tank chased over a ridge to 1625 1075 z 44 in the fight, and the next
// gather (on the lead's own spot) pinned the party on the hilltop, where no
// path led down. A gather now goes back to the run's anchor (the last point
// the run walked to) when the lead bot is off it, and gathers there.
static void TestGatherGoesBackToTheAnchorAfterARidgeChase()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1639.4f, {5649, 5650})};
    plan.steps[0].y = 1106.8f;
    plan.steps[0].z = 9.0f;
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadX = 1584; f.leadY = 1082; f.leadZ = 10;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 1622; f.approachY = 1051; f.approachZ = 18;
    CHECK(Tick(r, f) == "move 1622.0 1051.0 18.0");
    f.leadX = 1622; f.leadY = 1051; f.leadZ = 18;
    Tick(r, f); // at the hop: the anchor
    CHECK(r.anchorKnown && r.anchorZ == 18.0f);
    // In sight; the fight drags the tank up the ridge.
    f.seen = true;
    f.units = {Unit(1, 5649, 25)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.leadX = 1625; f.leadY = 1075; f.leadZ = 44;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(2, 5650, 30)};
    f.maxMemberYd = 29.1f;
    // The gather walks the lead bot back down to the anchor, not up the hill.
    CHECK(Tick(r, f) == "move 1622.0 1051.0 18.0");
    CHECK(r.phase == Phase::Gather && r.note.find("gathering back at 1622.0 1051.0 18.0") == 0);
    // Even with the party close, no pull while the lead bot is still up there.
    f.maxMemberYd = 5;
    CHECK(Tick(r, f).empty());
    // Back at the anchor and gathered: the pull goes out.
    f.leadX = 1622; f.leadY = 1052; f.leadZ = 18;
    CHECK(Tick(r, f) == "pullback 2");

    // Same floor but no navmesh path back (a pocket): back to the anchor too.
    Run p = Start(&plan);
    p.anchorKnown = true;
    p.anchorX = 1622; p.anchorY = 1051; p.anchorZ = 18;
    Facts g = Party(0);
    g.leadX = 1627; g.leadY = 1078; g.leadZ = 22;
    g.units = {Unit(2, 5650, 30)};
    // The path back is read lazily: asked for on the first tick, no pull yet.
    CHECK(Tick(p, g).empty() && p.anchorPathWanted && p.pulls == 0 && p.fruitlessPulls == 0);
    g.anchorPathRead = true;
    g.anchorReachable = false;
    g.units = {Unit(2, 5650, 30)};
    CHECK(Tick(p, g) == "move 1622.0 1051.0 18.0");
    // On the anchor's floor with a path back: the gather stays on the lead's spot.
    Run q = Start(&plan);
    q.anchorKnown = true;
    q.anchorX = 1622; q.anchorY = 1051; q.anchorZ = 18;
    Facts h = Party(0);
    h.leadX = 1630; h.leadY = 1060; h.leadZ = 20;
    h.anchorPathRead = true;
    h.maxMemberYd = 30;
    h.units = {Unit(2, 5650, 30)};
    CHECK(Tick(q, h) == "move 1630.0 1060.0 20.0");
}

// Codex on #1000: the shortest navmesh path back ignores packs. A direct walk
// back whose path passes within a not-yet-cleared pull group's radius + 10 yd
// drops the shortcut and replays the plan's points; no move enters the pack.
static void TestDirectWalkBackAvoidsUnclearedPacks()
{
    DungeonPlan plan;
    PlanStep later = PullStep(60, {9});
    later.y = 20;   // a later pack, 20 yd off the straight line
    ai::NormalizePlanStep(later);
    plan.steps = {Step(PlanStepKind::Move, 30), Step(PlanStepKind::Move, 120), PullStep(150, {1}), later};
    // Geometry: the straight path from 0 to 120 passes 20 yd from the later
    // pack (radius 15 + 10); a path that bends away does not.
    std::vector<PathPoint> straight = {{0, 0, 0}, {120, 0, 0}};
    std::vector<PathPoint> around = {{0, 0, 0}, {60, -30, 0}, {120, 0, 0}};
    CHECK(PathCrossesUnclearedPack(plan, 2, straight, 0));
    CHECK(!PathCrossesUnclearedPack(plan, 2, around, 0));
    CHECK(!PathCrossesUnclearedPack(plan, 4, straight, 0)); // that pack is cleared (an earlier step)

    Run r = Start(&plan, 2);
    Facts f = Party(0);
    r.walkBack = true;
    r.walkBackDirect = true;
    r.walkBackIdx = 1;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    f.approachY = 0;
    f.directPathCrossesPack = true;
    std::string order = Tick(r, f);
    CHECK(order.empty() && !r.walkBackDirect && r.walkBack && r.walkBackIdx == 0);
    CHECK(r.note.find("passes an uncleared pack") != std::string::npos);
    // The replay walks the plan's first point (30 0 0), not the hop toward the pack.
    f.seen = true;
    f.directPathCrossesPack = false;
    CHECK(Tick(r, f) == "move 30.0 0.0 0.0");
}

// Cold review on #1000: a stalled arrival short of the point (off the mesh)
// anchors where the lead bot stands, not at the unreachable point; skipped
// packs count in the walk back's pack check, the path's last leg too.
static void TestStalledAnchorAndSkippedPacks()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.leadX = 35; // 5 yd short, stuck
    for (std::uint32_t i = 0; i <= kWalkRetryMs / kTickMs + 1 && r.step == 0; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && r.anchorKnown && r.anchorX == 35.0f);

    DungeonPlan p;
    PlanStep skipped = PullStep(60, {9});
    skipped.y = 20;
    ai::NormalizePlanStep(skipped);
    PlanStep atEnd = PullStep(120, {8});
    ai::NormalizePlanStep(atEnd);
    p.steps = {skipped, Step(PlanStepKind::Move, 120), atEnd, Step(PlanStepKind::Move, 200)};
    std::vector<PathPoint> straight = {{0, 0, 0}, {119, 0, 0}};
    std::unordered_set<std::size_t> notCleared;
    CHECK(!PathCrossesUnclearedPack(p, 3, straight, 0, notCleared)); // both groups before step 3, cleared
    notCleared.insert(0);
    CHECK(PathCrossesUnclearedPack(p, 3, straight, 0, notCleared)); // the skipped one counts
    CHECK(PathCrossesUnclearedPack(p, 2, straight, 0));             // a group by the path's end counts too
}

// Issue #999 item 5 (Zul'Farrak): the Sandfury Witch Doctor 5650 heals its
// pack and drops a Greater Healing Ward 8179. Before the pull the mage
// polymorphs the best-ranked cc unit (never the pull target); the pull goes
// on a kill_first unit; in the fight the kill_first units are focused, the
// ward first as soon as it is up.
static PlanStep ZfPack()
{
    return PullStep(10, {5650, 5648, 7247});
}

static DungeonPlan ZfPlan()
{
    DungeonPlan plan;
    plan.steps = {ZfPack()};
    plan.ccPriority = {5650, 7247, 7246, 5648};
    plan.ccAlways = {5650, 7246};
    plan.killFirst = {8179, 5650, 7247};
    return plan;
}

static UnitFact Engaged(UnitFact u)
{
    u.engaged = true;
    return u;
}

// A Greater Healing Ward 8179 of `owner`; ownerEngaged: its owner fights the party.
static UnitFact Ward(std::uint64_t guid, std::uint64_t owner, bool ownerEngaged)
{
    UnitFact u = Unit(guid, 8179, 18);
    u.owner = owner;
    u.ownerEngaged = ownerEngaged;
    return u;
}

static void TestPullCrowdControlsTheHealer()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {MemberFact{"Magey", 5, true, true}};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20), Unit(3, 7247, 25)};
    // The witch doctor is the best cc rank: it is sheeped, not pulled.
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.note.find("crowd control on 5650") == 0 && r.state == State::Running);
    // The pull goes on the soul eater (kill_first) rather than the nearest.
    CHECK(Tick(r, f) == "pullback 3");
    CHECK(r.pulls == 1 && r.fruitlessPulls == 0);
    // In the fight: the soul eater is focused; the sheeped witch doctor is not.
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(2, 5650, 20)), Engaged(Unit(3, 7247, 5))};
    CHECK(Tick(r, f) == "focus 3");
    CHECK(Tick(r, f).empty()); // only on a change
    // A ward comes up: it is focused first.
    f.killFirst.push_back(Ward(9, 3, true));
    CHECK(Tick(r, f) == "focus 9");
    // The ward dies: back on the soul eater.
    f.killFirst.pop_back();
    CHECK(Tick(r, f) == "focus 3");
    // After the fight: the sheeped witch doctor is left while another can be
    // pulled, and no second cc goes out while it lives.
    f.anyInCombat = false;
    f.killFirst.clear();
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "pullback 2"); // the last one, sheeped or not
    // A refused cc or focus does not pause.
    OrderRefused(r, "cc moon 2 Magey", "no-bot");
    OrderRefused(r, "focus 3", "invalid-target");
    CHECK(r.state == State::Running);
}

static void TestNoCrowdControlWithoutAMageOrPlanList()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    // No mage: no cc; the pull still prefers kill_first.
    CHECK(Tick(r, f) == "pullback 2");

    DungeonPlan bare;
    bare.steps = {ZfPack()};
    Run b = Start(&bare);
    Facts g = Party(0);
    g.members = {MemberFact{"Magey", 5, true, true}};
    g.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(b, g) == "pullback 1"); // the nearest, as before
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(2, 5650, 20))};
    CHECK(Tick(b, g).empty());

    // A lone cc unit is pulled, not sheeped.
    Run c = Start(&plan);
    Facts h = Party(0);
    h.members = {MemberFact{"Magey", 5, true, true}};
    h.units = {Unit(2, 5650, 20)};
    CHECK(Tick(c, h) == "pullback 2");
}

// Codex on #1001: the Polymorph is pending until its aura shows on that unit
// in the fight (the mage's cc strategy is a combat one): not within
// kCcLandMs, the mark once more; still not, noted, and the fight goes on.
static void TestCrowdControlMustLand()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst.clear();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {MemberFact{"Tanky", 0, false}, MemberFact{"Magey", 5, true, true}};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.ccPending == 2);
    CHECK(Tick(r, f) == "pullback 1"); // the mark is out; the pull starts the fight
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty() && r.ccPending == 2);
    f.units[1].polymorphed = true; // landed
    CHECK(Tick(r, f).empty());
    CHECK(r.ccPending == 0 && r.note.find("polymorphed") != std::string::npos);

    // Never lands: once more after kCcLandMs, then noted.
    Run n = Start(&plan);
    Facts g = Party(0);
    g.members = {MemberFact{"Magey", 5, true, true}};
    g.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(n, g) == "cc moon 2 Magey");
    CHECK(Tick(n, g) == "pullback 1");
    g.anyInCombat = true;
    std::string order;
    for (std::uint32_t i = 0; i <= kCcLandMs / kTickMs && order.empty(); ++i)
        order = Tick(n, g);
    CHECK(order == "cc moon 2 Magey" && n.ccResent && n.note.find("once more") != std::string::npos);
    order.clear();
    for (std::uint32_t i = 0; i <= kCcLandMs / kTickMs && n.ccPending; ++i)
        order += Tick(n, g);
    CHECK(order.empty() && n.ccPending == 0 && n.note.find("cc did not land on 5650") != std::string::npos);
    CHECK(n.state == State::Running);

    // A mage that does not know Polymorph is not the crowd control.
    Run m = Start(&plan);
    Facts h = Party(0);
    h.members = {MemberFact{"Lowmage", 5, false}};
    h.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(m, h) == "pullback 1" && m.ccSent.empty());
}

// Codex on #1001: kill_first is this fight's only. A ward whose owner fights
// the party is focused; a ward of an unrelated witch doctor nearby is not,
// nor a kill_first unit that is not engaged.
static void TestKillFirstIsThisFightsOnly()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Ward(20, 30, false), Unit(30, 5650, 25)}; // unrelated ward and its idle owner
    CHECK(Tick(r, f).empty());
    f.killFirst.push_back(Ward(21, 1, true)); // the ward of a unit in this fight
    CHECK(Tick(r, f) == "focus 21");
}

// 19z5: the mage enters the fight seconds after the pull (it waits for the
// tank's threat, then walks into range); its Polymorphs landed 7-8 s into its
// own fight. The window counts only the mage's fight time.
static void TestCcWindowCountsTheMagesFightTime()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst.clear();
    Run r = Start(&plan);
    Facts f = Party(0);
    MemberFact mage{"Magey", 5, true, false};
    f.members = {mage};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    for (int i = 0; i < 20; ++i)
        CHECK(Tick(r, f).empty()); // the mage not in the fight yet: not counted
    CHECK(r.ccPending == 2 && !r.ccResent);
    f.members[0].inCombat = true;
    std::string order;
    std::uint32_t ticks = 0;
    for (; ticks <= kCcLandMs / kTickMs && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(order == "cc moon 2 Magey" && ticks == kCcLandMs / kTickMs);
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
    f.seen = false;
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
    f.seen = false;
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
    // In sight and nobody there: skipped (not "boss down": it was never fought).
    f.units.clear();
    Run r4 = Start(&plan);
    Tick(r4, f);
    CHECK(r4.step == 1 && r4.note == "boss is for the other team and not here: skipped");
    // The party's own boss in sight and dead: down.
    f.team = ai::kPlanFlagHorde;
    f.bossDeadNearSpot = true;
    Run r5 = Start(&plan);
    Tick(r5, f);
    CHECK(r5.step == 1 && r5.note == "boss down");
}

// Fourth observe run (Theka, ZF steps 54-56): "boss down" 2 s after arriving,
// with no fight; Theka patrols. A boss neither alive nor dead in sight is
// waited for at his spot, then the run pauses; never counted down.
static void TestBossDownNeedsASeenDeath()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 7272;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && r.state == State::Running && r.note.find("not at his spot") != std::string::npos);
    // He walks in on his patrol: pulled.
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    f.units = {Unit(5, 7272, 40)};
    CHECK(Tick(r, f) == "pullback 5");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    // Killed: his corpse is seen.
    f.units.clear();
    f.bossDeadNearSpot = true;
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");

    // Never there: paused after kBossSearchMs, "not found".
    Run p = Start(&plan);
    Facts g = Party(0);
    for (std::uint32_t i = 0; i <= kBossSearchMs / kTickMs + 1 && p.state == State::Running; ++i)
        Tick(p, g);
    CHECK(p.state == State::Paused && p.step == 0 && p.note.find("boss 7272 not found") == 0);
}

// Fourth observe run (step 43 -> 45) and 19z5 step 57 (Sul'lithuz Hatchlings
// 8130): a pullback refused invalid-target. The unit is left out for the
// run without a pause; the pack is skipped as out of reach, and a later step
// that sees the same unit pulls the other one. A skip clears the pull state.
static void TestSkipDropsTheRefusedPullTarget()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1}), Step(PlanStepKind::Move, 20), PullStep(30, {1, 2})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    CHECK(Tick(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", "invalid-target: The group cannot attack that target.");
    CHECK(r.state == State::Running && r.pullGuid == 0);
    Tick(r, f);
    CHECK(r.step == 1 && r.note.find("out of reach") != std::string::npos);
    f = Party(20);
    Tick(r, f);
    CHECK(r.step == 2);
    f.units = {Unit(7, 1, 5), Unit(8, 2, 15)};
    CHECK(Tick(r, f) == "pullback 8");
    // A skip clears the step's pull state.
    CHECK(Skip(r));
    CHECK(r.pullGuid == 0 && r.pulls == 0 && r.pullTries.empty());
}

static void TestResumeWithLeadDead()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.leadAlive = false;
    Tick(r, f);
    // The owner's wait rule: no pause, the regroup waits for the lead bot.
    CHECK(r.state == State::Running && r.regroup);
    Pause(r, "test: the old pause");
    CHECK(Resume(r));
    CHECK(r.regroup);
    CHECK(Tick(r, f) == "revive party");
    // The ghost runs back: no pause while the regroup's ceiling holds.
    for (int i = 0; i < 100; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.regroup);
    // Alive again, gathered: the step goes on.
    f.dead = 0;
    f.alive = 5;
    f.leadAlive = true;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup);
    // A lead bot that never comes back is bounded by kRegroupReviveMs.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    g.dead = 1;
    g.alive = 4;
    g.leadAlive = false;
    Tick(r2, g);
    Resume(r2);
    for (std::uint32_t i = 0; i <= kRegroupReviveMs / kTickMs + 2 && r2.state == State::Running; ++i)
        Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("still dead") != std::string::npos);
}

static UnitFact Hitter(std::uint64_t guid, std::uint32_t entry, float dist, bool healer = false)
{
    UnitFact u = Unit(guid, entry, dist);
    u.hitting = true;
    u.victimHealer = healer;
    return u;
}

// Uldaman rerun 2026-10-17, the walk to Galgann at -10 337: the tank held
// "stay" with nothing on it while the party fought 13-31 yd away. With mobs
// hitting the party for kGuardWaitMs the tank picks the nearest one up
// (`attack <guid> <tank>`), and after the fight the run walks it back to the
// stand point.
static void TestTankGuardsThePartyFromItsStand()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f); // on the stand point: the anchor, next step
    CHECK(r.step == 1 && r.anchorKnown);
    f.units = {Unit(31, 4853, 40)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    // The pullback's mobs aggroed on the healer and still run in: not hitting yet.
    UnitFact running = Unit(31, 4853, 30);
    running.victimHealer = true;
    f.partyAttackers = {running};
    for (int i = 0; i < 8; ++i)
        CHECK(Tick(r, f).empty());
    // They arrive and hit the party; nothing is on the tank.
    f.partyAttackers = {Hitter(41, 7320, 35), Hitter(51, 7320, 45), Hitter(31, 4853, 30)};
    for (std::uint32_t ms = kTickMs; ms < kGuardWaitMs; ms += kTickMs)
        CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f) == "attack 31 Tanky"); // the nearest one within kGuardYd
    CHECK(r.guardReturn);
    // The order ended the stay; the tank holds that mob now.
    f.leadStaying = false;
    f.leadHit = true;
    f.leadX = 28;
    f.partyAttackers = {Hitter(41, 7320, 12)};
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).empty()); // a mob on a DPS while the tank holds its own: the tank's own AI
    // The fight is over: back to the stand point first, then the step goes on.
    f.anyInCombat = false;
    f.leadHit = false;
    f.partyAttackers.clear();
    f.units.clear();
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.leadX = 1;
    f.leadStaying = true;
    Tick(r, f);
    CHECK(!r.guardReturn);
    CHECK(r.state == State::Done);
}

// Replay of keepers door pull 100 (trace r-...9bbf): a Brawler on the tank in
// melee reach, a Geomancer casting at the mage from 30 yd, then two Maulers on
// the mage and the healer. The tank keeps its Brawler while only the mage is
// hit; a mob hitting the healer is picked up (it needs no stay once the guard
// took the tank off it), the next one after a fresh wait, and the cap is
// reached in one fight.
static void TestTankGuardPull100Replay()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    f.units = {Unit(97, 4855, 40)};
    CHECK(Tick(r, f) == "pullback 97");
    f.anyInCombat = true;
    f.leadHit = true; // the Brawler, 1 yd
    f.partyAttackers = {Hitter(98, 4853, 30)}; // the Geomancer on the mage
    for (int i = 0; i < 8; ++i)
        CHECK(Tick(r, f).empty());
    // A Mauler reaches the healer at T (trace: 7478); she is dead at T+2-3 s
    // (botwatch 73 % at 03:07:22, 0 % at 03:07:26): the order goes out on the
    // first tick she is seen hit.
    f.partyAttackers = {Hitter(98, 4853, 30), Hitter(90, 7320, 14, true)};
    CHECK(Tick(r, f) == "attack 90 Tanky");
    // Off its stay now; the second Mauler turns on the healer: picked up once
    // kGuardRepeatMs have passed since the first, and the cap is reached in
    // one fight.
    f.leadStaying = false;
    f.partyAttackers = {Hitter(98, 4853, 30), Hitter(91, 7320, 20, true)};
    CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f) == "attack 91 Tanky");
    int orders = 2;
    for (int i = 0; i < 20; ++i)
        if (Tick(r, f).compare(0, 7, "attack ") == 0)
            ++orders;
    CHECK(orders == static_cast<int>(kGuardOrdersPerFight));
    CHECK(r.state == State::Running);
}

// The guard stays out of the way: no tank, a tank off its stay the guard did
// not take off, a polymorphed, evading, too far or not yet hitting mob, a
// scripted event; a refused pick-up or walk back never pauses the run.
static void TestTankGuardLimits()
{
    auto guardRun = [](DungeonPlan& plan)
    {
        plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855})};
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        f.units = {Unit(31, 4855, 40)};
        Tick(r, f); // pullback 31
        return r;
    };
    auto fight = [](bool tank, bool staying, bool hit)
    {
        Facts f = Party(0);
        f.anyInCombat = true;
        f.leadIsTank = tank;
        f.leadStaying = staying;
        f.leadHit = hit;
        f.partyAttackers = {Hitter(31, 4855, 20)};
        return f;
    };
    auto ordersIn = [](Run& r, const Facts& f, int ticks)
    {
        int n = 0;
        for (int i = 0; i < ticks; ++i)
            if (Tick(r, f).compare(0, 7, "attack ") == 0)
                ++n;
        return n;
    };
    {
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight(true, true, true), 8) == 0);   // the tank holds a mob; the victim is no healer
        CHECK(ordersIn(r, fight(false, true, false), 8) == 0); // the lead bot is no tank
        CHECK(ordersIn(r, fight(true, false, false), 8) == 0); // off its stay by itself: its own AI goes already
        Facts f = fight(true, true, false);
        f.partyAttackers[0].polymorphed = true;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].polymorphed = false;
        f.partyAttackers[0].evading = true;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].evading = false;
        f.partyAttackers[0].hitting = false;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].hitting = true;
        f.partyAttackers[0].distLead = kGuardYd + 1;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].distLead = 20;
        CHECK(ordersIn(r, f, 5) == 1);
        OrderRefused(r, "attack 31 Tanky", "failed: That bot did not accept the attack.");
        CHECK(r.state == State::Running);
    }
    {
        // A scripted event fights in place (the Keepers' and Archaedas' holds).
        DungeonPlan plan;
        Run r = guardRun(plan);
        r.eventOn = true;
        CHECK(ordersIn(r, fight(true, true, false), 8) == 0);
    }
    {
        // A refused walk back to the stand point goes on from where it stands.
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight(true, true, false), 5) == 1);
        Facts after = Party(25);
        CHECK(Tick(r, after) == "move 0.0 0.0 0.0");
        OrderRefused(r, "move 0.0 0.0 0.0", "unreachable: The lead bot has no walkable path to that point.");
        CHECK(r.state == State::Running && !r.guardReturn);
    }
}

static void TestEmptyPlan()
{
    DungeonPlan plan;
    Run r = Start(&plan);
    CHECK(r.state == State::Done);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
}

// (a) Uldaman 19z11 01:59-02:04: the revived priest walked part of the way
// (more than kStragglerGainYd since the gather began), then stood at the
// scorpid pit's navmesh hole (-163.5 298.7). The regroup's ceiling skipped
// her and paused. A bot that stands still now is summoned, once per regroup;
// then one more gather window, then the pause.
static void TestStuckStragglerThatMovedIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.maxMemberYd = 110;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 110)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather: first sample at 110 yd
    // She walks 30 yd, then stands at the hole, 80 yd out.
    f.members = {Mem("Tanky", 0), Mem("Fyran", 80)};
    f.maxMemberYd = 80;
    std::string order;
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Fyran");
    CHECK(r.state == State::Running && r.regroup && r.note.find("has not moved") != std::string::npos);
    // Bounded: one summon per bot per regroup; the next ceiling pauses.
    order.clear();
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
    // A bot still on its way (moving every tick) is not summoned.
    Run w = Start(&plan);
    BeginRegroup(w);
    Facts g = Party(0);
    g.maxMemberYd = 110;
    g.members = {Mem("Tanky", 0), Mem("Walky", 110)};
    Tick(w, g);
    g.members[1].yd = 80;
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && w.state == State::Running; ++i)
    {
        g.members[1].x += (i % 2 ? 3.0f : -3.0f);
        CHECK(Tick(w, g).find("summon") == std::string::npos);
    }
    CHECK(w.state == State::Paused);
}

// (b) Uldaman 19z11 02:50: the approach paused after 8 hops while the priest
// stood still on a navmesh pocket (43 371). Before the pause a far bot that
// stands still is summoned, once; a walker is not; then the pause (the lead
// bot's own way is the reason).
static void TestApproachHopSummonsBeforePause()
{
    DungeonPlan plan;
    plan.steps = {PullStep(300, {1})};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    f.maxMemberYd = 15;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 150), Mem("Walky", 90)};
    f.members[2].yd = 10; // close enough for the pull's gather
    f.maxMemberYd = 150;
    r.approachHops = kMaxApproachHops;
    Run::StillState st;
    st.x = 150;
    st.ms = kStillMs;
    r.still["Fyran"] = st;
    std::string const order = Tick(r, f);
    CHECK(order == "summon Fyran");
    CHECK(r.state == State::Running && r.note.find("approach: Fyran") == 0);
    CHECK(Tick(r, f).find("summon") == std::string::npos); // Walky moves: not summoned
    CHECK(r.state == State::Paused && r.note.find("approach hops") != std::string::npos);
}

// (c) Uldaman 19z11 02:48: only the lead bot died in the fight; its own
// release put it at the entrance 300 yd away while the others won. It is the
// wipe recovery: regroup, then the walk back over the plan's points. Bounded
// by kAutoRegroupsPerStep. A lead bot raised where it fell needs nothing.
static void TestLeadOnlyDeathRecovers()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.anyInCombat = true;
    Tick(r, f); // fighting, the lead bot alive at 210
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f);
    CHECK(r.leadDied && r.state == State::Running);
    // Back on its feet at the entrance, the fight won.
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.anyInCombat = false;
    f.leadX = -90;
    f.maxMemberYd = 300;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 305)};
    f.members[1].x = 215; // where the party won the fight, 5 yd from where the lead bot fell
    Tick(r, f);
    CHECK(r.regroup && r.walkBackDue && r.autoRegroups == 1 && r.state == State::Running);
    CHECK(r.note.find("the lead bot died and came back") == 0);
    // The living party holds; the lead bot walks back to it, nobody is
    // gathered on (or summoned to) the entrance.
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran");
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -50;
    std::string order;
    for (int i = 0; i < 3; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("summon") != 0 && order != "move -90.0 0.0 0.0");
    }
    CHECK(!r.regroup && r.walkBack && r.hold.size() == 1);
    // Back with it: the hold ends.
    f.leadX = 200;
    f.members[1].yd = 15;
    Tick(r, f);
    CHECK(r.hold.empty());
    // Bounded: past kAutoRegroupsPerStep it pauses.
    Run p = Start(&plan, 2);
    p.autoRegroups = kAutoRegroupsPerStep;
    Facts g = Party(210);
    g.anyInCombat = true;
    Tick(p, g);
    g.leadAlive = false;
    g.dead = 1;
    g.alive = 4;
    Tick(p, g);
    g.leadAlive = true;
    g.dead = 0;
    g.alive = 5;
    g.anyInCombat = false;
    g.leadX = -90;
    Tick(p, g);
    CHECK(p.state == State::Paused && p.note.find("came back") != std::string::npos);
    // Raised where it fell (a resurrection in the party): no regroup of its own.
    Run q = Start(&plan, 2);
    Facts h = Party(210);
    h.anyInCombat = true;
    Tick(q, h);
    h.leadAlive = false;
    h.dead = 1;
    h.alive = 4;
    Tick(q, h);
    h.leadAlive = true;
    h.dead = 0;
    h.alive = 5;
    h.anyInCombat = false;
    Tick(q, h);
    CHECK(!q.regroup && q.autoRegroups == 0 && !q.leadDied);
}

// The owner's rule (2026-10-17): "Party should wait if a party member dies
// until it is back." A death in a walk holds the walk; the bot alive again but
// far is waited for (running back); within kGatherYd the step goes on.
static void TestDeathWhileWalkingBlocksTheStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for Fyran (dead)") == 0);
    // Released, ran back, alive at the entrance: still waited for.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.maxMemberYd = 90;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 90)};
    std::string order;
    for (int i = 0; i < 20; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("move 40") != 0);
    }
    CHECK(r.regroup && r.note.find("Fyran (running back)") != std::string::npos);
    // Back with the party: the walk goes on.
    f.maxMemberYd = 5;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 5)};
    for (int i = 0; i < 5 && order.find("move 40") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "move 40.0 0.0 0.0" && !r.regroup && r.waitFor.empty());
}

// The lead bot dead out of a fight: the party waits for it (no pause).
static void TestLeadDeathBlocks()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).find("pullback") != 0);
    CHECK(r.state == State::Running && r.regroup && r.note.find("Tanky (dead)") != std::string::npos);
}

// Events cannot be left mid-way: the wait comes after the event.
static void TestWaitRuleEventException()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 80)};
    Run r = Start(&plan);
    r.eventOn = true;
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup && r.waitFor.count("Fyran"));
    r.eventOn = false; // the event is over
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for Fyran (dead)") == 0);
}

// N1 (Uldaman 19z11: 17 of 63 pulls with a member dead): the pull itself
// holds while anyone is dead, outside an event.
static void TestNoPullWithADeadMember()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    std::string order;
    PullUnit(r, f, f.units[0], order);
    CHECK(order.empty() && r.note.find("no pull while a party member is dead") != std::string::npos);
    for (int i = 0; i < 40; ++i)
        CHECK(Tick(r, f).find("pullback") != 0);
    // Inside an event the pull goes out (it cannot wait).
    Run e = Start(&plan);
    e.eventOn = true;
    order.clear();
    PullUnit(e, f, f.units[0], order);
    CHECK(order == "pullback 7");
}

// N3 (Uldaman 01:43, 02:31): three dead, the two rogues still fighting. The
// regroup neither revives nor gathers nor walks while a living bot fights:
// the fight first, then the regroup.
static void TestRegroupWaitsForTheFight()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(100, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(100);
    f.dead = 3;
    f.alive = 2;
    f.deadNames = {"Tanky", "Fyran", "Iront"};
    f.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.empty() || order.find("focus") == 0 || order.find("cc ") == 0);
    }
    CHECK(r.state == State::Running && r.note.find("fighting on") != std::string::npos);
    // A regroup already going (a resume) holds in a fight too.
    Run g = Start(&plan, 1);
    BeginRegroup(g);
    Facts h = Party(100);
    h.dead = 1;
    h.alive = 4;
    h.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(g, h).find("revive") != 0);
    h.anyInCombat = false;
    CHECK(Tick(g, h) == "revive party");
}

// A death during the regroup's gather: back to reviving.
static void TestDeathDuringRegroup()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.maxMemberYd = 60;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 60)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    f.members = {Mem("Tanky", 0)};
    f.maxMemberYd = 0;
    CHECK(Tick(r, f) == "revive party");
    CHECK(r.regroup && r.note.find("Fyran (dead)") != std::string::npos);
}

// Review of #1060: the common case, the fight ends while the lead bot is
// still a ghost. The regroup revives it; once it is up at the entrance the
// living party deep inside holds (no gather to the entrance, no summon there).
static void TestLeadDeadAfterTheFightHolds()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.anyInCombat = true;
    Tick(r, f); // fighting, the lead bot alive at 210
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f); // it falls in the fight
    // The fight is over, the lead bot still dead: the regroup, its hold armed.
    f.anyInCombat = false;
    f.members = {Mem("Fyran", 5)};
    f.members[0].x = 215;
    f.maxMemberYd = 0;
    Tick(r, f);
    CHECK(r.regroup && r.holdDue && r.walkBackDue && r.autoRegroups == 1 && r.hold.empty());
    CHECK(Tick(r, f) == "revive party");
    // Up at the entrance, 300 yd from where it fell.
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.leadX = -90;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 305)};
    f.members[1].x = 215;
    f.maxMemberYd = 305;
    CHECK(Tick(r, f).empty());
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran" && !r.holdDue);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -50;
    for (int i = 0; i < 20; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.find("summon") != 0 && order != "move -90.0 0.0 0.0");
    }
    CHECK(r.state == State::Running && !r.regroup && r.walkBack && r.hold.size() == 1);
    // A player's order pauses the run: the hold ends (the glue lets them go).
    CHECK(PauseForPlayer(r, "follow"));
    CHECK(r.hold.empty() && !r.holdDue);
    // Raised where it fell (a resurrection in the party): no hold.
    Run q = Start(&plan, 2);
    Facts h = Party(210);
    h.anyInCombat = true;
    Tick(q, h);
    h.leadAlive = false;
    h.dead = 1;
    h.alive = 4;
    Tick(q, h);
    h.anyInCombat = false;
    h.members = {Mem("Fyran", 30)};
    h.members[0].x = 240;
    Tick(q, h);
    CHECK(q.holdDue);
    h.leadAlive = true;
    h.dead = 0;
    h.alive = 5;
    Tick(q, h);
    CHECK(q.hold.empty() && !q.holdDue);
}

int main()
{
    TestMove();
    TestWalkStuckIsBounded();
    TestStalledNearPointCountsAsArrived();
    TestPullLoopOnOneTargetPauses();
    TestOutOfReachPackIsSkippedNotCleared();
    TestMissingItemPausesOnce();
    TestResumeRegroups();
    TestResumeWithLeadDead();
    TestFarMoveWalksBackInHops();
    TestDirectStepGathersFirst();
    TestSkipWhileRunning();
    TestUnreachableMoveDetours();
    TestPullGatherAndFight();
    TestUnseenPackIsApproachedNotCleared();
    TestFarPackHopIsNotUndoneByTheGatherBack();
    TestApproachHopCapCountsHopsWithoutProgress();
    TestItemCarrierIsPickedFromTheStepsOwnInputs();
    TestUseItemPausesOnSplitInputs();
    TestLootStepIsDoneWhenAnyPartyBotHasTheItem();
    TestLootRefusalTriesTheTurnBotThenEachBot();
    TestPullThatStartsNoFightIsPassed();
    TestAmbushWalksThrough();
    TestBossKillOrderAndTeamGate();
    TestHopPullsNearUnitsFirst();
    TestWipeAndLeadDeathPause();
    TestFewDeadReviveBeforePull();
    TestFewDeadRuleSkipsEvents();
    TestRegroupFightsFirst();
    TestRegroupWaitsForPartyRez();
    TestRegroupRevivePauses();
    TestMealWaitBeforePull();
    TestFightCeiling();
    TestTaskSteps();
    TestOpenWithoutKeyAndTaskError();
    TestWaitObjectTimeoutIsBounded();
    TestUnsupportedPausesAndSkip();
    TestPlayerOrderAndRefusalPause();
    TestStepIdleCeiling();
    TestTeamFlags();
    TestOtherTeamBossIsLookedAtAndFoughtUnlessFriendly();
    TestRefusedGatherIsRetriedOnceThenLeftOut();
    TestWipeRegroupsAndWalksBack();
    TestThreeDeadRegroupAfterTheFight();
    TestGatherSummonsAStalledStraggler();
    TestWalkBackRefusalKinds();
    TestBossDownNeedsASeenDeath();
    TestSkipDropsTheRefusedPullTarget();
    TestBossKilledAwayFromHisSpot();
    TestWalkBackFarPointIsApproached();
    TestRefusedPullOnlyForInvalidTarget();
    TestWipeWithTheLeadOnTheGraveyardMap();
    TestNearWipeLatchHoldsThroughTheWipe();
    TestLeadDiesFirstThenTheWipeRegroups();
    TestFightCeilingWithFourDeadRegroups();
    TestStragglerOnAnotherFloorIsSummoned();
    TestResumeFromTheEntranceWalksBack();
    TestGatherGoesBackToTheAnchorAfterARidgeChase();
    TestDirectWalkBackAvoidsUnclearedPacks();
    TestStalledAnchorAndSkippedPacks();
    TestPullCrowdControlsTheHealer();
    TestNoCrowdControlWithoutAMageOrPlanList();
    TestCrowdControlMustLand();
    TestKillFirstIsThisFightsOnly();
    TestCcWindowCountsTheMagesFightTime();
    TestTankGuardsThePartyFromItsStand();
    TestTankGuardPull100Replay();
    TestTankGuardLimits();
    TestEmptyPlan();
    TestStuckStragglerThatMovedIsSummoned();
    TestApproachHopSummonsBeforePause();
    TestLeadOnlyDeathRecovers();
    TestDeathWhileWalkingBlocksTheStep();
    TestLeadDeathBlocks();
    TestWaitRuleEventException();
    TestNoPullWithADeadMember();
    TestRegroupWaitsForTheFight();
    TestDeathDuringRegroup();
    TestLeadDeadAfterTheFightHolds();
    std::cout << "test_dungeon_run_policy: ok\n";
    return 0;
}
