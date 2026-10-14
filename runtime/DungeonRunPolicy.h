#pragma once

// The run leader (`.bot run`, docs/guides/dungeon-run-leader.md): an opt-in
// driver that walks a bound party through a dungeon plan
// (ai/playerbot/DungeonPlanPolicy.h) by itself, behind
// AiPlayerbot.DungeonSelfClear (default 0).
//
// It gives the party ONLY the remote commander's own orders (`cmd move`,
// `move ... direct`, `pullback`, `rest`, `loot`, `open`, `use`), so every
// commander fix also fixes the run leader and the two cannot drift apart. The
// fights themselves are the bots' own combat AI (with the boss tactics table).
//
// This header is the whole decision: one Run per commander, advanced once a
// second by Tick() with a Facts snapshot that the world glue in
// commands/BotCommands.cpp reads from the party and the plan step's
// surroundings. Tick() answers with at most one order (the text after `.bot
// cmd `); the glue sends it as the commander and reports a refusal back with
// OrderRefused(). No game types here: tools/test_dungeon_run_policy.cpp runs it
// on its own.
//
// Safety rules, carried over from turtle-agent's remote driver (its plan
// stepper is the owner's MIT code; see docs/PROVENANCE.md):
//   - gather before a pull: every living bot within kGatherYd of the lead bot;
//   - walk in the plan's hops; before each hop, pull what stands near it, but
//     never what a later pull or boss step owns (OwnedByLaterStep);
//   - a pack or boss counts as done only when the lead bot stands within
//     kSeeYd of it and no living, hostile unit of it is left;
//   - kWipeDead dead (or the lead bot dead) pauses the run to regroup;
//   - every wait has a ceiling (gather, walk, pull, fight, rest, task, object
//     wait, and kStepIdleCapMs per step); a ceiling pauses the run with the
//     reason, it never loops;
//   - any order from a player to the party pauses the run until `resume`.

#include "../ai/playerbot/DungeonPlanPolicy.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace DungeonRun {

using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

constexpr std::uint32_t kTickMs = 1000;           // the glue ticks every run this often
constexpr std::uint32_t kWipeDead = 3;            // this many dead bots pause the run
constexpr float kArriveYd = 3.0f;                 // a walk is done this close to its point
constexpr float kStalledArriveYd = 6.0f;          // ... or this close once it stopped gaining ground (a point
                                                  // just off the mesh or on a ledge the bot cannot reach)
constexpr float kSeeYd = 60.0f;                   // a pack, boss or spot is judged only from this close
constexpr float kApproachYd = 30.0f;              // an unseen pack is approached to this far from its centre
constexpr float kApproachHopYd = 40.0f;           // ... in navmesh path hops of at most this length
constexpr std::uint32_t kMaxApproachHops = 8;     // ... and at most this many, then pause
constexpr float kDetourMaxYd = 15.0f;             // an unreachable move point may be stood in for by a
                                                  // reachable one at most this far from it
constexpr float kGatherYd = 20.0f;                // every living bot this close to the lead before a pull
constexpr float kHopPullYd = 15.0f;               // units this close to the next hop are pulled first
constexpr std::uint32_t kGatherCapMs = 30000;
constexpr std::uint32_t kWalkRetryMs = 12000;     // no progress for this long: send the move again
constexpr std::uint32_t kWalkTries = 3;           // ... at most this often, then pause
constexpr float kWalkProgressYd = 2.0f;
constexpr std::uint32_t kPullNoFightMs = 25000;   // a pull that started no fight in this time failed
constexpr std::uint32_t kPullTriesPerUnit = 2;    // a unit two pulls could not start a fight with is passed
constexpr std::uint32_t kMaxPullsPerTarget = 3;  // a unit three fought pulls did not kill is out of reach
constexpr std::uint32_t kFruitlessPulls = 3;      // three pulls in a row that kill nothing: the rest of the pack is out of reach
constexpr std::uint32_t kMaxPullsPerStep = 20;
constexpr std::uint32_t kRegroupReviveMs = 240000; // after a resume: the dead must be up within this
constexpr std::uint32_t kRegroupGatherMs = 60000;  // ... and the party gathered within this after that
constexpr std::uint32_t kFightCapMs = 180000;
constexpr std::uint32_t kBossFightCapMs = 420000;
constexpr std::uint32_t kRestCapMs = 60000;
constexpr std::uint32_t kRestRepeatMs = 20000;
constexpr std::uint32_t kTaskCapMs = 90000;       // loot, open, use: the commander task's own budget is shorter
constexpr std::uint32_t kStepIdleCapMs = 300000;  // out-of-combat time one step may take

// Rest gate: it starts when someone is below a "start" line after a fight or
// before a step, and ends when everybody is at the "end" line (or at kRestCapMs).
struct RestLines
{
    std::uint32_t startHealth = 80, startHealerMana = 50, startMana = 35;
    std::uint32_t endHealth = 90, endHealerMana = 80, endMana = 60;
};

enum class State : std::uint8_t { Running, Paused, Done };
enum class Phase : std::uint8_t { Begin, Gather, Walk, AwaitPull, AwaitTask, Rest, Wait };

inline const char* StateName(State s)
{
    switch (s)
    {
        case State::Running: return "running";
        case State::Paused: return "paused";
        default: return "done";
    }
}

inline const char* PhaseName(Phase p)
{
    switch (p)
    {
        case Phase::Begin: return "begin";
        case Phase::Gather: return "gather";
        case Phase::Walk: return "walk";
        case Phase::AwaitPull: return "pull";
        case Phase::AwaitTask: return "task";
        case Phase::Rest: return "rest";
        default: return "wait";
    }
}

// A creature near the step, as the glue saw it this tick.
struct UnitFact
{
    std::uint64_t guid = 0;
    std::uint32_t entry = 0;
    float distLead = 0.0f;
    bool hostile = true;     // not friendly to the party (hostile or neutral): it may be pulled
    bool stealthed = false;
    bool evading = false;    // running home: not pulled this tick
};

// What the glue reads once a tick for the run's current step.
struct Facts
{
    // The party (the commander's bound bots, the lead bot included).
    std::uint32_t total = 0, alive = 0, dead = 0;
    bool leadAlive = true;
    bool anyInCombat = false;
    float leadX = 0.0f, leadY = 0.0f, leadZ = 0.0f;
    float maxMemberYd = 0.0f;           // the living bot farthest from the lead bot
    std::uint32_t minHealthPct = 100;   // living bots
    std::uint32_t minHealerManaPct = 100;
    std::uint32_t minManaPct = 100;     // mana users that are not healers
    // The step.
    std::uint32_t team = 0;             // the party's team: ai::kPlanFlagAlliance or kPlanFlagHorde (0 unknown)
    bool seen = false;                  // the lead bot stands within kSeeYd of the step's point
    // pull/boss not seen yet: the next approach hop, a point of the lead bot's
    // navmesh path toward the step's point (kApproachHopYd along it at most,
    // kApproachYd short of the point). approachKnown false: no path.
    bool approachKnown = false;
    float approachX = 0.0f, approachY = 0.0f, approachZ = 0.0f;
    std::vector<UnitFact> units;        // pull/boss: living units of the step's entries within its radius
    std::vector<UnitFact> hopUnits;     // move: living hostile units within kHopPullYd of the point, not owned by a later step
    int objectState = -1;               // wait_object: 0 ready, 1 active, 2 active_alt; -1 not found
    std::uint32_t hostilesNear = 0;     // wait_clear: living hostiles within the radius
    std::string carrier;                // the bot that does object and item steps (the lead bot)
    bool carrierHasItem = false;        // the step's `item` is in the carrier's bags
    bool carrierHasEntryItem = false;   // use_item: the item to use is in the bags
    bool carrierBusy = false;           // a commander task (loot/open/use) runs for the carrier
    int taskReply = 0;                  // the carrier's task answer since the run's order: 0 none, 1 ACK, 2 ERR
    std::string taskText;
};

struct Run
{
    const DungeonPlan* plan = nullptr;
    std::size_t step = 0;
    State state = State::Running;
    Phase phase = Phase::Begin;
    std::string note;        // what it does now, or why it paused

    std::uint32_t phaseMs = 0;
    std::uint32_t stepIdleMs = 0;
    bool restDue = true;     // check the rest lines before the step
    bool stepRested = false; // a `rest` step has had its gate
    bool orderSent = false;  // task steps: the order went out
    bool walkedThrough = false;
    std::uint32_t approachHops = 0;
    // A move whose point the navmesh cannot reach walks to the nearest
    // reachable point near it instead (UseDetour).
    bool detour = false;
    float detourX = 0.0f, detourY = 0.0f, detourZ = 0.0f;
    bool fighting = false;
    std::uint32_t fightMs = 0;
    std::uint32_t restRepeatMs = 0;
    // Walk.
    float walkX = 0.0f, walkY = 0.0f, walkZ = 0.0f;
    std::uint32_t walkTries = 0;
    std::uint32_t walkStillMs = 0;
    float walkBestYd = 0.0f;
    // Pulls.
    std::uint64_t pullGuid = 0;
    std::uint32_t pulls = 0;
    std::unordered_map<std::uint64_t, std::uint32_t> pullTries;
    std::unordered_set<std::uint64_t> passed;
    std::size_t openAtLastPull = 0;
    std::uint32_t fruitlessPulls = 0;
    // Run-wide (kept across steps).
    std::unordered_set<std::uint64_t> unreachable;   // units given up on: never pulled again this run
    std::unordered_set<std::uint32_t> missing;       // items the run did not get (a skipped loot or use)
    std::uint32_t pendingMissing = 0;                 // the item a paused step failed to get (missing on skip)
    std::size_t skippedOpenStep = static_cast<std::size_t>(-1); // an `open` left out: the wait after it goes too
    // Regroup after a resume (revive the dead, gather), before the step.
    bool regroup = false;
    bool reviveSent = false;
    bool regroupGatherSent = false;
    std::uint32_t regroupMs = 0;
};

inline const PlanStep* Current(const Run& r)
{
    return r.plan && r.step < r.plan->steps.size() ? &r.plan->steps[r.step] : nullptr;
}

inline std::string Num(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}

inline std::string MoveOrder(float x, float y, float z, bool direct)
{
    return "move " + Num(x) + " " + Num(y) + " " + Num(z) + (direct ? " direct" : "");
}

inline std::string EntryArg(std::uint32_t entry, float x, float y, float z)
{
    return "entry:" + std::to_string(entry) + "@" + Num(x) + "," + Num(y) + "," + Num(z);
}

inline void EnterPhase(Run& r, Phase p)
{
    r.phase = p;
    r.phaseMs = 0;
}

inline void Pause(Run& r, const std::string& why)
{
    r.state = State::Paused;
    r.note = why;
}

// Clears what belongs to one step (also on resume, so a resumed step gets
// fresh tries and ceilings).
inline void ResetStep(Run& r)
{
    EnterPhase(r, Phase::Begin);
    r.stepIdleMs = 0;
    r.restDue = true;
    r.stepRested = false;
    r.orderSent = false;
    r.walkedThrough = false;
    r.approachHops = 0;
    r.detour = false;
    r.walkTries = 0;
    r.walkStillMs = 0;
    r.walkBestYd = 0.0f;
    r.walkX = r.walkY = r.walkZ = 0.0f;
    r.pullGuid = 0;
    r.pulls = 0;
    r.pullTries.clear();
    r.passed.clear();
    r.openAtLastPull = 0;
    r.fruitlessPulls = 0;
}

inline void NextStep(Run& r, const std::string& note)
{
    r.pendingMissing = 0;
    ++r.step;
    ResetStep(r);
    r.note = note;
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = "plan done";
    }
}

inline Run Start(const DungeonPlan* plan, std::size_t fromStep = 0)
{
    Run r;
    r.plan = plan;
    r.step = fromStep;
    ResetStep(r);
    r.note = "started";
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = "plan has no steps";
    }
    return r;
}

// `.bot run resume`: the party regroups first (the dead are revived, the
// living gather on the lead bot), then the paused step starts over with fresh
// tries; a lead bot far from it walks back on the navmesh (approach hops).
inline bool Resume(Run& r)
{
    if (r.state != State::Paused || !Current(r))
        return false;
    r.state = State::Running;
    r.fighting = false;
    ResetStep(r);
    r.regroup = true;
    r.reviveSent = false;
    r.regroupGatherSent = false;
    r.regroupMs = 0;
    r.note = "resumed: regrouping";
    return true;
}

// `.bot run skip`: leaves the current step out and goes on running (from a
// pause too, with the regroup of a resume). An item the step failed to get is
// noted missing: later steps that need it are left out with a note instead
// of failing one after the other.
inline bool Skip(Run& r)
{
    const PlanStep* s = Current(r);
    if (r.state == State::Done || !s)
        return false;
    bool const wasPaused = r.state == State::Paused;
    if (r.pendingMissing)
        r.missing.insert(r.pendingMissing);
    if (s->kind == PlanStepKind::Open)
        r.skippedOpenStep = r.step;
    std::size_t const skipped = r.step + 1;
    r.state = State::Running;
    NextStep(r, "skipped step " + std::to_string(skipped));
    if (wasPaused && r.state == State::Running)
    {
        r.regroup = true;
        r.reviveSent = false;
        r.regroupGatherSent = false;
        r.regroupMs = 0;
    }
    return true;
}

// Any order from a player to the party.
inline bool PauseForPlayer(Run& r, const std::string& order)
{
    if (r.state != State::Running)
        return false;
    Pause(r, "player order (" + order + "): .bot run resume to go on");
    return true;
}

// The glue's order was refused (ERR): pause with the module's own words. A
// refused `rest` (nobody had food or drink to use) only notes it: the rest
// gate's own ceiling ends the wait.
inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)
{
    if (order == "rest" || order == "revive party")
    {
        // The rest gate and the regroup have their own ceilings.
        r.note = order + " refused: " + answer;
        return;
    }
    const PlanStep* s = Current(r);
    if (s && s->kind == PlanStepKind::UseItem && s->item && !r.missing.empty())
    {
        // A use that cannot work because an earlier item is missing (a
        // reagent): a consequence, not a second failure.
        r.missing.insert(s->item);
        NextStep(r, "use_item " + std::to_string(s->entry) + " refused (" + answer + "): an earlier item is missing; " +
            "item " + std::to_string(s->item) + " is noted missing too");
        return;
    }
    if (s && (s->kind == PlanStepKind::Loot || s->kind == PlanStepKind::UseItem) && s->item)
        r.pendingMissing = s->item;
    Pause(r, "cmd " + order + " refused: " + answer +
        (r.pendingMissing ? " (item " + std::to_string(r.pendingMissing) +
            " missing: .bot run skip leaves out the steps that need it)" : ""));
}

inline bool NeedsRest(const Facts& f, const RestLines& l)
{
    return f.minHealthPct < l.startHealth || f.minHealerManaPct < l.startHealerMana || f.minManaPct < l.startMana;
}

inline bool Rested(const Facts& f, const RestLines& l)
{
    return f.minHealthPct >= l.endHealth && f.minHealerManaPct >= l.endHealerMana && f.minManaPct >= l.endMana;
}

// Walks the party to a point. True once the lead bot is there; otherwise
// `order` may hold the move to send. Pauses after kWalkTries moves without
// progress.
inline bool WalkTo(Run& r, const Facts& f, float x, float y, float z, bool direct, std::uint32_t dt,
    std::string& order)
{
    float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, x, y, z);
    if (dist <= kArriveYd)
        return true;
    bool const fresh = r.phase != Phase::Walk || r.walkX != x || r.walkY != y || r.walkZ != z;
    if (fresh)
    {
        EnterPhase(r, Phase::Walk);
        r.walkX = x;
        r.walkY = y;
        r.walkZ = z;
        r.walkTries = 1;
        r.walkStillMs = 0;
        r.walkBestYd = dist;
        order = MoveOrder(x, y, z, direct);
        return false;
    }
    r.walkStillMs += dt;
    if (dist < r.walkBestYd - kWalkProgressYd)
    {
        r.walkBestYd = dist;
        r.walkStillMs = 0;
    }
    if (r.walkStillMs >= kWalkRetryMs && dist <= kStalledArriveYd)
        return true;
    if (r.walkStillMs >= kWalkRetryMs)
    {
        if (r.walkTries >= kWalkTries)
        {
            Pause(r, "the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z));
            return false;
        }
        ++r.walkTries;
        r.walkStillMs = 0;
        order = MoveOrder(x, y, z, direct);
    }
    return false;
}

// The unit to pull now: hostile, not evading, not passed, not stealthed;
// nearest to the lead bot. When `entry` is not 0 only that entry. Null for none.
inline const UnitFact* PickPull(const Run& r, const std::vector<UnitFact>& units, std::uint32_t entry)
{
    const UnitFact* best = nullptr;
    for (const UnitFact& u : units)
    {
        if (!u.hostile || u.evading || u.stealthed || r.passed.count(u.guid) || r.unreachable.count(u.guid))
            continue;
        if (entry && u.entry != entry)
            continue;
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    return best;
}

// Units the step still has to deal with: hostile, alive (the glue lists only
// living ones), not passed. Evading ones count (they come back).
inline std::size_t OpenUnits(const Run& r, const std::vector<UnitFact>& units)
{
    std::size_t n = 0;
    for (const UnitFact& u : units)
        if (u.hostile && !r.passed.count(u.guid) && !r.unreachable.count(u.guid))
            ++n;
    return n;
}

// Gathers the party, then sends `pullback <guid>`.
inline void PullUnit(Run& r, const Facts& f, const UnitFact& u, std::string& order)
{
    if (f.maxMemberYd > kGatherYd)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "gathering before a pull";
            // The lead bot holds where it stands and the others form up on it.
            order = MoveOrder(f.leadX, f.leadY, f.leadZ, false);
        }
        else if (r.phaseMs >= kGatherCapMs)
            Pause(r, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) + " s (a bot is " +
                Num(f.maxMemberYd) + " yd from the lead bot)");
        return;
    }
    if (r.pulls >= kMaxPullsPerStep)
    {
        Pause(r, std::to_string(kMaxPullsPerStep) + " pulls did not clear the step");
        return;
    }
    std::uint32_t& tries = r.pullTries[u.guid];
    if (tries >= kMaxPullsPerTarget)
    {
        // Each pull started a fight, yet the unit lives: it evades, leashes
        // home or stands where the party cannot reach. Never loop on it.
        r.unreachable.insert(u.guid);
        r.note = "unit " + std::to_string(u.entry) + " survived " + std::to_string(kMaxPullsPerTarget) +
            " pulls: left as out of reach";
        return;
    }
    ++tries;
    ++r.pulls;
    r.pullGuid = u.guid;
    EnterPhase(r, Phase::AwaitPull);
    r.note = "pulling " + std::to_string(u.entry);
    order = "pullback " + std::to_string(u.guid);
}

// The step's pull is out: wait for the fight (the fight gate takes over) or
// give the pull up after kPullNoFightMs.
inline void AwaitPull(Run& r)
{
    if (r.phaseMs < kPullNoFightMs)
        return;
    auto it = r.pullTries.find(r.pullGuid);
    if (it != r.pullTries.end() && it->second >= kPullTriesPerUnit)
        r.passed.insert(r.pullGuid);
    r.pullGuid = 0;
    EnterPhase(r, Phase::Begin);
}

// Walks toward a step whose point is out of sight, one navmesh hop at a time
// (the glue's approach point). A hop is fixed when its walk starts; once there
// the next tick asks for the next one. Bounded by kMaxApproachHops.
inline void ApproachHop(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order,
    const std::string& what)
{
    r.note = "approaching the " + what;
    if (r.phase == Phase::Walk)
    {
        if (WalkTo(r, f, r.walkX, r.walkY, r.walkZ, false, dt, order))
            EnterPhase(r, Phase::Begin);
        return;
    }
    if (!f.approachKnown)
    {
        Pause(r, "no walkable path toward the " + what + " at " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z));
        return;
    }
    if (++r.approachHops > kMaxApproachHops)
    {
        Pause(r, std::to_string(kMaxApproachHops) + " approach hops did not bring the party within " +
            Num(kSeeYd) + " yd of the " + what);
        return;
    }
    WalkTo(r, f, f.approachX, f.approachY, f.approachZ, false, dt, order);
}

// pull and boss steps: approach until seen, then pull in order until nothing
// hostile of the step is left. `order` lists the entries in kill order (empty:
// any entry, nearest first).
inline void FightStep(Run& r, const Facts& f, const PlanStep& s, const std::vector<std::uint32_t>& killOrder,
    std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    // A step for the other team (flags 2/4):
    //   - a pack is skipped without a look, unless the party already stands
    //     near it and a unit of it is not friendly;
    //   - a boss is always looked at first, and skipped only when every unit
    //     of its encounter there is friendly to the party. A hostile or
    //     neutral one is fought (Uldaman's Lost Dwarves carry team horde in
    //     the plan, yet were hostile to an Alliance party in a live run).
    if (!ai::StepForTeam(s, f.team))
    {
        bool notFriendly = false;
        for (const UnitFact& u : f.units)
            notFriendly = notFriendly || (u.hostile && !r.passed.count(u.guid));
        bool const skip = s.kind == PlanStepKind::Boss
            ? (f.seen && !f.units.empty() && !notFriendly)
            : (!f.seen || !notFriendly);
        if (skip)
        {
            NextStep(r, s.kind == PlanStepKind::Boss
                ? "boss is for the other team and friendly to this party: skipped"
                : "pack is for the other team: skipped");
            return;
        }
    }
    if (!f.seen)
    {
        ApproachHop(r, f, s, dt, order, s.kind == PlanStepKind::Boss ? "boss" : "pack");
        return;
    }
    const UnitFact* pick = nullptr;
    if (killOrder.empty())
        pick = PickPull(r, f.units, 0);
    for (std::uint32_t entry : killOrder)
        if (!pick)
            pick = PickPull(r, f.units, entry);
    if (pick)
    {
        // Pulls that kill nothing, three in a row: the rest of the pack is
        // out of reach (a pit, a ledge). It is left, not counted cleared.
        std::size_t const open = OpenUnits(r, f.units);
        if (r.phase != Phase::Gather)
        {
            if (r.pulls > 0 && open >= r.openAtLastPull)
                ++r.fruitlessPulls;
            else if (r.pulls > 0)
                r.fruitlessPulls = 0;
            r.openAtLastPull = open;
        }
        if (r.fruitlessPulls >= kFruitlessPulls)
        {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid))
                    r.unreachable.insert(u.guid);
            NextStep(r, std::string(s.kind == PlanStepKind::Boss ? "boss" : "pack") + " skipped, not cleared: " +
                std::to_string(open) + " units out of reach after " + std::to_string(kFruitlessPulls) +
                " pulls that killed nothing");
            return;
        }
        PullUnit(r, f, *pick, order);
        return;
    }
    if (OpenUnits(r, f.units) > 0)
    {
        // Only evading or stealthed units left: let them settle (the step's
        // idle ceiling bounds this), and walk through a stealthed pack.
        r.note = "waiting for the pack to settle";
        bool const stealthOnly = [&] {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid) && !u.stealthed)
                    return false;
            return true;
        }();
        if (stealthOnly && WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            NextStep(r, "walked through a stealthed pack");
        return;
    }
    if ((s.flags & ai::kPlanFlagAmbush) && !r.walkedThrough)
    {
        // An ambush pack shows itself only when the party walks into it.
        r.note = "walking through an ambush pack";
        if (WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            r.walkedThrough = true;
        if (!r.walkedThrough)
            return;
    }
    std::size_t outOfReach = 0;
    bool friendlyLeft = false;
    for (const UnitFact& u : f.units)
    {
        if (u.hostile && r.unreachable.count(u.guid))
            ++outOfReach;
        friendlyLeft = friendlyLeft || !u.hostile;
    }
    std::string const what = s.kind == PlanStepKind::Boss ? "boss" : "pack";
    if (outOfReach)
        NextStep(r, what + " skipped, not cleared: " + std::to_string(outOfReach) + " units out of reach");
    else if (friendlyLeft)
        NextStep(r, what + " is friendly to this party: left alone");
    else
        NextStep(r, s.kind == PlanStepKind::Boss ? "boss down" : "pack cleared");
}

inline void MoveStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    if (const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0))
    {
        PullUnit(r, f, *hopUnit, order);
        return;
    }
    // Far away (after a wipe the lead bot comes back from the entrance; a
    // long plan hop): walk back on the navmesh in hops first.
    if (!r.detour && !f.seen)
    {
        ApproachHop(r, f, s, dt, order, "next point");
        return;
    }
    // A direct step takes the lead bot's line: the party gathers on it first
    // (`cmd move ... direct` refuses while a bot has no way to the lead's spot).
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && f.maxMemberYd > kGatherYd)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "gathering before a direct step";
            order = MoveOrder(f.leadX, f.leadY, f.leadZ, false);
        }
        else if (r.phaseMs >= kGatherCapMs)
            Pause(r, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                " s before a direct step (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)");
        return;
    }
    if (r.detour)
    {
        r.note = "walking around: the plan's point is off the walkable way";
        if (WalkTo(r, f, r.detourX, r.detourY, r.detourZ, false, dt, order))
            NextStep(r, "arrived at the nearest reachable point, " +
                Num(ai::PlanDistance(r.detourX, r.detourY, r.detourZ, s.x, s.y, s.z)) + " yd from the plan's");
        return;
    }
    r.note = "walking";
    if (WalkTo(r, f, s.x, s.y, s.z, s.kind == PlanStepKind::MoveDirect, dt, order))
        NextStep(r, "arrived");
}

// The glue found that the current move step's point cannot be reached (its
// `cmd move` was refused unreachable or off-mesh) and that (x,y,z), within
// kDetourMaxYd of it, can: the step walks there instead, by the navmesh path
// around whatever is in the way. False (nothing changes) when the step is not
// a move, a detour is already on, or the point is too far from the plan's.
inline bool UseDetour(Run& r, float x, float y, float z)
{
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running || r.detour ||
        (s->kind != PlanStepKind::Move && s->kind != PlanStepKind::MoveDirect))
        return false;
    if (ai::PlanDistance(x, y, z, s->x, s->y, s->z) > kDetourMaxYd)
        return false;
    r.detour = true;
    r.detourX = x;
    r.detourY = y;
    r.detourZ = z;
    EnterPhase(r, Phase::Begin);
    return true;
}

// loot, open, use_item: one commander task for the carrier, then its result.
inline void TaskStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const kind = ai::PlanStepKindName(s.kind);
    if (!r.orderSent)
    {
        if ((s.kind == PlanStepKind::Loot || s.kind == PlanStepKind::UseItem) && s.item && f.carrierHasItem)
        {
            NextStep(r, kind + ": " + f.carrier + " already carries item " + std::to_string(s.item));
            return;
        }
        if (s.kind == PlanStepKind::Open && s.item && !f.carrierHasItem)
        {
            if (r.missing.count(s.item))
            {
                std::size_t const here = r.step;
                NextStep(r, "open " + std::to_string(s.entry) + " left out: it needs item " + std::to_string(s.item) +
                    ", which the run did not get");
                r.skippedOpenStep = here;
                return;
            }
            Pause(r, "open: " + f.carrier + " does not carry item " + std::to_string(s.item) + " (the lock needs it)");
            return;
        }
        if (s.kind == PlanStepKind::UseItem && !f.carrierHasEntryItem)
        {
            if (r.missing.count(s.entry))
            {
                if (s.item)
                    r.missing.insert(s.item);
                NextStep(r, "use_item " + std::to_string(s.entry) + " left out: the run did not get that item");
                return;
            }
            r.pendingMissing = s.item;
            Pause(r, "use_item: " + f.carrier + " does not carry item " + std::to_string(s.entry));
            return;
        }
        if (f.carrierBusy)
        {
            r.note = kind + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        r.note = kind + " by " + f.carrier;
        if (s.kind == PlanStepKind::Loot)
            order = "loot " + EntryArg(s.entry, s.x, s.y, s.z) + " " + f.carrier + (s.item ? " " + std::to_string(s.item) : "");
        else if (s.kind == PlanStepKind::Open)
            order = "open " + EntryArg(s.entry, s.x, s.y, s.z) + " " + f.carrier;
        else
            order = "use " + std::to_string(s.entry) + " " + f.carrier;
        return;
    }
    bool const consequence = s.kind == PlanStepKind::UseItem && s.item && !r.missing.empty();
    if (f.taskReply == 2)
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item failed (" + f.taskText + "): an earlier item is missing; item " +
                std::to_string(s.item) + " is noted missing too");
            return;
        }
        if (s.kind != PlanStepKind::Open && s.item)
            r.pendingMissing = s.item;
        Pause(r, kind + " failed: " + f.taskText +
            (r.pendingMissing ? " (item " + std::to_string(r.pendingMissing) +
                " missing: .bot run skip leaves out the steps that need it)" : ""));
        return;
    }
    if (f.carrierBusy)
    {
        if (r.phaseMs >= kTaskCapMs)
            Pause(r, kind + ": " + f.carrier + " did not finish in " + std::to_string(kTaskCapMs / 1000) + " s");
        return;
    }
    if (s.kind != PlanStepKind::Open && s.item && !f.carrierHasItem)
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item did not make item " + std::to_string(s.item) +
                " (an earlier item is missing): it is noted missing too");
            return;
        }
        r.pendingMissing = s.item;
        Pause(r, kind + " done, but item " + std::to_string(s.item) + " is not in " + f.carrier +
            "'s bags (.bot run skip leaves out the steps that need it)");
        return;
    }
    NextStep(r, kind + " done");
}

// The out-of-combat ceiling of a step: kStepIdleCapMs, or a wait's own
// timeout plus a minute to get there when that is longer.
inline std::uint32_t StepIdleCapMs(const PlanStep& s)
{
    bool const wait = s.kind == PlanStepKind::WaitObject || s.kind == PlanStepKind::WaitClear;
    std::uint32_t const own = wait ? s.timeoutS * 1000 + 60000 : 0;
    return own > kStepIdleCapMs ? own : kStepIdleCapMs;
}

// One second of the run. Returns the order to send now (the text after
// `.bot cmd `), or "" for none.
inline std::string Tick(Run& r, const Facts& f, std::uint32_t dt = kTickMs, const RestLines& lines = RestLines())
{
    std::string order;
    if (r.state != State::Running)
        return order;
    const PlanStep* s = Current(r);
    if (!s)
    {
        r.state = State::Done;
        r.note = "plan done";
        return order;
    }
    r.phaseMs += dt;

    // After a resume or a skip from a pause: the dead get up, the living
    // gather on the lead bot, then the step goes on.
    if (r.regroup)
    {
        r.regroupMs += dt;
        if (f.dead > 0 || !f.leadAlive)
        {
            r.note = "regroup: " + std::to_string(f.dead) + " dead, reviving";
            if (!r.reviveSent || r.regroupMs % 60000 < dt)
            {
                r.reviveSent = true;
                order = "revive party";
            }
            if (r.regroupMs > kRegroupReviveMs)
                Pause(r, "regroup: " + std::to_string(f.dead) + " still dead after " +
                    std::to_string(kRegroupReviveMs / 1000) + " s (cmd revive <bot>, then .bot run resume)");
            return order;
        }
        if (f.maxMemberYd > kGatherYd && !f.anyInCombat)
        {
            r.note = "regroup: gathering on the lead bot";
            if (!r.regroupGatherSent)
            {
                r.regroupGatherSent = true;
                r.regroupMs = 0;
                order = MoveOrder(f.leadX, f.leadY, f.leadZ, false);
            }
            else if (r.regroupMs > kRegroupGatherMs)
                Pause(r, "regroup: the party did not gather within " + std::to_string(kRegroupGatherMs / 1000) +
                    " s (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)");
            return order;
        }
        r.regroup = false;
        r.note = "regrouped";
    }

    // Wipe: stop and regroup.
    if (f.dead >= kWipeDead || (f.total > 0 && f.alive == 0))
    {
        Pause(r, std::to_string(f.dead) + " dead: regroup (cmd revive), then .bot run resume");
        return order;
    }
    if (!f.leadAlive)
    {
        Pause(r, "the lead bot is dead: revive it, then .bot run resume");
        return order;
    }

    // Fights are the bots' own; the run only bounds them.
    if (f.anyInCombat)
    {
        if (!r.fighting)
        {
            r.fighting = true;
            r.fightMs = 0;
        }
        r.fightMs += dt;
        std::uint32_t const cap = s->kind == PlanStepKind::Boss ? kBossFightCapMs : kFightCapMs;
        if (r.fightMs > cap)
            Pause(r, "a fight lasted more than " + std::to_string(cap / 1000) + " s");
        else
            r.note = "fighting";
        return order;
    }
    if (r.fighting)
    {
        // Back to the step's start: it looks at what is left, and rests first.
        r.fighting = false;
        r.restDue = true;
        r.pullGuid = 0;
        if (r.phase != Phase::AwaitTask && r.phase != Phase::Wait)
            EnterPhase(r, Phase::Begin);
    }

    r.stepIdleMs += dt;
    if (r.stepIdleMs > StepIdleCapMs(*s))
    {
        Pause(r, "step " + std::to_string(r.step + 1) + " (" + ai::PlanStepKindName(s->kind) + ") took more than " +
            std::to_string(StepIdleCapMs(*s) / 1000) + " s out of combat");
        return order;
    }

    // Rest gate.
    if (r.phase == Phase::Rest)
    {
        if (Rested(f, lines) || r.phaseMs >= kRestCapMs)
            EnterPhase(r, Phase::Begin);
        else
        {
            r.restRepeatMs += dt;
            if (r.restRepeatMs >= kRestRepeatMs)
            {
                r.restRepeatMs = 0;
                order = "rest";
            }
            return order;
        }
    }
    if (r.phase == Phase::Begin && (r.restDue || (s->kind == PlanStepKind::Rest && !r.stepRested)))
    {
        bool const forced = s->kind == PlanStepKind::Rest && !r.stepRested;
        r.restDue = false;
        r.stepRested = r.stepRested || s->kind == PlanStepKind::Rest;
        if (forced || NeedsRest(f, lines))
        {
            EnterPhase(r, Phase::Rest);
            r.restRepeatMs = 0;
            r.note = "resting";
            order = "rest";
            return order;
        }
    }

    switch (s->kind)
    {
        case PlanStepKind::Move:
        case PlanStepKind::MoveDirect:
            MoveStep(r, f, *s, dt, order);
            break;
        case PlanStepKind::Pull:
            FightStep(r, f, *s, {}, dt, order);
            break;
        case PlanStepKind::Boss:
        {
            std::vector<std::uint32_t> killOrder = s->entries;
            killOrder.push_back(s->entry);
            FightStep(r, f, *s, killOrder, dt, order);
            break;
        }
        case PlanStepKind::Loot:
        case PlanStepKind::Open:
        case PlanStepKind::UseItem:
            TaskStep(r, f, *s, order);
            break;
        case PlanStepKind::WaitObject:
            if (r.skippedOpenStep != static_cast<std::size_t>(-1) && r.step == r.skippedOpenStep + 1)
            {
                NextStep(r, "wait for object " + std::to_string(s->entry) + " left out: the open before it was left out");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            if (f.objectState == static_cast<int>(s->param))
                NextStep(r, "object " + std::to_string(s->entry) + " is in the state the plan waits for");
            else if (r.phaseMs >= s->timeoutS * 1000)
                Pause(r, "waited " + std::to_string(s->timeoutS) + " s for object " + std::to_string(s->entry) +
                    (f.objectState < 0 ? " (not found near its point)" : " (state " + std::to_string(f.objectState) + ")"));
            else
                r.note = "waiting for object " + std::to_string(s->entry);
            break;
        case PlanStepKind::WaitClear:
            if (!f.seen)
            {
                ApproachHop(r, f, *s, dt, order, "spot to watch");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            if (f.hostilesNear == 0)
                NextStep(r, "no hostile left near the spot");
            else if (r.phaseMs >= s->timeoutS * 1000)
                Pause(r, "waited " + std::to_string(s->timeoutS) + " s; " + std::to_string(f.hostilesNear) +
                    " hostiles still near the spot");
            else
                r.note = "waiting for the spot to clear";
            break;
        case PlanStepKind::Rest:
            NextStep(r, "rested");
            break;
        default:
            Pause(r, std::string("step kind ") + ai::PlanStepKindName(s->kind) +
                " is not supported yet: do it by hand, then .bot run skip and .bot run resume");
            break;
    }
    return order;
}

// The status line for the commander:
//   TBM:RUN|<plan>|<state>|<step>/<steps>|<leg>|<kind>|<phase>|<note>
// <step> counts from 1 (0 before the first, steps+1 is never sent: a done run
// shows the last). '|' and line breaks in the note become spaces.
inline std::string StatusLine(const Run& r)
{
    std::size_t const total = r.plan ? r.plan->steps.size() : 0;
    std::size_t const shown = total == 0 ? 0 : (r.step < total ? r.step + 1 : total);
    const PlanStep* s = r.plan && shown ? &r.plan->steps[shown - 1] : nullptr;
    std::string note = r.note;
    for (char& c : note)
        if (c == '|' || c == '\n' || c == '\r')
            c = ' ';
    std::string leg = s ? r.plan->LegName(s->leg) : std::string();
    if (leg.empty() && s)
        leg = std::to_string(s->leg);
    return "TBM:RUN|" + (r.plan ? r.plan->name : std::string("-")) + "|" + StateName(r.state) + "|" +
        std::to_string(shown) + "/" + std::to_string(total) + "|" + (leg.empty() ? "-" : leg) + "|" +
        (s ? ai::PlanStepKindName(s->kind) : "-") + "|" + PhaseName(r.phase) + "|" + (note.empty() ? "-" : note);
}

// Which steps the run leader does today; the rest pause the run.
inline bool Supported(PlanStepKind kind)
{
    return kind != PlanStepKind::None && kind != PlanStepKind::Ritual;
}

} // namespace DungeonRun
} // namespace TortoiseBots
