// Standalone check for the run leader's event holds after Uldaman run 3
// (19z12, tests/uldaman-events-3): the done condition read on every tick (in a
// fight, an approach and a pause too), the regroup when the lead bot falls in a
// hold, the party's places on the hold spot, and an approach hop not read on
// the tick a walk ended (runtime/DungeonRunPolicy.h: HoldDoneNote,
// HoldDoneWhilePaused, LeadLostInHold, HoldStands, Facts::approachRead).
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_hold_policy.cpp -o /tmp/test_dungeon_hold_policy && /tmp/test_dungeon_hold_policy
#include "../runtime/DungeonRunPolicy.h"

#include <cmath>
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
using ai::PlanStepRefusal;

static bool Has(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

static PlanStep Move(float x, float y = 0.0f)
{
    PlanStep s;
    s.kind = PlanStepKind::Move;
    s.x = x;
    s.y = y;
    ai::NormalizePlanStep(s);
    return s;
}

// Archaedas: hold at the origin until instance data 2 reads 3 (DONE), an
// encounter; Archaedas (2748) is the first entry, the statues follow.
static PlanStep ArchaedasHold(float radius = 45.0f)
{
    PlanStep s;
    s.kind = PlanStepKind::Hold;
    s.entry = 2;
    s.param = 3;
    s.flags = ai::kPlanFlagDoneData | ai::kPlanFlagEncounter;
    s.timeoutS = 600;
    s.radius = radius;
    s.entries = {2748, 7309, 7077, 7076, 10120};
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

// The vault door after it (124369 active).
static PlanStep DoorWait()
{
    PlanStep s;
    s.kind = PlanStepKind::WaitObject;
    s.entry = 124369;
    s.param = 1;
    s.timeoutS = 60;
    s.x = 40.0f;
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

static Facts Party()
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.carrier = "Trian";
    f.seen = true;
    f.eventData = 1; // IN_PROGRESS
    const char* names[] = {"Trian", "Cogtastic", "Quelinator", "Tobbert", "Rundar"};
    const bool ranged[] = {false, false, true, true, true};
    for (int i = 0; i < 5; ++i)
    {
        MemberFact m;
        m.name = names[i];
        m.yd = i == 0 ? 0.0f : 3.0f;
        m.ranged = ranged[i];
        f.members.push_back(m);
    }
    return f;
}

static UnitFact Unit(std::uint32_t entry, float x, float y, bool awake)
{
    UnitFact u;
    u.guid = entry * 100 + static_cast<std::uint64_t>(x + 1000.0f);
    u.entry = entry;
    u.x = x;
    u.y = y;
    u.distLead = std::sqrt(x * x + y * y);
    u.hostile = awake;
    return u;
}

// The lead bot dead in a fight: off the living members, on the dead list.
static void KillLead(Facts& f)
{
    f.leadAlive = false;
    f.alive = 4;
    f.dead = 1;
    f.deadNames = {"Trian"};
    f.members.erase(f.members.begin());
}

static void TestHoldDoneSeenFromAway()
{
    // Run 3 at 11:10: Archaedas died 12 s after the lead bot. The hold rested,
    // then approached its spot from where the lead bot came back to life.
    DungeonPlan plan;
    plan.steps = {Move(-30), ArchaedasHold(), DoorWait()};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.units = {Unit(2748, 11, 0, false)};
    Tick(r, f);
    CHECK(r.eventOn && r.phase == Phase::Hold);
    // The fight: the lead bot dies, the others fight on.
    f.anyInCombat = true;
    f.units[0].hostile = true;
    KillLead(f);
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running);
    // Archaedas dies in the fight: data 2 reads 3. The hold ends on that tick,
    // in the fight, without the spot in sight.
    f.seen = false;
    f.eventData = 3;
    Tick(r, f);
    // (the note says "fighting" again: the fight goes on under the next step).
    CHECK(r.step == 2 && r.state == State::Running && !r.eventOn);

    // The same from an approach: the lead bot alive far away, the spot out of
    // sight, no fight. No approach hop goes out; the step ends.
    Run a = Start(&plan, 1);
    Facts g = Party();
    g.seen = false;
    g.approachKnown = true;
    g.approachX = -20;
    g.eventData = 3;
    CHECK(Tick(a, g) != "move -20.0 0.0 0.0");
    CHECK(a.step == 2 && Has(a.note, "waiting for object 124369"));
    // ... and from a rest phase.
    Run b = Start(&plan, 1);
    Facts h = Party();
    h.minHealthPct = 30;
    h.eventData = 1;
    CHECK(Tick(b, h) == "rest" && b.phase == Phase::Rest);
    h.eventData = 3;
    Tick(b, h);
    CHECK(b.step == 2);
    // Not done: an approach goes on as before.
    Run c = Start(&plan, 1);
    g.eventData = 1;
    CHECK(Tick(c, g) == "move -20.0 0.0 0.0" && c.step == 1);
    // Done by object: the door state counts the same way out of sight.
    PlanStep door;
    door.kind = PlanStepKind::Hold;
    door.entry = 124367;
    door.param = 1;
    door.flags = ai::kPlanFlagDoneObject;
    door.timeoutS = 600;
    CHECK(ai::NormalizePlanStep(door) == PlanStepRefusal::None);
    DungeonPlan doorPlan;
    doorPlan.steps = {Move(-30), door, Move(30)};
    Run d = Start(&doorPlan, 1);
    Facts k = Party();
    k.seen = false;
    k.objectState = 1;
    Tick(d, k);
    CHECK(d.step == 2);
    CHECK(Has(HoldDoneNote(door, k), "event done: object 124367 is in state 1"));
    CHECK(HoldDoneNote(DoorWait(), k).empty()); // only a hold ends this way
}

static void TestPausedHoldDoneResumes()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArchaedasHold(), DoorWait()};
    Run r = Start(&plan, 1);
    Pause(r, "8 approach hops did not bring the party within 60.0 yd of the event spot");
    Facts f = Party();
    f.seen = false;
    // Not done yet: still paused, nothing changes.
    CHECK(!HoldDoneWhilePaused(r, f) && r.state == State::Paused && r.step == 1);
    // Done while paused: the step ends and the run resumes with a regroup.
    f.eventData = 3;
    CHECK(HoldDoneWhilePaused(r, f));
    CHECK(r.state == State::Running && r.step == 2 && r.regroup && r.walkBackDue);
    CHECK(Has(r.note, "event done: data 2 reads 3 while paused (8 approach hops") && Has(r.note, "resumed by itself"));
    // A running run is not touched.
    CHECK(!HoldDoneWhilePaused(r, f));

    // A player's pause stays a pause: the step ends, the note says so.
    Run p = Start(&plan, 1);
    CHECK(PauseForPlayer(p, "move 1 2 3"));
    CHECK(HoldDoneWhilePaused(p, f));
    CHECK(p.state == State::Paused && p.step == 2 && PausedByPlayer(p) && Has(p.note, "event done: data 2 reads 3") &&
        Has(p.note, ".bot run resume goes on with step 3"));

    // `.bot run pause` is a player's pause too, whatever its note says.
    Run q = Start(&plan, 1);
    PausePlayer(q, "paused by .bot run pause");
    CHECK(PausedByPlayer(q) && HoldDoneWhilePaused(q, f) && q.state == State::Paused && q.step == 2);
    // A run's own pause after a resume is not the player's.
    CHECK(Resume(q));
    Pause(q, "a fight lasted more than 600 s");
    CHECK(!PausedByPlayer(q));

    // The hold as the plan's last step: the run is done.
    DungeonPlan last;
    last.steps = {Move(-30), ArchaedasHold()};
    Run l = Start(&last, 1);
    Pause(l, "event not done after 600 s");
    CHECK(HoldDoneWhilePaused(l, f) && l.state == State::Done);
}

static void TestLeadDeadInHoldRegroups()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArchaedasHold(), DoorWait()};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.units = {Unit(2748, 11, 0, false)};
    Tick(r, f);
    CHECK(r.eventOn);
    f.anyInCombat = true;
    KillLead(f);
    Tick(r, f);
    // The fight is over with the lead bot dead and the event not done: a
    // regroup (revive), never an approach hop toward the spot.
    f.anyInCombat = false;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -20;
    std::string order = Tick(r, f);
    CHECK(r.regroup && order.compare(0, 5, "move ") != 0 && Has(r.note, "the lead bot is dead"));
    CHECK(Tick(r, f) == "revive party");

    // The lead bot back to life away from the spot (its own release): the
    // wipe recovery, not an approach.
    Run a = Start(&plan, 1);
    Facts g = Party();
    g.units = {Unit(2748, 11, 0, false)};
    Tick(a, g);
    g.anyInCombat = true;
    KillLead(g);
    Tick(a, g);
    g = Party();
    g.leadX = -200;
    g.seen = false;
    g.approachKnown = true;
    g.approachX = -150;
    for (MemberFact& m : g.members)
        if (m.name != "Trian")
        {
            m.yd = 200;
            m.x = 0;
        }
    order = Tick(a, g);
    CHECK(a.regroup && order != "move -150.0 0.0 0.0" && Has(a.note, "came back to life away from the party"));

    // A living lead bot with a dead healer inside the event: the hold goes on
    // (the event is not left mid-way for a member).
    Run b = Start(&plan, 1);
    Facts h = Party();
    h.units = {Unit(2748, 11, 0, false)};
    Tick(b, h);
    h.alive = 4;
    h.dead = 1;
    h.deadNames = {"Quelinator"};
    h.members.erase(h.members.begin() + 2);
    Tick(b, h);
    CHECK(!b.regroup && b.phase == Phase::Hold);
}

static void TestHoldStandsPlaceRangedBehindTank()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArchaedasHold(), DoorWait()};
    Run r = Start(&plan, 1);
    Facts f = Party();
    // Archaedas (the first entry) 11 yd east of the spot, statues all round.
    f.units = {Unit(2748, 11, 0, false), Unit(7076, -20, 20, false), Unit(7076, -20, -20, false)};
    Tick(r, f);
    CHECK(r.phase == Phase::Hold && r.holdStands.size() == 3 && r.holdStandsGen == 1);
    // Every ranged bot (healer too), never the lead bot or the rogue, west of
    // the tank (away from Archaedas), within kHoldGatherYd of the spot.
    for (const HoldStand& s : r.holdStands)
    {
        CHECK(s.name == "Quelinator" || s.name == "Rundar" || s.name == "Tobbert");
        CHECK(s.x < -4.0f && std::sqrt(s.x * s.x + s.y * s.y) <= kHoldGatherYd + 0.01f);
    }
    // Spread across the line, kHoldSpreadYd apart.
    CHECK(std::fabs(r.holdStands[0].y - r.holdStands[1].y) >= kHoldSpreadYd - 0.01f);
    // The same party: the places stay (no new generation for the glue).
    f.units[0].x = 9;
    Tick(r, f);
    CHECK(r.holdStandsGen == 1);
    // The mage dies: the places change once.
    f.members.erase(f.members.begin() + 3);
    f.alive = 4;
    f.dead = 1;
    f.deadNames = {"Tobbert"};
    Tick(r, f);
    CHECK(r.holdStands.size() == 2 && r.holdStandsGen == 2);
    // The step ends: no places left (the glue lets the bots follow again).
    f.eventData = 3;
    Tick(r, f);
    CHECK(r.step == 2 && r.holdStands.empty());

    // Units that ring the spot (their middle on it): never a stand nearer to a
    // unit than the tank's spot; two units east and west leave the north or
    // south side open.
    Run k = Start(&plan, 1);
    Facts g = Party();
    g.units = {Unit(2748, 15, 0, false), Unit(2748, -15, 0, false)};
    Tick(k, g);
    CHECK(k.holdStands.size() == 3);
    for (const HoldStand& s : k.holdStands)
        for (const UnitFact& unit : g.units)
            CHECK(std::hypot(s.x - unit.x, s.y - unit.y) >= std::hypot(unit.x, unit.y) - 0.01f);
    // The Keepers' hold as in the plan (cold review of #1076): the four statues
    // ring the spot, their middle 3.4 yd off it, and the script wakes each onto
    // the NEAREST player. The old fallback (the move the party came by, south-
    // west) put the ranged 4.6-5.6 yd from the south statue that woke first.
    // Every stand must be at least as far from every statue as the spot is.
    PlanStep keepers = ArchaedasHold(30.0f);
    keepers.entry = 1;
    keepers.entries = {4857};
    keepers.x = 101.5f;
    keepers.y = 271.2f;
    PlanStep altar;
    altar.kind = PlanStepKind::Ritual;
    altar.entry = 130511;
    altar.x = 104.8f;
    altar.y = 272.5f;
    CHECK(ai::NormalizePlanStep(altar) == PlanStepRefusal::None);
    DungeonPlan uld;
    uld.steps = {Move(82.3f, 252.4f), altar, keepers};
    Run u = Start(&uld, 2);
    Facts kf = Party();
    kf.leadX = 101.5f;
    kf.leadY = 271.2f;
    kf.units = {Unit(4857, 99.7f, 259.3f, false), Unit(4857, 117.8f, 267.9f, false), Unit(4857, 109.4f, 286.1f, false),
        Unit(4857, 91.4f, 277.5f, false)};
    Tick(u, kf);
    CHECK(u.phase == Phase::Hold && u.holdStands.size() == 3);
    for (const HoldStand& s : u.holdStands)
    {
        CHECK(std::hypot(s.x - 101.5f, s.y - 271.2f) <= kHoldGatherYd + 0.01f);
        for (const UnitFact& unit : kf.units)
            CHECK(std::hypot(s.x - unit.x, s.y - unit.y) >= std::hypot(101.5f - unit.x, 271.2f - unit.y) - 0.01f);
    }
    // A small hold radius bounds the places too.
    DungeonPlan small;
    small.steps = {Move(0, 30), ArchaedasHold(4.0f)};
    Run t = Start(&small, 1);
    Facts h = Party();
    Tick(t, h);
    CHECK(t.holdStands.size() == 3);
    for (const HoldStand& s : t.holdStands)
        CHECK(s.y > 0.0f && std::sqrt(s.x * s.x + s.y * s.y) <= 4.01f);
    // A pause clears the places (every bot back to its own AI).
    Pause(t, "test");
    CHECK(t.holdStands.empty());
    // No ranged bot: no places.
    Run n = Start(&plan, 1);
    Facts m = Party();
    for (MemberFact& member : m.members)
        member.ranged = false;
    Tick(n, m);
    CHECK(n.phase == Phase::Hold && n.holdStands.empty());
}

// Zul'Farrak's pyramid event (r12-19z29 / r8-19z26): a hold at the origin with
// the five troll entries; instance data 1 reads 7 when done (never in a test).
static PlanStep ZfHold()
{
    PlanStep s;
    s.kind = PlanStepKind::Hold;
    s.entry = 1;
    s.param = 7;
    s.flags = ai::kPlanFlagDoneData;
    s.timeoutS = 600;
    s.radius = 30.0f;
    s.entries = {7787, 7788, 7789, 8876, 8877};
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

// A hold run past its arrival: the party on its places (gen 1, three ranged
// bots), the tank on stay.
struct FightHold
{
    DungeonPlan plan;
    Run r;
    Facts f;
};

static void StartFightHold(FightHold& h, PlanStep hold, std::uint32_t entry)
{
    h.plan.steps = {Move(-30), hold, DoorWait()};
    h.r = Start(&h.plan, 1);
    h.f = Party();
    h.f.leadIsTank = true;
    h.f.leadStaying = true;
    h.f.units = {Unit(entry, 30, 0, false)};
    Tick(h.r, h.f);
    CHECK(h.r.phase == Phase::Hold && h.r.eventOn && h.r.holdPlaced);
    CHECK(h.r.holdStands.size() == 3 && h.r.holdStandsGen == 1);
}

// An awake unit of the event in the fight, `yd` from the tank.
static UnitFact Engaged(std::uint32_t entry, float x, float y, float yd)
{
    UnitFact u = Unit(entry, x, y, true);
    u.engaged = true;
    u.distLead = yd;
    return u;
}

// The ranged bots (the healer too) `yd` from the tank, on the ground.
static void RangedYd(Facts& f, float yd)
{
    for (MemberFact& m : f.members)
        if (m.ranged)
        {
            m.yd = yd;
            m.dz = 0.0f;
        }
}

// Plan ZF-EVENT-HOLD T1: the tank is sent at the first awake troll (the engage
// order ends its stay), chases it 12 yd off the spot and on, and the ranged
// fall behind it out of heal range: the places are let go after two fight
// ticks (not one), and the glue lets the placed bots follow the tank.
static void TestHoldStandsLetGoWhenTheRangedFallBehindTheTank()
{
    FightHold h;
    StartFightHold(h, ZfHold(), 7789);
    Run& r = h.r;
    Facts& f = h.f;
    f.anyInCombat = true;
    f.units = {Engaged(7789, 14, 5, 15.0f)};
    RangedYd(f, 6.0f);
    std::string const order = Tick(r, f);
    CHECK(order == "attack " + std::to_string(f.units[0].guid) + " Trian");
    CHECK(r.holdUnstayed && Has(r.note, "the tank takes 7789"));
    CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1 && r.holdPlaced);
    // The tank fights where it stands, then chases 12 yd off; the ranged 36 yd behind.
    f.leadStaying = false;
    f.leadX = 12.0f;
    RangedYd(f, 36.0f);
    Tick(r, f);
    CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1 && r.holdPlaced); // one tick is not enough
    Tick(r, f);
    CHECK(r.holdStands.empty() && r.holdStandsGen == 2 && !r.holdPlaced);
    CHECK(Has(r.note, "the ranged 36.0 yd behind the tank") && Has(r.note, "leave their places") && r.holdReleases == 1);
    // No churn while it stays away: the generation does not move again.
    for (int i = 0; i < 20; ++i)
    {
        f.leadX = 12.0f + static_cast<float>(i);
        Tick(r, f);
    }
    CHECK(r.holdStands.empty() && r.holdStandsGen == 2 && !r.holdPlaced && r.holdReleases == 1);
}

// T2: the places stay (a) for a one-tick spike of the distance, (b) while the
// tank is on or near the spot (up to exactly kHoldReturnYd) however far a
// ranged bot is, (c) while the ranged are within reach of a tank that is far
// off the spot. Every case runs many rounds.
static void TestHoldStandsKeptUnlessBothAreFar()
{
    // (a) spikes: far for one tick, back within reach the next, never two in a row.
    {
        FightHold h;
        StartFightHold(h, ZfHold(), 7789);
        Run& r = h.r;
        Facts& f = h.f;
        f.anyInCombat = true;
        f.units = {Engaged(7789, 14, 5, 15.0f)};
        Tick(r, f);
        f.leadStaying = false;
        f.leadX = 20.0f;
        for (int i = 0; i < 30; ++i)
        {
            RangedYd(f, i % 2 ? 12.0f : 36.0f);
            Tick(r, f);
            CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1 && r.holdPlaced);
        }
    }
    // (b) the tank on the spot, a ranged bot far off: not the tank's walk.
    {
        FightHold h;
        StartFightHold(h, ZfHold(), 7789);
        Run& r = h.r;
        Facts& f = h.f;
        f.anyInCombat = true;
        f.units = {Engaged(7789, 14, 5, 15.0f)};
        Tick(r, f);
        f.leadStaying = false;
        RangedYd(f, 40.0f);
        f.leadX = 5.0f;
        for (int i = 0; i < 20; ++i)
            Tick(r, f);
        f.leadX = kHoldReturnYd; // exactly the walk back's limit is still the spot
        for (int i = 0; i < 5; ++i)
            Tick(r, f);
        CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1 && r.holdPlaced);
        f.leadX = kHoldReturnYd + 0.5f;
        Tick(r, f);
        Tick(r, f);
        CHECK(r.holdStands.empty() && r.holdStandsGen == 2);
    }
    // (c) the tank far off the spot, the ranged within 30 yd of it.
    {
        FightHold h;
        StartFightHold(h, ZfHold(), 7789);
        Run& r = h.r;
        Facts& f = h.f;
        f.anyInCombat = true;
        f.units = {Engaged(7789, 14, 5, 15.0f)};
        Tick(r, f);
        f.leadStaying = false;
        f.leadX = 28.0f;
        RangedYd(f, kHoldFollowRangedYd); // exactly the limit is within reach
        for (int i = 0; i < 20; ++i)
            Tick(r, f);
        CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1 && r.holdPlaced);
        // The same distance measured on the ground: 3D 40 yd with 15 yd of height is 37 on the ground.
        for (MemberFact& m : f.members)
            if (m.ranged)
            {
                m.yd = 32.0f;
                m.dz = 15.0f; // 28.3 yd on the ground
            }
        for (int i = 0; i < 20; ++i)
            Tick(r, f);
        CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1);
    }
}

// T3: the fight ends with the tank away: the hold walks it back and, once it
// stands on the spot, places the same three bots again (generation 3).
static void TestHoldStandsBackWhenTheTankIsBack()
{
    FightHold h;
    StartFightHold(h, ZfHold(), 7789);
    Run& r = h.r;
    Facts& f = h.f;
    f.anyInCombat = true;
    f.units = {Engaged(7789, 14, 5, 15.0f)};
    Tick(r, f);
    f.leadStaying = false;
    f.leadX = 12.0f;
    RangedYd(f, 36.0f);
    Tick(r, f);
    Tick(r, f);
    CHECK(r.holdStands.empty() && r.holdStandsGen == 2);
    // The fight is over; the hold sends the tank back.
    f.anyInCombat = false;
    f.units = {Unit(7789, 14, 5, false)};
    std::string order;
    for (int i = 0; i < 6 && order.compare(0, 5, "move ") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "move 0.0 0.0 0.0");
    CHECK(r.holdStands.empty() && r.holdStandsGen == 2);
    // It arrives (on stay again).
    f.leadX = 0.0f;
    f.leadStaying = true;
    RangedYd(f, 6.0f);
    for (int i = 0; i < 4 && !r.holdPlaced; ++i)
        Tick(r, f);
    CHECK(r.holdPlaced && r.holdStands.size() == 3 && r.holdStandsGen == 3);
    for (const HoldStand& s : r.holdStands)
        CHECK(s.name == "Quelinator" || s.name == "Rundar" || s.name == "Tobbert");
}

// T4: a dead lead bot is not followed: the places stay as they are (the rez
// and regroup rules apply).
static void TestHoldStandsStayWhenTheLeadIsDead()
{
    FightHold h;
    StartFightHold(h, ZfHold(), 7789);
    Run& r = h.r;
    Facts& f = h.f;
    f.anyInCombat = true;
    f.units = {Engaged(7789, 14, 5, 15.0f)};
    Tick(r, f);
    f.leadX = 40.0f;
    KillLead(f);
    RangedYd(f, 40.0f);
    for (int i = 0; i < 4; ++i)
        Tick(r, f);
    CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1);
}

// T5: only a hold's own fight releases: another step's fight (a stale list
// cannot exist today) and a hold fought before its event is on leave it alone.
static void TestHoldStandsReleaseOnlyInTheHoldStep()
{
    // A pull step's fight, the lead bot 12 yd from the step's point.
    PlanStep pull;
    pull.kind = PlanStepKind::Pull;
    pull.x = 0.0f;
    pull.radius = 10.0f;
    pull.entries = {7787};
    CHECK(ai::NormalizePlanStep(pull) == PlanStepRefusal::None);
    DungeonPlan plan;
    plan.steps = {pull, ZfHold()};
    Run r = Start(&plan, 0);
    r.eventOn = true; // stale
    HoldStand stale;
    stale.name = "Quelinator";
    r.holdStands = {stale};
    r.holdPlaced = true;
    r.holdStandsGen = 4;
    Facts f = Party();
    f.leadIsTank = true;
    f.anyInCombat = true;
    f.leadX = 12.0f;
    RangedYd(f, 36.0f);
    f.units = {Engaged(7787, 14, 5, 15.0f)};
    for (int i = 0; i < 5; ++i)
        Tick(r, f);
    CHECK(r.holdStands.size() == 1 && r.holdStandsGen == 4 && r.holdPlaced);

    // The hold step before its event is on (eventOn false): not touched either.
    DungeonPlan plan2;
    plan2.steps = {Move(-30), ZfHold()};
    Run q = Start(&plan2, 1);
    q.holdStands = {stale};
    q.holdPlaced = true;
    q.holdStandsGen = 4;
    q.eventOn = false;
    Facts g = Party();
    g.leadIsTank = true;
    g.seen = false;
    g.anyInCombat = true;
    g.leadX = 12.0f;
    RangedYd(g, 36.0f);
    g.units = {Engaged(7787, 14, 5, 15.0f)};
    for (int i = 0; i < 5; ++i)
        Tick(q, g);
    CHECK(q.holdStandsGen == 4 && q.holdStands.size() == 1);
}

// T6: the r12-19z29 phase 2 replay (+105.9 .. +156 s): the tank takes 7789 at
// 15 yd, then 8876 at 8.8 and 7789 at 5, and chases down the stairs: 0, 12, 21,
// 28, 31 and 36 yd off the spot, the four ranged 8, 17, 27, 36, 38 and 40 yd
// from it (the live run: 36 yd median, 85 % of the frames on "stay"). The
// places are let go within two fight ticks of the ranged passing 30 yd.
static void TestZfPyramidPhase2Replay()
{
    FightHold h;
    StartFightHold(h, ZfHold(), 7789);
    Run& r = h.r;
    Facts& f = h.f;
    f.anyInCombat = true;
    f.units = {Engaged(7789, 14, 5, 15.0f), Engaged(8876, 9, 0, 8.8f), Engaged(7787, 5, 0, 5.0f)};
    std::string const first = Tick(r, f);
    CHECK(first.compare(0, 7, "attack ") == 0 && Has(r.note, "the tank takes"));
    f.leadStaying = false;
    float const away[] = {0.0f, 12.0f, 21.0f, 28.0f, 31.0f, 36.0f};
    float const behind[] = {8.0f, 17.0f, 27.0f, 36.0f, 38.0f, 40.0f};
    for (int i = 0; i < 6; ++i)
    {
        f.leadX = away[i];
        RangedYd(f, behind[i]);
        Tick(r, f);
        if (i < 4)
            CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1); // 36 yd is the first tick past 30
        else
            CHECK(r.holdStands.empty() && !r.holdPlaced);
    }
    CHECK(r.holdStandsGen == 2);
}

// The Uldaman Keepers' (radius 30) and Archaedas' (radius 45) holds (19z12, the
// stands' origin): the tank goes after a keeper or a statue 17 yd out while the
// ranged stand on their places 17-28 yd from it (within heal and cast range):
// the places stay through many rounds. A tank that chases 35 yd out with the
// ranged still on their places, 36 yd behind it, is out of heal range: the
// places go after two ticks (intended).
static void TestUldamanHoldsKeepTheirPlacesInReach()
{
    for (int variant = 0; variant < 2; ++variant)
    {
        PlanStep hold = variant == 0 ? ArchaedasHold(30.0f) : ArchaedasHold();
        if (variant == 0)
        {
            hold.entry = 1;
            hold.entries = {4857};
        }
        std::uint32_t const entry = variant == 0 ? 4857 : 2748;
        FightHold h;
        StartFightHold(h, hold, entry);
        Run& r = h.r;
        Facts& f = h.f;
        f.anyInCombat = true;
        f.units = {Engaged(entry, 11, 0, 11.0f)};
        std::string const order = Tick(r, f);
        CHECK(order.compare(0, 7, "attack ") == 0);
        f.leadStaying = false;
        std::vector<MemberFact*> ranged;
        for (MemberFact& m : f.members)
            if (m.ranged)
                ranged.push_back(&m);
        CHECK(ranged.size() == 3);
        float const spread[] = {17.0f, 22.0f, 28.0f};
        for (int round = 0; round < 40; ++round)
        {
            f.leadX = round % 3 ? 17.0f : 9.0f;
            for (int i = 0; i < 3; ++i)
            {
                ranged[static_cast<std::size_t>(i)]->yd = spread[i];
                ranged[static_cast<std::size_t>(i)]->dz = 0.0f;
            }
            Tick(r, f);
            CHECK(r.holdStands.size() == 3 && r.holdStandsGen == 1 && r.holdPlaced && r.holdReleases == 0);
        }
        // 35 yd out, the ranged still on their places.
        f.leadX = 35.0f;
        RangedYd(f, 36.0f);
        Tick(r, f);
        CHECK(r.holdStands.size() == 3);
        Tick(r, f);
        CHECK(r.holdStands.empty() && r.holdStandsGen == 2 && !r.holdPlaced && r.holdReleases == 1);
    }
}

static void TestApproachNotReadIsNotNoPath()
{
    // Run 3, baelog pull 7: the walk back to the stand point ended on a tick
    // whose facts were read during that walk (no approach hop asked for), and
    // the run paused with "no walkable path toward the pack" although the
    // navmesh path from the entrance is complete.
    PlanStep pull;
    pull.kind = PlanStepKind::Pull;
    pull.x = -274.05f;
    pull.y = 138.45f;
    pull.z = -47.3f;
    pull.radius = 13.1f;
    pull.entries = {4852, 4851};
    CHECK(ai::NormalizePlanStep(pull) == PlanStepRefusal::None);
    DungeonPlan plan;
    plan.steps = {Move(-236.5f, 54.9f), pull};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.leadX = -236.5f;
    f.leadY = 54.9f;
    f.seen = false;
    f.approachKnown = false;
    f.approachRead = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && Has(r.note, "reading the way") && r.approachHops == 0);
    // The next tick asks the navmesh: the hop goes out.
    f.approachRead = true;
    f.approachKnown = true;
    f.approachX = -249.9f;
    f.approachY = 132.8f;
    f.approachZ = -46.7f;
    CHECK(Tick(r, f) == "move -249.9 132.8 -46.7");
    // Read and no path at all: the stand search comes first.
    Run n = Start(&plan, 1);
    f.approachKnown = false;
    Tick(n, f);
    CHECK(n.state == State::Running && n.approachStandWanted && Has(n.note, "no walkable path toward the pack"));
}

// Zul'Farrak's Bly hold (r14-19z32 slot 2, leg bly, steps 223-225): a hold
// done by its units (no done flag), entries 7604,7608,7606,7605, radius 25,
// 300 s. Bly (7604) is seen friendly after the gossip; 10 s later he, Oro and
// Raven turn hostile and are in the fight on the same tick (the fight branch
// ran, HoldStep never saw an awake unit before the fight). Murta (7608) is
// never in the radius (he died 69 yd off). The three die, the fight ends, the
// tank walks back on the spot: the hold ends there. The live run held 300 s
// twice and paused "event not done after 300 s (3 of 3 down)": the fight
// branch counted the event but never marked it started.
static PlanStep BlyHold()
{
    PlanStep s;
    s.kind = PlanStepKind::Hold;
    s.timeoutS = 300;
    s.radius = 25.0f;
    s.entries = {7604, 7608, 7606, 7605};
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

static void TestBlyHoldEndsWhenTheFightKilledTheCrew()
{
    FightHold h;
    StartFightHold(h, BlyHold(), 7604);
    Run& r = h.r;
    Facts& f = h.f;
    f.units = {Unit(7604, 5, 0, false)};
    for (int i = 0; i < 10; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && !r.eventStarted && r.eventTotal == 1);
    // Hostile and in the fight on the same tick.
    f.anyInCombat = true;
    f.units = {Engaged(7604, 5, 0, 5.0f), Engaged(7606, 6, 2, 6.0f), Engaged(7605, -5, 3, 6.0f)};
    Tick(r, f);
    CHECK(r.step == 1 && r.eventTotal == 3 && r.eventUp == 3);
    CHECK(r.eventStarted);
    f.units = {Engaged(7604, 5, 0, 5.0f), Engaged(7605, -5, 3, 6.0f)};
    f.eventDown = 1;
    Tick(r, f);
    f.units = {Engaged(7604, 5, 0, 5.0f)};
    f.eventDown = 2;
    Tick(r, f);
    CHECK(r.step == 1);
    // The last one dies: "3 of 3 down" on the fight's last tick, then the fight is over.
    f.units.clear();
    f.eventDown = 3;
    Tick(r, f);
    f.anyInCombat = false;
    for (int i = 0; i < 5 && r.step == 1; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2);
    CHECK(Has(r.note, "every unit of it is dead") && Has(r.note, "3 of 3 down"));
}

// The same awake-in-the-fight start must not mark a data hold's encounter
// started: Archaedas's statues may wake before data 2 leaves 0, and a hold
// marked started reads a 0 as "back to 0: the encounter reset" (EventFailed)
// instead of waiting kEventStartMs for the start to take.
static void TestFightDoesNotStartADataHold()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArchaedasHold(), DoorWait()};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.leadIsTank = true;
    f.leadStaying = true;
    f.eventData = 0; // NOT_STARTED: the ritual has not taken yet
    f.units = {Unit(2748, 11, 0, false)};
    Tick(r, f);
    CHECK(r.step == 1 && r.eventOn && !r.eventStarted);
    f.anyInCombat = true;
    f.units = {Engaged(7309, 6, 0, 6.0f)};
    Tick(r, f);
    CHECK(r.eventUp == 1 && !r.eventStarted);
    f.anyInCombat = false;
    f.units = {Unit(2748, 11, 0, false)};
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && !Has(r.note, "event failed"));
}

// The Codex review of 81b9fc71: a units hold whose crew is hostile and in the
// fight on the hold's very first tick (no HoldStep tick before it, so the
// event was never on and the fight branch never counted it): the three die
// and the hold ends with "3 of 3 down". A data hold (flag 8) fought on its
// first tick is left as it was: not on, not started, by the fight alone.
static void TestBlyHoldFoughtOnItsFirstTickEnds()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), BlyHold(), DoorWait()};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.leadIsTank = true;
    f.leadStaying = true;
    f.anyInCombat = true;
    f.units = {Engaged(7604, 5, 0, 5.0f), Engaged(7606, 6, 2, 6.0f), Engaged(7605, -5, 3, 6.0f)};
    CHECK(!r.eventOn);
    Tick(r, f);
    CHECK(r.step == 1 && r.eventOn && r.eventStarted && r.eventTotal == 3);
    f.units.clear();
    f.eventDown = 3;
    Tick(r, f);
    f.anyInCombat = false;
    for (int i = 0; i < 5 && r.step == 1; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2);
    CHECK(Has(r.note, "every unit of it is dead") && Has(r.note, "3 of 3 down"));

    // The fight alone (a party fight near the spot, Bly still friendly) puts
    // the event on but does not start it: only an awake unit does.
    Run q = Start(&plan, 1);
    Facts h = Party();
    h.leadIsTank = true;
    h.leadStaying = true;
    h.anyInCombat = true;
    h.units = {Unit(7604, 5, 0, false)};
    Tick(q, h);
    CHECK(q.step == 1 && q.eventOn && !q.eventStarted);

    DungeonPlan zf;
    zf.steps = {Move(-30), ZfHold(), DoorWait()};
    Run z = Start(&zf, 1);
    Facts g = Party();
    g.leadIsTank = true;
    g.leadStaying = true;
    g.anyInCombat = true;
    g.units = {Engaged(7789, 5, 0, 5.0f)};
    Tick(z, g);
    CHECK(z.step == 1 && !z.eventOn && !z.eventStarted);
}

// Zul'Farrak's Farraki Arena (Champion Razjal the Quick 62498): the run
// leader's rows are `gossip 62498 param 1` then a units hold (no done flag,
// flags 16384 kPlanFlagResetToGossip) with entries 62498,62496,62497,62499,
// radius 35, 600 s. The three champions
// stand faction 35 and friendly, then turn hostile 13 s after the gossip. The
// core script (farraki_arena.cpp ResetArena) makes all three friendly and
// faction 35 again and restores the gossip on a wipe or an evade: the units
// hold must go back to the gossip (the step that started it), not sit out the
// 600 s timeout.
static PlanStep ArenaGossip()
{
    PlanStep s;
    s.kind = PlanStepKind::Gossip;
    s.entry = 62498;
    s.param = 1;
    s.x = 4.0f;
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

static PlanStep ArenaHold()
{
    PlanStep s;
    s.kind = PlanStepKind::Hold;
    s.flags = ai::kPlanFlagResetToGossip;
    s.timeoutS = 600;
    s.radius = 35.0f;
    s.entries = {62498, 62496, 62497, 62499};
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

// The arena crew alive and friendly again with the party in no fight: the
// script's ResetArena. Razjal (62498, the hold's first entry), Kath'zen
// (62496) and Juthza (62497) are back in the radius.
static Facts ArenaReset(const Facts& base)
{
    Facts f = base;
    f.anyInCombat = false;
    f.units = {Unit(62498, 5, 0, false), Unit(62496, 10, 0, false), Unit(62497, 6, 2, false)};
    return f;
}

static void TestArenaHoldResetGoesBackToTheGossip()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    // The event started: Kath'zen turned hostile in the fight.
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.step == 2 && r.eventStarted && r.eventUp == 1);
    // The reset: all three friendly and alive again, no fight. Exactly 5 s of
    // such ticks and the hold goes back to the gossip and pauses (the 4 s
    // test below pins the other side of the boundary).
    f = ArenaReset(f);
    for (int i = 0; i < 5; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Paused);
    CHECK(Has(r.note, "event reset") && Has(r.note, "starts it again at step 2 (gossip)"));
}

static void TestArenaHoldResetNeedsFiveSeconds()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted);
    // 4 s of reset ticks is under the 5 s: still in the hold, not paused.
    f = ArenaReset(f);
    for (int i = 0; i < 4; ++i)
        Tick(r, f);
    CHECK(r.step == 2 && r.state == State::Running);
    CHECK(!Has(r.note, "event failed") && !Has(r.note, "event reset"));
}

static void TestArenaHoldNotStartedIsNoReset()
{
    // The 13 s intro: the units are alive and friendly from the first tick and
    // were never hostile, so the event never started and nothing resets. 20 s
    // of it leaves the hold in place (the timeout rule is unchanged).
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.units = {Unit(62498, 5, 0, false), Unit(62496, 10, 0, false), Unit(62497, 6, 2, false)};
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    CHECK(r.step == 2 && r.state == State::Running && !r.eventStarted);
    CHECK(!Has(r.note, "event failed") && !Has(r.note, "event reset"));
}

static void TestArenaHoldInFightIsNoReset()
{
    // The arena's units are back friendly but the party is still in a fight (a
    // patrol add): no reset until the fight is over.
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted && r.step == 2);
    f = ArenaReset(f);
    f.anyInCombat = true;
    for (int i = 0; i < 10; ++i)
        Tick(r, f);
    CHECK(r.step == 2 && r.state == State::Running && !Has(r.note, "event reset"));
    CHECK(!UnitsHoldResetSeen(r, f, *Current(r)));
}

static void TestArenaWipeRearmsTheGossipOnce()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted && r.step == 2);
    // The wipe recovery: the party is dead/regrouping, the units are back at
    // once, no 5 s wait: the regroup re-arms to the gossip. The tick counts
    // the event's units (CountEvent) before the wipe branch reads them.
    f = ArenaReset(f);
    CountEvent(r, f, *Current(r));
    CHECK(RearmEventOnWipe(r, f));
    CHECK(r.step == 1 && r.eventRetry);
    // A second time in the same run does not re-arm again.
    r.step = 2;
    CHECK(!RearmEventOnWipe(r, f));
}

static void TestArenaWipeRearmsTheGossipOnTheTick()
{
    // The production path: the wipe regroup branch reaches RearmEventOnWipe
    // before the step dispatch does, so the rearm must read this tick's units
    // (the fighting block counted the previous tick's, with the units hostile).
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    // Tick 1: the event started in the fight.
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted && r.step == 2);
    // Tick 2: the party wiped, the script reset the units (friendly, no fight).
    f = ArenaReset(f);
    f.dead = 5;
    f.alive = 0;
    Tick(r, f);
    CHECK(r.step == 1); // the wipe recovery re-armed to the gossip
}

// The party up and back at the hold, the arena reset: the lead bot on the plan's last move point before the hold
// (the regroup's walk back is done there; the hold spot is 30 yd on, in sight).
static Facts UpAtTheHold()
{
    Facts f = ArenaReset(Party());
    f.leadX = -30.0f;
    return f;
}

static void TestArenaSecondWipeGoesBackToTheGossip()
{
    // The first wipe re-arms to the gossip (once). The event's second attempt starts and is wiped too: no second
    // re-arm, but the event WAS started, so the reset is read when the party is back and the crew friendly again.
    // ResetStep clears eventStarted on that wipe; Run::resetHoldStarted is the latch that keeps the start.
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted && r.resetHoldStarted && r.step == 2);
    f = ArenaReset(f);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.step == 1 && r.eventRetry && r.eventRearmed.count(1) == 1);
    // The run is back at the hold (the gossip said the event started again) for its second attempt: it turns
    // hostile and is wiped in it.
    r.step = 2;
    f = Party();
    f.leadX = -30.0f;
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted && r.resetHoldStarted && r.step == 2);
    f = ArenaReset(f);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.step == 2 && !r.eventStarted && r.resetHoldStarted); // no second re-arm, the latch kept
    CHECK(r.eventRearmed.size() == 1);
    // Up at the spot, the whole crew friendly: the reset comes within the 5 s window.
    f = UpAtTheHold();
    for (int i = 0; i < 5; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Paused);
    CHECK(Has(r.note, "event reset") && Has(r.note, "starts it again at step 2 (gossip)"));
}

static void TestArenaResetReadAfterAnOutOfSightRegroupTick()
{
    // The wipe's regroup tick came with the lead bot more than 60 yd from the spot: nothing of the arena was read
    // (no units), so no re-arm. ResetStep cleared eventStarted; once back in range, the latch still says the event
    // had started, and the reset is read (it used to wait out the 600 s).
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), ArenaHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted && r.resetHoldStarted);
    Facts far = ArenaReset(Party());
    far.leadX = 100.0f;
    far.seen = false;
    far.units.clear();
    far.dead = 5;
    far.alive = 0;
    far.leadAlive = false;
    Tick(r, far);
    CHECK(r.step == 2 && !r.eventStarted && r.resetHoldStarted);
    CHECK(r.eventRearmed.empty());
    f = UpAtTheHold();
    for (int i = 0; i < 5; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Paused);
    CHECK(Has(r.note, "event reset"));
}

static void TestArenaHoldWithoutTheFlagIsNoReset()
{
    // The mutation guard: the same reset ticks on a hold whose row does not set
    // kPlanFlagResetToGossip leave the hold in place. The flag is what makes
    // the reset read opt-in, not the shape of the units.
    PlanStep hold = ArenaHold();
    hold.flags = 0;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::None);
    DungeonPlan plan;
    plan.steps = {Move(-30), ArenaGossip(), hold};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.anyInCombat = true;
    f.units = {Engaged(62496, 10, 0, 10.0f)};
    Tick(r, f);
    CHECK(r.eventStarted && r.step == 2);
    f = ArenaReset(f);
    for (int i = 0; i < 10; ++i)
        Tick(r, f);
    CHECK(r.step == 2 && r.state == State::Running);
    CHECK(!Has(r.note, "event reset") && !Has(r.note, "event failed"));
    CHECK(!UnitsHoldResetSeen(r, f, *Current(r)));
}

static void TestArenaHoldWithoutAGossipIsNoReset()
{
    // The flag alone is not enough: the step to go back to must be the gossip
    // that started the event. A flagged hold with no gossip before it (or with
    // a ritual there) reads no reset and waits out its timeout as before.
    {
        DungeonPlan plan;
        plan.steps = {Move(-30), ArenaHold()};
        Run r = Start(&plan, 1);
        Facts f = Party();
        f.anyInCombat = true;
        f.units = {Engaged(62496, 10, 0, 10.0f)};
        Tick(r, f);
        CHECK(r.eventStarted && r.step == 1);
        f = ArenaReset(f);
        for (int i = 0; i < 10; ++i)
            Tick(r, f);
        CHECK(r.step == 1 && r.state == State::Running);
        CHECK(!Has(r.note, "event reset") && !Has(r.note, "event failed"));
        CHECK(!UnitsHoldResetSeen(r, f, *Current(r)));
    }
    {
        PlanStep ritual;
        ritual.kind = PlanStepKind::Ritual;
        ritual.entry = 1;
        ritual.param = 3;
        CHECK(ai::NormalizePlanStep(ritual) == PlanStepRefusal::None);
        DungeonPlan plan;
        plan.steps = {Move(-30), ritual, ArenaHold()};
        Run r = Start(&plan, 2);
        Facts f = Party();
        f.anyInCombat = true;
        f.units = {Engaged(62496, 10, 0, 10.0f)};
        Tick(r, f);
        CHECK(r.eventStarted && r.step == 2);
        f = ArenaReset(f);
        for (int i = 0; i < 10; ++i)
            Tick(r, f);
        CHECK(r.step == 2 && r.state == State::Running);
        CHECK(!Has(r.note, "event reset") && !Has(r.note, "event failed"));
        CHECK(!UnitsHoldResetSeen(r, f, *Current(r)));
    }
}

static void TestUnflaggedUnitsHoldsFriendlyAgainAreNoResets()
{
    // Bly's crew, the Sunken Temple and Stratholme holds: units holds whose
    // first entry counts while still friendly. A crew that evades or drops
    // hostility and turns friendly again is not a reset without the flag, so
    // the hold goes on instead of pausing the run.
    PlanStep bly = BlyHold();
    PlanStep temple;
    temple.kind = PlanStepKind::Hold;
    temple.timeoutS = 300;
    temple.radius = 30.0f;
    temple.entries = {8580};
    CHECK(ai::NormalizePlanStep(temple) == PlanStepRefusal::None);
    PlanStep stratholme;
    stratholme.kind = PlanStepKind::Hold;
    stratholme.timeoutS = 300;
    stratholme.radius = 68.0f;
    stratholme.entries = {10440, 10416, 10417};
    CHECK(ai::NormalizePlanStep(stratholme) == PlanStepRefusal::None);
    struct Case
    {
        PlanStep hold;
        std::uint32_t gossip;
    };
    Case const cases[] = {{bly, 7604}, {temple, 8580}, {stratholme, 10440}};
    for (const Case& c : cases)
    {
        PlanStep gossip;
        gossip.kind = PlanStepKind::Gossip;
        gossip.entry = c.gossip;
        gossip.param = 1;
        CHECK(ai::NormalizePlanStep(gossip) == PlanStepRefusal::None);
        DungeonPlan plan;
        plan.steps = {Move(-30), gossip, c.hold};
        Run r = Start(&plan, 2);
        Facts f = Party();
        f.anyInCombat = true;
        f.units = {Engaged(c.hold.entries.front(), 10, 0, 10.0f)};
        Tick(r, f);
        CHECK(r.step == 2 && r.eventStarted);
        f.anyInCombat = false;
        f.units = {Unit(c.hold.entries.front(), 5, 0, false)};
        for (int i = 0; i < 10; ++i)
            Tick(r, f);
        CHECK(r.step == 2 && r.state == State::Running);
        CHECK(!Has(r.note, "event reset") && !Has(r.note, "event failed"));
        CHECK(!UnitsHoldResetSeen(r, f, *Current(r)));
    }
}

static void TestBlyHoldUnchangedByTheResetRule()
{
    // Bly's crew turns hostile and dies in the fight: "event done" as today.
    {
        FightHold h;
        StartFightHold(h, BlyHold(), 7604);
        Run& r = h.r;
        Facts& f = h.f;
        f.anyInCombat = true;
        f.units = {Engaged(7604, 5, 0, 5.0f), Engaged(7606, 6, 2, 6.0f), Engaged(7605, -5, 3, 6.0f)};
        Tick(r, f);
        CHECK(r.eventStarted && r.step == 1);
        f.units = {Engaged(7604, 5, 0, 5.0f)};
        f.eventDown = 2;
        Tick(r, f);
        f.units.clear();
        f.eventDown = 3;
        Tick(r, f);
        CHECK(r.step == 1);
        f.anyInCombat = false;
        for (int i = 0; i < 5 && r.step == 1; ++i)
            Tick(r, f);
        CHECK(r.step == 2 && Has(r.note, "every unit of it is dead"));
    }
    // Bly alive and hostile for 10 s: the hold's units are not friendly, so
    // nothing resets (the awake unit is the event, not a reset).
    {
        FightHold h;
        StartFightHold(h, BlyHold(), 7604);
        Run& r = h.r;
        Facts& f = h.f;
        f.anyInCombat = true;
        f.units = {Engaged(7604, 5, 0, 5.0f)};
        for (int i = 0; i < 10; ++i)
            Tick(r, f);
        CHECK(r.step == 1 && r.state == State::Running && !Has(r.note, "event reset"));
    }
}


// Scarlet Monastery's Mograine 3976 and Whitemane 3977 as one hold (SM-CATHEDRAL-MOGRAINE-2026-10-23 fix 3, the plan
// shape turtle-agent writes for a boss hold with a value): instance data 1, value 4, flag 8 alone, timeout 600 s. The
// pair's data keeps its own numbers (1 in progress, 2 Mograine died once, 3 revived, 4 both dead): 2 is no FAIL, 3 is
// no DONE, 0 is no reset; only 4 ends the hold. The fight is the hold's: it is bounded by the 600 s timeout, not by
// the boss ceiling, and an unattackable unit (Mograine's fake death) never ends it.
static PlanStep MograineHold()
{
    PlanStep s;
    s.kind = PlanStepKind::Hold;
    s.entry = 1;
    s.param = 4;
    s.flags = ai::kPlanFlagDoneData;
    s.timeoutS = 600;
    s.radius = 35.0f;
    s.entries = {3976, 3977};
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

// #243 round 1: Zul'Farrak's Bly hold done by its units with kPlanFlagDeadGossipOk (32768): the gossip row before it counted its
// dead NPC as done (dead_is_done, 512), so the crew can never be turned hostile (only Bly's own script does, and he lies dead).
// The hold then ends as soon as no unit of it is hostile: at once when all four lie dead, and with the same note when only
// friendly survivors are left. Without the gossip's dead note (a run resumed at the hold) it waits for a hostility as before.
static const char* const kCrewDeadAnswer =
    "no-npc: No living NPC of that entry is within 40 yd of the bot: a dead one lies nearby: Sergeant Bly (7604) "
    "lies dead 3 yd away and respawns in 6 d 23 h (604800 s spawn timer).";

static PlanStep DeadBlyGossip()
{
    PlanStep s;
    s.kind = PlanStepKind::Gossip;
    s.entry = 7604;
    s.param = 1;
    s.flags = ai::kPlanFlagDeadNpcIsDone;
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

static PlanStep BlyHoldDeadGossip()
{
    PlanStep s = BlyHold();
    s.flags = ai::kPlanFlagDeadGossipOk;
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

// The gossip row ran to its dead-NPC "counted done": the run stands on the flagged hold (step 2).
static Run StartAfterTheDeadGossip(const DungeonPlan* plan)
{
    Run r = Start(plan, 1);
    Facts f = Party();
    std::string const order = Tick(r, f);
    CHECK(order.compare(0, 11, "gossip 7604") == 0);
    OrderRefused(r, order, kCrewDeadAnswer);
    CHECK(r.state == State::Running && r.step == 2);
    CHECK(Has(r.note, "counted done"));
    return r;
}

static void TestDeadGossipHoldEndsAtOnceWhenAllLieDead()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), DeadBlyGossip(), BlyHoldDeadGossip(), Move(30)};
    Run r = StartAfterTheDeadGossip(&plan);
    Facts f = Party();
    f.units.clear();
    f.eventDown = 4; // the four corpses are in sight
    Tick(r, f);
    CHECK(r.step == 3 && r.state == State::Running);
    CHECK(Has(r.note, "event done: the NPC that starts it was dead and no unit of it is hostile"));
    CHECK(Has(r.note, "(4 of 4 down)"));
}

static void TestDeadGossipHoldEndsWithFriendlySurvivors()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), DeadBlyGossip(), BlyHoldDeadGossip(), Move(30)};
    Run r = StartAfterTheDeadGossip(&plan);
    Facts f = Party();
    // Bly died; Murta and Oro stand friendly (faction 35) and cannot be turned: no pull reaches them.
    f.units = {Unit(7608, 5, 0, false), Unit(7606, 10, 0, false)};
    f.eventDown = 2;
    Tick(r, f);
    CHECK(r.step == 3 && r.state == State::Running);
    CHECK(Has(r.note, "event done: the NPC that starts it was dead and no unit of it is hostile"));
    CHECK(Has(r.note, "(2 of 2 down)"));
}

static void TestDeadGossipHoldWithoutTheDeadNoteWaits()
{
    // The latch is the gossip row's own tick: a run resumed at the hold (Start past the gossip) never saw it, so the flag
    // alone changes nothing and the hold waits for a unit to turn hostile, as an unflagged units hold does.
    DungeonPlan plan;
    plan.steps = {Move(-30), DeadBlyGossip(), BlyHoldDeadGossip(), Move(30)};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.units = {Unit(7608, 5, 0, false), Unit(7606, 10, 0, false)};
    f.eventDown = 2;
    for (int i = 0; i < 10; ++i)
        Tick(r, f);
    CHECK(r.step == 2 && r.state == State::Running);
    CHECK(!Has(r.note, "the NPC that starts it was dead"));
}

static void TestDeadGossipHoldWithoutAStartStepDoesNotEnd()
{
    // A flagged hold with no gossip before it (the plan starts at the hold): there is no startNpcDeadStep and no
    // event start step, both npos. They must not match: the hold waits for a hostile unit, as the rule above does.
    DungeonPlan plan;
    plan.steps = {Move(-30), BlyHoldDeadGossip(), Move(30)};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.units = {Unit(7608, 5, 0, false), Unit(7606, 10, 0, false)};
    f.eventDown = 2;
    for (int i = 0; i < 10; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running);
    CHECK(r.startNpcDeadStep == static_cast<std::size_t>(-1) && EventStartStep(r) == static_cast<std::size_t>(-1));
    CHECK(!Has(r.note, "the NPC that starts it was dead"));
}

static void TestMograineWhitemaneHoldWaitsForValueFour()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), MograineHold(), DoorWait()};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.units = {Unit(3976, 14, 0, true)};
    // 0 (not started), 1, 2 (died once), 3 (revived), and 0 again: none ends or fails the hold.
    for (long long data : {0LL, 1LL, 2LL, 3LL, 0LL, 2LL})
    {
        f.eventData = data;
        for (int i = 0; i < 40; ++i)
            Tick(r, f);
        CHECK(r.state == State::Running && r.step == 1);
        CHECK(!Has(r.note, "event failed") && !Has(r.note, "event done"));
        CHECK(r.eventMs < 600000u);
    }
    // In a fight past the boss ceiling (420 s): the hold's 600 s bound it, and an unattackable engaged unit (Mograine
    // unselectable in his fake death) does not end it.
    f.eventData = 2;
    f.anyInCombat = true;
    f.engagedRead = true;
    UnitFact mograine = Unit(3976, 14, 0, true);
    mograine.engaged = true;
    mograine.attackable = false;
    f.engagedUnits = {mograine};
    r.fighting = true;
    r.fightMs = kBossFightCapMs + 10000;
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.fighting);
    CHECK(!UnattackableFightLetGo(r, f));
    // 4: both dead, the hold is done, in the fight too.
    f.eventData = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2); // the fight's note follows on the same tick
    // ... and out of a fight, with its note.
    Run d = Start(&plan, 1);
    Facts h = Party();
    h.units = {Unit(3976, 14, 0, true)};
    h.eventData = 3;
    Tick(d, h);
    CHECK(d.step == 1);
    h.eventData = 4;
    Tick(d, h);
    CHECK(d.state == State::Running && d.step == 2); // the door wait runs on the same tick
    CHECK(HoldDoneNote(MograineHold(), h) == "event done: data 1 reads 4");
    h.eventData = 3;
    CHECK(HoldDoneNote(MograineHold(), h).empty());
    // The same row with the encounter flag (the default boss hold) would have failed on data 2.
    DungeonPlan enc;
    PlanStep e = MograineHold();
    e.param = 3;
    e.flags = ai::kPlanFlagDoneData | ai::kPlanFlagEncounter;
    enc.steps = {Move(-30), e, DoorWait()};
    Run x = Start(&enc, 1);
    Facts g = Party();
    g.units = {Unit(3976, 14, 0, true)};
    g.eventData = 2;
    Tick(x, g);
    CHECK(x.state == State::Paused && Has(x.note, "event failed"));
}

// The Dire Maul West crystal holds (spec dmw-crystal-done 5b): a hold done by its
// object reads the object's state in its waiting notes. Entry 177259, done when
// ACTIVE (1).
static PlanStep CrystalHold(std::uint32_t timeoutS = 2)
{
    PlanStep s;
    s.kind = PlanStepKind::Hold;
    s.entry = 177259;
    s.param = 1;
    s.flags = ai::kPlanFlagDoneObject;
    s.timeoutS = timeoutS;
    s.radius = 25.0f;
    s.entries = {11483, 11480};
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

// T1 (the r39s2 case): the crystal still reads READY (0) while the hold waits
// for ACTIVE (1). The hold runs to its timeout and its pause note shows the
// read beside the wait.
static void TestCrystalHoldWaitsForActive()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), CrystalHold(), Move(30)};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.units = {Unit(11483, 11, 0, true)};
    f.objectState = 0;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.phase == Phase::Hold);
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(Has(r.note, "event: holding the spot"));
    CHECK(Has(r.note, "object 177259 reads 0 ready, waits for 1 active"));
    Tick(r, f);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 1);
    CHECK(Has(r.note, "event not done after 2 s"));
    CHECK(Has(r.note, "object 177259 reads 0 ready, waits for 1 active"));
}

// T2 (the r39 step-491 case): the crystal is not found within 30 yd of the
// spot. The notes say so instead of a read.
static void TestCrystalHoldMissingObject()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), CrystalHold(), Move(30)};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.units = {Unit(11483, 11, 0, true)};
    f.objectState = -1;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.phase == Phase::Hold);
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(Has(r.note, "object 177259 not found within 30 yd of the spot"));
    Tick(r, f);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 1);
    CHECK(Has(r.note, "object 177259 not found within 30 yd of the spot"));
}

// T3: the crystal reads ACTIVE (1): the hold ends, with the existing note.
static void TestCrystalHoldDoneWhenActive()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), CrystalHold(), Move(30)};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.units = {Unit(11483, 11, 0, true)};
    f.objectState = 1;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2);
    CHECK(HoldDoneNote(CrystalHold(), f) == "event done: object 177259 is in state 1");
}

// The mutation guard for the spec's "append for non-object holds" mutation: a
// units hold's exact notes carry no object read. If the read ever leaked into
// a non-object hold, this exact-note check fails.
static void TestUnitsHoldNotesCarryNoObjectRead()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), BlyHold(), Move(30)};
    Run r = Start(&plan, 1);
    Facts f = Party();
    f.leadIsTank = true;
    f.leadStaying = true;
    f.units = {Unit(7604, 11, 0, true)};
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.phase == Phase::Hold);
    CHECK(r.note == "event: holding the spot (0 of 1 down, 1 awake)");
    f.units.clear();
    f.eventDown = 1;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2);
    CHECK(r.note == "event done: every unit of it is dead (1 of 1 down)");
}

// Review round 3 Opus fix 2: while the run holds at a closed door (doorPaused)
// the per-tick hold-done rule must not end the pause: only the door read open
// (DoorOpenWhilePaused) or the skip lets it on.
static void TestPausedHoldDoneLeavesADoorHold()
{
    DungeonPlan plan;
    plan.steps = {Move(-30), ArchaedasHold(), DoorWait()};
    Run r = Start(&plan, 1);
    PauseDoor(r, 0, "open it with Door Opener (item 1)", "the way is locked: the bots wait before it");
    CHECK(r.state == State::Paused && r.doorPaused);
    Facts f = Party();
    f.seen = false;
    f.eventData = 3; // the hold's own event done, but the door still shut
    CHECK(!HoldDoneWhilePaused(r, f));
    CHECK(r.state == State::Paused && r.doorPaused && r.step == 1);
}

int main()
{
    TestHoldDoneSeenFromAway();
    TestPausedHoldDoneResumes();
    TestLeadDeadInHoldRegroups();
    TestHoldStandsPlaceRangedBehindTank();
    TestHoldStandsLetGoWhenTheRangedFallBehindTheTank();
    TestHoldStandsKeptUnlessBothAreFar();
    TestHoldStandsBackWhenTheTankIsBack();
    TestHoldStandsStayWhenTheLeadIsDead();
    TestHoldStandsReleaseOnlyInTheHoldStep();
    TestZfPyramidPhase2Replay();
    TestUldamanHoldsKeepTheirPlacesInReach();
    TestApproachNotReadIsNotNoPath();
    TestBlyHoldEndsWhenTheFightKilledTheCrew();
    TestFightDoesNotStartADataHold();
    TestBlyHoldFoughtOnItsFirstTickEnds();
    TestArenaHoldResetGoesBackToTheGossip();
    TestArenaHoldResetNeedsFiveSeconds();
    TestArenaHoldNotStartedIsNoReset();
    TestArenaHoldInFightIsNoReset();
    TestArenaWipeRearmsTheGossipOnce();
    TestArenaWipeRearmsTheGossipOnTheTick();
    TestArenaSecondWipeGoesBackToTheGossip();
    TestArenaResetReadAfterAnOutOfSightRegroupTick();
    TestArenaHoldWithoutTheFlagIsNoReset();
    TestArenaHoldWithoutAGossipIsNoReset();
    TestUnflaggedUnitsHoldsFriendlyAgainAreNoResets();
    TestBlyHoldUnchangedByTheResetRule();
    TestDeadGossipHoldEndsAtOnceWhenAllLieDead();
    TestDeadGossipHoldEndsWithFriendlySurvivors();
    TestDeadGossipHoldWithoutTheDeadNoteWaits();
    TestDeadGossipHoldWithoutAStartStepDoesNotEnd();
    TestMograineWhitemaneHoldWaitsForValueFour();
    TestCrystalHoldWaitsForActive();
    TestCrystalHoldMissingObject();
    TestCrystalHoldDoneWhenActive();
    TestUnitsHoldNotesCarryNoObjectRead();
    TestPausedHoldDoneLeavesADoorHold();
    std::cout << "test_dungeon_hold_policy: ok\n";
    return 0;
}
