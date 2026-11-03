// Standalone check for the run's teleport return and the "awaiting return" hold (runtime/DungeonRunPolicy.h
// FarReturnTick, StrandedTeleportTick, WedgedSummon, SummonRevived; ai/playerbot/policy/dungeon/ReturnHoldPolicy.h):
//   g++ -std=c++17 -Wall -Wextra tools/test_teleport_return_policy.cpp -o /tmp/t && /tmp/t
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

static MemberFact Mem(const std::string& name, float yd, float dz = 0.0f)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.dz = dz;
    m.x = yd;
    return m;
}

// A bot-only party, the lead bot (Tanky) at 0 0 0, both keys at their shipped defaults.
static Facts Party(bool keysOn = true)
{
    Facts f;
    f.total = 3;
    f.alive = 3;
    f.carrier = "Tanky";
    f.seen = true;
    f.members = {Mem("Tanky", 0.0f), Mem("Healy", 5.0f)};
    if (keysOn)
    {
        f.teleportReturnYd = 120.0f;
        f.reviveHoldYd = 100.0f;
    }
    return f;
}

static DungeonPlan TwoMoves()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), Step(PlanStepKind::Move, 200)};
    return plan;
}

// Walk 29 run a: a revived bot 400 yd away, fighting adders at the entrance, is teleported to the lead bot (it was
// skipped for its fight, and walked back through the packs).
static void TestRevivedFarInCombatIsTeleported()
{
    DungeonPlan plan = TwoMoves();
    Run r = Start(&plan);
    r.waitFor.insert("Runner");
    Facts f = Party();
    MemberFact runner = Mem("Runner", 400.0f);
    runner.inCombat = true;
    runner.attackers = 2;
    f.members.push_back(runner);
    f.anyInCombat = true;
    std::string order = Tick(r, f);
    CHECK(order.compare(0, 6, "fetch ") != 0); // queued; the glue reads the lead bot's ground first
    CHECK(r.fetch.count("Runner") && r.fetchReadWanted && r.returnHold.count("Runner"));
    f.fetchRead = true;
    order = Tick(r, f);
    CHECK(order == "fetch 0.0 0.0 0.0 Runner");
    CHECK(r.note == "teleporting Runner (try 1 of 3) back to the lead bot");
    // It landed: done, and released.
    f.members[2].yd = 2.0f;
    f.members[2].x = 2.0f;
    f.members[2].inCombat = false;
    f.members[2].attackers = 0;
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(!r.fetch.count("Runner") && !r.returnHold.count("Runner"));

    // Keys at 0: the old behaviour, skipped while it fights, no hold.
    Run old = Start(&plan);
    old.waitFor.insert("Runner");
    Facts off = Party(false);
    off.members.push_back(runner);
    off.anyInCombat = true;
    off.fetchRead = true;
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(old, off).compare(0, 6, "fetch ") != 0);
    CHECK(old.fetch.empty() && old.returnHold.empty() && !old.fetchReadWanted);

    // A real player in the party: no teleport (bot-only), as before.
    Run withPlayer = Start(&plan);
    withPlayer.waitFor.insert("Runner");
    Facts pf = f;
    pf.members[2] = runner;
    pf.anyInCombat = true;
    PlayerFact p;
    p.name = "Someone";
    pf.players.push_back(p);
    Tick(withPlayer, pf);
    CHECK(withPlayer.fetch.empty());
}

// Not there after the teleport: sent again 5 s later, three tries in all, then a note naming the bot (never left to
// walk back).
static void TestTeleportRetriesThenNotes()
{
    DungeonPlan plan = TwoMoves();
    Run r = Start(&plan);
    r.waitFor.insert("Runner");
    Facts f = Party();
    f.members.push_back(Mem("Runner", 400.0f));
    f.members[2].inCombat = true;
    f.anyInCombat = true;
    f.fetchRead = true;
    std::vector<std::uint64_t> sent;
    std::string giveUp;
    for (int i = 0; i < 30 && giveUp.empty(); ++i)
    {
        std::string const order = Tick(r, f);
        if (order.compare(0, 6, "fetch ") == 0)
            sent.push_back(r.runMs);
        if (r.note.find("teleport returns") != std::string::npos)
            giveUp = r.note;
    }
    CHECK(sent.size() == kFetchTries);
    CHECK(sent[1] - sent[0] == kFetchRetryMs && sent[2] - sent[1] == kFetchRetryMs);
    CHECK(giveUp == "Runner is still 400.0 yd from the lead bot after 3 teleport returns in 15 s: bring it to the lead bot");
    CHECK(r.fetchGaveUp.count("Runner") && r.fetch.empty() && r.state == State::Running);
    // No fourth try at that revive.
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).compare(0, 6, "fetch ") != 0);
    CHECK(kFetchTries == 3 && kFetchRetryMs == 5000);
}

// The portal dropped a summon (the bot fought when it went): the run hears it and teleports the bot instead.
static void TestCancelledSummonIsRetriedByTeleport()
{
    DungeonPlan plan = TwoMoves();
    auto run = [&plan]()
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        r.reviveSent = true;
        r.waitFor.insert("Sedge");
        r.summoned.insert("Sedge");
        r.reviveSummons["Sedge"] = 0; // SummonRevived's summon is out
        return r;
    };
    Facts f = Party();
    f.members.push_back(Mem("Sedge", 60.0f));
    f.fetchRead = true;
    Run r = run();
    Tick(r, f); // learns its cancel count (0)
    f.members[2].summonCancels = 1;
    std::string const order = Tick(r, f);
    CHECK(order == "fetch 0.0 0.0 0.0 Sedge");
    CHECK(r.fetch.at("Sedge").tries == 1 && r.returnHold.count("Sedge"));

    // Keys at 0: the old fall-back, it walks back (the cancel is only heard sooner).
    Facts off = Party(false);
    off.members.push_back(Mem("Sedge", 60.0f));
    Run old = run();
    Tick(old, off);
    off.members[2].summonCancels = 1;
    CHECK(Tick(old, off).compare(0, 6, "fetch ") != 0);
    CHECK(!old.reviveSummons.count("Sedge") && old.fetch.empty());
    Run note = run();
    note.summonCancelled.insert("Sedge");
    UpdateReviveSummons(note, off, kTickMs);
    CHECK(note.note == "regroup: the summon of Sedge was cancelled by the portal (a fight): it walks back");
    // ... and with the key on, the same failure is a teleport, never a walk.
    Run on = run();
    on.fetchOn = true;
    on.summonCancelled.insert("Sedge");
    UpdateReviveSummons(on, f, kTickMs);
    CHECK(on.note == "regroup: the summon of Sedge was cancelled by the portal (a fight): it is teleported back instead");
    CHECK(on.fetch.count("Sedge"));
}

static Run StrandRun(const DungeonPlan& plan)
{
    Run r = Start(&plan, 1);
    r.strandIdx = 1;
    r.strandSummon = true;
    r.fetchOn = true;
    r.holdOn = true;
    return r;
}

// The stranded lead bot's summons: one counts as done when the bot arrived (10 yd), not when it went out (walk 29
// run a: Sedge and Dinkle were "summoned", stayed at the entrance and died there).
static void TestStrandedSummonDoneOnlyOnArrival()
{
    DungeonPlan plan = TwoMoves();
    Run r = StrandRun(plan);
    Facts f = Party();
    f.members.push_back(Mem("Left", 30.0f));
    std::string order;
    CHECK(StrandedTeleportTick(r, f, kTickMs, order) && order == "summon Left");
    CHECK(r.strandSummoned.count("Left") && !r.strandArrived.count("Left") && r.returnHold.count("Left"));
    // Still 30 yd away: pending, not done, and not summoned again before kReviveSummonWaitMs.
    std::uint32_t ms = 0;
    for (; ms + kTickMs < kReviveSummonWaitMs; ms += kTickMs)
    {
        order.clear();
        CHECK(StrandedTeleportTick(r, f, kTickMs, order) && order.empty() && r.strandSummon);
    }
    CHECK(r.note == "the lead bot was teleported to the last reached point (step 2): waiting for Left (30.0 yd from it) "
        "to arrive");
    order.clear();
    CHECK(StrandedTeleportTick(r, f, kTickMs, order) && order == "summon Left"); // not there: again
    CHECK(r.strandSummonTries["Left"] == 2);
    // The portal drops it: again at once.
    order.clear();
    r.summonCancelled.insert("Left");
    CHECK(StrandedTeleportTick(r, f, kTickMs, order) && order == "summon Left");
    r.summonCancelled.clear();
    // Arrived: done, and the summons end.
    f.members[2].yd = 4.0f;
    order.clear();
    CHECK(!StrandedTeleportTick(r, f, kTickMs, order) && order.empty());
    CHECK(r.strandArrived.count("Left") && !r.strandSummoned.count("Left") && !r.strandSummon);

    // Left behind farther than the teleport return's reach, in a fight: teleported, not waited for.
    Run far = StrandRun(plan);
    Facts ff = Party();
    ff.members.push_back(Mem("Dinkle", 400.0f));
    ff.members[2].inCombat = true;
    order.clear();
    CHECK(StrandedTeleportTick(far, ff, kTickMs, order) && order.empty());
    CHECK(far.fetch.count("Dinkle") && far.returnHold.count("Dinkle"));
    // ... with the key off: waited for (in its fight), as before.
    Run farOld = StrandRun(plan);
    farOld.fetchOn = false;
    farOld.holdOn = false;
    order.clear();
    CHECK(StrandedTeleportTick(farOld, ff, kTickMs, order) && order.empty() && farOld.fetch.empty());
    CHECK(farOld.note.find("to leave its fight before summoning it") != std::string::npos);
}

// A wedged raid member past the teleport return's reach is teleported, in combat or not.
static void TestWedgedFarIsTeleported()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 2000), Step(PlanStepKind::Move, 3000)};
    Facts f = Party();
    f.raid.on = true;
    MemberFact w = Mem("W", 150.0f, -30.0f);
    w.inCombat = true;
    f.members.push_back(w);
    Run r = Start(&plan);
    r.fetchOn = true;
    r.holdOn = true;
    r.still["W"].ms = kWedgedMs;
    std::string order;
    CHECK(WedgedSummon(r, f, kTickMs, order) && order.empty() && r.fetch.count("W"));
    CHECK(r.note.find("): teleported back") != std::string::npos);
    Run old = Start(&plan);
    old.still["W"].ms = kWedgedMs;
    CHECK(!WedgedSummon(old, f, kTickMs, order) && order.empty()); // key off: in combat, skipped as before
}

// The lead bot on a live pull (walk 29 run d: the whole raid summoned onto the Jin'do pack): the return point is the
// nearest read point outside every unpulled unit's reach.
static void TestReturnPointOutsideThePack()
{
    Facts f = Party();
    StandPackReach unit;
    unit.x = 2.0f;
    unit.reachYd = 15.0f;
    f.fetchReach = {unit};
    ai::ApproachPoint a{12.0f, 0.0f, 0.0f}, b{0.0f, 25.0f, 0.0f}, c{-30.0f, 0.0f, 0.0f};
    f.fetchSpots = {a, b, c};
    ai::ApproachPoint p;
    bool clear = false;
    CHECK(FetchPoint(f, p, clear) == 1 && clear && p.y == 25.0f); // a is in reach (10 < 18), b is out
    CHECK(ai::PlanDistance(p.x, p.y, p.z, unit.x, unit.y, unit.z) >= unit.reachYd + kFetchClearMarginYd);
    // The lead bot clear of every pack: its own spot.
    f.fetchReach[0].x = 100.0f;
    CHECK(FetchPoint(f, p, clear) == -1 && clear && p.x == 0.0f && p.y == 0.0f);
    // Nothing clear: the lead bot's spot, and the caller is told.
    f.fetchReach[0].x = 0.0f;
    f.fetchReach[0].reachYd = 60.0f;
    CHECK(FetchPoint(f, p, clear) == -1 && !clear);

    // Through the tick: the order names that point, the hold arrives there.
    DungeonPlan plan = TwoMoves();
    Run r = Start(&plan);
    r.waitFor.insert("Runner");
    Facts t = Party();
    t.fetchReach = {unit};
    t.fetchSpots = {a, b, c};
    t.fetchRead = true;
    t.members.push_back(Mem("Runner", 300.0f));
    CHECK(Tick(r, t) == "fetch 0.0 25.0 0.0 Runner");
    CHECK(r.note == "teleporting Runner (try 1 of 3) back to the lead bot: beside it, outside the reach of the pack it "
        "stands by");
    CHECK(r.returnHold.at("Runner").pointKnown && r.returnHold.at("Runner").y == 25.0f);
    t.members[2].yd = 25.0f; // landed at the point, 25 yd from the lead bot
    t.members[2].x = 0.0f;
    t.members[2].y = 25.0f;
    Tick(r, t);
    CHECK(!r.fetch.count("Runner") && !r.returnHold.count("Runner"));
}

// A bot the run is bringing back stands still: the hold does not end because its leader moved 50 yd; it ends on
// arrival, or after kReturnHoldMs with a note. The bot's CanMove reads the glue's mark (ReturnHoldOn).
static void TestReturnHoldStandsStillUntilArrival()
{
    DungeonPlan plan = TwoMoves();
    Run r = Start(&plan);
    r.waitFor.insert("Held");
    Facts f = Party();
    f.teleportReturnYd = 0.0f; // the portal summon path
    f.members.push_back(Mem("Held", 80.0f));
    std::string order;
    r.holdOn = true;
    CHECK(SummonRevived(r, f, order) && order == "summon Held" && r.returnHold.count("Held"));
    for (int i = 0; i < 5; ++i)
    {
        f.members[2].yd = 80.0f + 10.0f * static_cast<float>(i + 1); // the lead bot walks on 50 yd
        Tick(r, f);
        CHECK(r.returnHold.count("Held"));
    }
    std::uint32_t const now = 1000000;
    CHECK(ai::ReturnHoldOn(now + ai::RETURN_HOLD_MARK_MS, now)); // the glue's mark: CanMove false
    CHECK(!ai::ReturnHoldOn(0, now) && !ai::ReturnHoldOn(now, now) && !ai::ReturnHoldOn(now - 1, now));
    CHECK(!ai::ReturnHoldOn(now + ai::RETURN_HOLD_MARK_MS + 1, now));
    CHECK(ai::ReturnHoldOn(500u, 0xFFFFFF00u)); // across the timer wrap
    f.members[2].yd = 5.0f; // the summon landed
    Tick(r, f);
    CHECK(!r.returnHold.count("Held"));

    // Never arrives: released after 60 s with a note naming it.
    Run late = Start(&plan);
    late.holdOn = true;
    HoldForReturn(late, "Held");
    f.members[2].yd = 90.0f;
    std::string note;
    for (std::uint32_t ms = 0; ms <= ai::kReturnHoldMs + kTickMs && late.returnHold.count("Held"); ms += kTickMs)
    {
        order.clear();
        if (FarReturnTick(late, f, kTickMs, order))
            note = late.note;
        CHECK(order.empty() && late.state == State::Running);
    }
    CHECK(!late.returnHold.count("Held"));
    CHECK(note == "Held waited 60 s 90.0 yd from the lead bot for its summon or teleport: released");
    // Hold key 0: no hold at all.
    Run off = Start(&plan);
    off.waitFor.insert("Held");
    Facts zero = Party(false);
    zero.members.push_back(Mem("Held", 80.0f));
    Tick(off, zero);
    order.clear();
    CHECK(SummonRevived(off, zero, order) && off.returnHold.empty());
    // A paused run leaves its bots to their own AI.
    Run paused = Start(&plan);
    paused.holdOn = true;
    HoldForReturn(paused, "Held");
    Pause(paused, "test");
    CHECK(paused.returnHold.empty());
}

// The glue's revive hold (any revive, the run paused or not): armed for a bot revived inside a dungeon farther than
// the key from the lead bot; released with the lead bot or once the run took it over.
static void TestReviveHoldRules()
{
    CHECK(ai::ShouldArmReviveHold(100.0f, true, 400.0f));
    CHECK(ai::ShouldArmReviveHold(100.0f, true, 1000.0f)); // the lead bot not on its map
    CHECK(!ai::ShouldArmReviveHold(100.0f, true, 100.0f));
    CHECK(!ai::ShouldArmReviveHold(100.0f, false, 400.0f)); // outside a dungeon: nothing changes
    CHECK(!ai::ShouldArmReviveHold(0.0f, true, 400.0f));    // key 0: off
    CHECK(ai::ReviveHoldReleased(10.0f, false) && !ai::ReviveHoldReleased(10.5f, false));
    CHECK(ai::ReviveHoldReleased(400.0f, true));
    CHECK(ai::kReturnHoldMs == 60000 && ai::kReturnArriveYd == 10.0f);
}

int main()
{
    TestRevivedFarInCombatIsTeleported();
    TestTeleportRetriesThenNotes();
    TestCancelledSummonIsRetriedByTeleport();
    TestStrandedSummonDoneOnlyOnArrival();
    TestWedgedFarIsTeleported();
    TestReturnPointOutsideThePack();
    TestReturnHoldStandsStillUntilArrival();
    TestReviveHoldRules();
    std::cout << "test_teleport_return_policy: ok\n";
    return 0;
}
