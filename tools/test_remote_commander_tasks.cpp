// Standalone tests for the remote commander's per-bot player orders (issue
// #884): the walk and act state machine of `cmd loot`, `cmd open` and `cmd use`,
// the standing point, the lock verdict, the loot ledger the loot window reports
// into, the cast-failure mapping and the loot-method names.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_remote_commander_tasks.cpp -o /tmp/test_remote_commander_tasks
//   /tmp/test_remote_commander_tasks

#include "../runtime/RemoteCommanderTasks.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace TortoiseBots::RemoteCommander;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static TaskFacts Walking(float distance)
{
    TaskFacts facts;
    facts.distance = distance;
    return facts;
}

// The bot stands 1.5 yd short of the object, on the bot's side; a bot already
// closer stays where it is.
static void TestStandingPoint()
{
    Point3 p = StandingPoint(100.0f, 0.0f, 0.0f, 0.0f, 7.0f);
    CHECK(Near(p.x, 1.5f) && Near(p.y, 0.0f) && Near(p.z, 7.0f));
    p = StandingPoint(0.0f, -30.0f, 0.0f, 0.0f, 2.0f);
    CHECK(Near(p.x, 0.0f) && Near(p.y, -1.5f));
    p = StandingPoint(3.0f, 4.0f, 0.0f, 0.0f, 0.0f); // 5 yd away, diagonal
    CHECK(Near(std::sqrt(p.x * p.x + p.y * p.y), 1.5f));
    CHECK(p.x > 0.0f && p.y > 0.0f);
    // Closer than the back-off, or on top of the object: where it stands now.
    p = StandingPoint(1.0f, 0.5f, 0.0f, 0.0f, 3.0f);
    CHECK(Near(p.x, 1.0f) && Near(p.y, 0.5f) && Near(p.z, 3.0f));
    p = StandingPoint(0.0f, 0.0f, 0.0f, 0.0f, 3.0f);
    CHECK(Near(p.x, 0.0f) && Near(p.y, 0.0f));
}

static void TestWalkBudget()
{
    CHECK(WalkBudgetMs(0.0f) == kMinWalkMs);
    CHECK(WalkBudgetMs(-5.0f) == kMinWalkMs);
    CHECK(WalkBudgetMs(40.0f) == kMinWalkMs + 10000);   // 40 yd at 4 yd/s
    CHECK(WalkBudgetMs(10000.0f) == kMaxWalkMs);         // capped
    CHECK(WalkBudgetMs(100.0f) < WalkBudgetMs(150.0f));
}

static void TestRangeChecks()
{
    CHECK(WithinReactDistance(40.0f, 150.0f));
    CHECK(WithinReactDistance(150.0f, 150.0f));
    CHECK(!WithinReactDistance(150.1f, 150.0f));
    CHECK(!WithinReactDistance(1.0f, 0.0f));     // react distance not configured: refused
    CHECK(InUseRange(4.4f, 5.0f));
    CHECK(InUseRange(4.5f, 5.0f));
    CHECK(!InUseRange(4.6f, 5.0f));
}

// Walk, arrive, act, succeed.
static void TestWalkThenAct()
{
    TaskState state = MakeTask(TaskKind::Loot, 30.0f, true);
    CHECK(state.stage == TaskStage::Walk);
    CHECK(state.walkBudgetMs == WalkBudgetMs(30.0f));
    CHECK(state.actBudgetMs == kLootWindowMs);

    TaskVerdict verdict = AdvanceTask(state, Walking(30.0f), kTaskTickMs);
    CHECK(verdict.step == TaskStep::Wait);
    verdict = AdvanceTask(state, Walking(12.0f), kTaskTickMs);
    CHECK(verdict.step == TaskStep::Wait);

    TaskFacts arrived = Walking(2.0f);
    arrived.inRange = true;
    verdict = AdvanceTask(state, arrived, kTaskTickMs);
    CHECK(verdict.step == TaskStep::BeginAct);
    CHECK(state.stage == TaskStage::Act);

    // The window has not closed yet.
    verdict = AdvanceTask(state, arrived, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Wait);
    arrived.actDone = true;
    verdict = AdvanceTask(state, arrived, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Succeed);
}

// Already in range at the first look: the action starts at once.
static void TestAlreadyInRange()
{
    TaskState state = MakeTask(TaskKind::Open, 2.0f, true);
    TaskFacts facts = Walking(2.0f);
    facts.inRange = true;
    CHECK(AdvanceTask(state, facts, 0).step == TaskStep::BeginAct);
    // An order that needs no walk starts in the act stage.
    TaskState use = MakeTask(TaskKind::Use, 0.0f, false);
    CHECK(use.stage == TaskStage::Act);
    CHECK(use.actBudgetMs == kUseCastMs);
    CHECK(MakeTask(TaskKind::Open, 0.0f, true).actBudgetMs == kOpenEffectMs);
}

// A bot that does not close in is stuck; one that walks on is not.
static void TestStall()
{
    TaskState state = MakeTask(TaskKind::Loot, 60.0f, true);
    float distance = 60.0f;
    for (uint32_t t = 0; t < 20000; t += kTaskTickMs)
    {
        distance -= 1.2f; // keeps closing in by more than kProgressYards every look
        CHECK(AdvanceTask(state, Walking(distance), kTaskTickMs).step == TaskStep::Wait);
    }
    CHECK(state.stalledMs == 0);

    // Now it stops 6 yd short of the object.
    TaskState stuck = MakeTask(TaskKind::Loot, 60.0f, true);
    TaskVerdict verdict;
    uint32_t looks = 0;
    do
    {
        verdict = AdvanceTask(stuck, Walking(6.0f), kTaskTickMs);
        ++looks;
        CHECK(looks < 200);
    } while (verdict.step == TaskStep::Wait);
    CHECK(verdict.step == TaskStep::Fail);
    CHECK(std::strcmp(verdict.code, kCodeUnreachable) == 0);
    CHECK(stuck.elapsedMs >= kStallMs && stuck.elapsedMs <= kStallMs + kTaskTickMs);
}

// The walk budget ends a walk that keeps creeping.
static void TestWalkTimeout()
{
    TaskState state = MakeTask(TaskKind::Loot, 20.0f, true);
    float distance = 20.0f;
    TaskVerdict verdict;
    uint32_t looks = 0;
    do
    {
        distance -= 0.002f; // never enough to count as progress... and never stalls for good
        // a new best every 5 s keeps the stall counter below its limit
        if (looks % 20 == 0)
            distance -= 2.0f;
        verdict = AdvanceTask(state, Walking(distance), kTaskTickMs);
        ++looks;
        CHECK(looks < 1000);
    } while (verdict.step == TaskStep::Wait);
    CHECK(verdict.step == TaskStep::Fail);
    CHECK(std::strcmp(verdict.code, kCodeTimeout) == 0);
    CHECK(state.elapsedMs >= state.walkBudgetMs);
}

static void TestWalkRefusals()
{
    TaskState state = MakeTask(TaskKind::Open, 30.0f, true);
    TaskFacts facts = Walking(30.0f);
    facts.botAvailable = false;
    TaskVerdict verdict = AdvanceTask(state, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeUnavailable) == 0);

    state = MakeTask(TaskKind::Open, 30.0f, true);
    facts = Walking(30.0f);
    facts.inCombat = true;
    verdict = AdvanceTask(state, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeInCombat) == 0);

    state = MakeTask(TaskKind::Open, 30.0f, true);
    facts = Walking(30.0f);
    facts.targetPresent = false;
    verdict = AdvanceTask(state, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeInvalidTarget) == 0);

    // In range wins over a fight that began on the last step? No: combat stops the walk.
    state = MakeTask(TaskKind::Open, 30.0f, true);
    facts = Walking(2.0f);
    facts.inRange = true;
    facts.inCombat = true;
    CHECK(AdvanceTask(state, facts, kTaskTickMs).step == TaskStep::Fail);
}

// The act stage: a fight or the object vanishing does not matter (the window
// is open), a dead or teleporting bot does, and the budget ends the wait.
static void TestActStage()
{
    TaskState state = MakeTask(TaskKind::Loot, 2.0f, true);
    TaskFacts facts = Walking(2.0f);
    facts.inRange = true;
    CHECK(AdvanceTask(state, facts, 0).step == TaskStep::BeginAct);
    facts.targetPresent = false;
    CHECK(AdvanceTask(state, facts, kTaskTickMs).step == TaskStep::Wait);
    facts.inCombat = true;

    TaskFacts gone = facts;
    gone.botAvailable = false;
    TaskVerdict verdict = AdvanceTask(state, gone, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeUnavailable) == 0);

    state = MakeTask(TaskKind::Loot, 2.0f, true);
    facts = Walking(2.0f);
    facts.inRange = true;
    AdvanceTask(state, facts, 0);
    verdict = AdvanceTask(state, facts, kLootWindowMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeTimeout) == 0);

    // A result that arrives on the last look still counts.
    state = MakeTask(TaskKind::Use, 0.0f, false);
    facts = Walking(0.0f);
    facts.actDone = true;
    CHECK(AdvanceTask(state, facts, kUseCastMs + 1).step == TaskStep::Succeed);
}

// An item that teleports the bot (hearthstone) ends with the bot between maps:
// that is its result, but only for a use whose cast has started.
static void TestTeleportEndsUse()
{
    TaskState use = MakeTask(TaskKind::Use, 0.0f, true);   // nothing to walk to: in range at once
    TaskFacts facts;
    facts.inRange = true;
    CHECK(AdvanceTask(use, facts, 0).step == TaskStep::BeginAct);
    facts.botAvailable = false;
    facts.teleporting = true;
    facts.actStarted = true;
    CHECK(AdvanceTask(use, facts, kTaskTickMs).step == TaskStep::Succeed);

    // Not started yet (a retry state), or dead rather than teleporting: unavailable.
    TaskVerdict verdict;
    facts.actStarted = false;
    verdict = AdvanceTask(use, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeUnavailable) == 0);
    facts.actStarted = true;
    facts.teleporting = false;
    verdict = AdvanceTask(use, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeUnavailable) == 0);

    // Loot and open are never saved by a teleport.
    TaskState loot = MakeTask(TaskKind::Loot, 0.0f, true);
    TaskFacts walkOk;
    walkOk.inRange = true;
    AdvanceTask(loot, walkOk, 0);
    facts.teleporting = true;
    verdict = AdvanceTask(loot, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail);
    // A use still in its walk stage is not over either.
    TaskState walking = MakeTask(TaskKind::Use, 30.0f, true);
    CHECK(AdvanceTask(walking, facts, kTaskTickMs).step == TaskStep::Fail);
}

// A used object shows its reaction by changing state; a vanished one is no result.
static void TestObjectEffect()
{
    CHECK(JudgeObjectEffect(true, 0x100, 0x100) == ObjectEffect::Waiting);
    CHECK(JudgeObjectEffect(true, 0x100, 0x101) == ObjectEffect::Reacted);
    CHECK(JudgeObjectEffect(false, 0x100, 0) == ObjectEffect::Gone);
    CHECK(JudgeObjectEffect(false, 0, 0) == ObjectEffect::Gone);   // gone even if the packed state matches
    CHECK(std::strcmp(kCodeCancelled, "cancelled") == 0);
}

static void TestLockVerdict()
{
    LockFacts none;
    CHECK(CheckLock(none) == LockVerdict::Open);

    // Key lock: the carried key opens it, no key does not.
    LockFacts keyed;
    keyed.keyItem = 5396;
    CHECK(CheckLock(keyed) == LockVerdict::NeedsItem);
    keyed.hasKey = true;
    CHECK(CheckLock(keyed) == LockVerdict::Open);

    // Skill lock: enough skill opens it, too little does not.
    LockFacts picked;
    picked.skillId = 633;
    picked.reqSkill = 150;
    picked.skillValue = 149;
    CHECK(CheckLock(picked) == LockVerdict::Locked);
    picked.skillValue = 150;
    CHECK(CheckLock(picked) == LockVerdict::Open);

    // Both rows are alternatives: either one opens it; neither asks for the key.
    LockFacts both;
    both.keyItem = 5396;
    both.skillId = 633;
    both.reqSkill = 150;
    both.skillValue = 200;
    CHECK(CheckLock(both) == LockVerdict::Open);
    both.skillValue = 10;
    CHECK(CheckLock(both) == LockVerdict::NeedsItem);
    both.hasKey = true;
    CHECK(CheckLock(both) == LockVerdict::Open);

    CHECK(std::strcmp(LockVerdictCode(LockVerdict::NeedsItem), "no-item") == 0);
    CHECK(std::strcmp(LockVerdictCode(LockVerdict::Locked), "locked") == 0);
    CHECK(std::strcmp(LockVerdictCode(LockVerdict::Open), "ok") == 0);
    CHECK(std::strlen(LockVerdictText(LockVerdict::Locked)) > 10);
}

static void TestCastReasonCodes()
{
    CHECK(std::strcmp(CastReasonCode("range"), "too-far") == 0);
    CHECK(std::strcmp(CastReasonCode("no-item"), "no-item") == 0);
    CHECK(std::strcmp(CastReasonCode("no-target"), "invalid-target") == 0);
    CHECK(std::strcmp(CastReasonCode("cd"), "busy") == 0);
    CHECK(std::strcmp(CastReasonCode("casting"), "busy") == 0);
    CHECK(std::strcmp(CastReasonCode("moving"), "busy") == 0);
    CHECK(std::strcmp(CastReasonCode("oom"), "failed") == 0);
    CHECK(std::strcmp(CastReasonCode("los"), "failed") == 0);
    CHECK(std::strcmp(CastReasonCode(""), "failed") == 0);
}

// The ledger: one watch per bot, on one object; the window reports into it.
static void TestLootLedger()
{
    LootLedger ledger;
    uint32_t only = 99;
    CHECK(ledger.Empty());
    CHECK(!ledger.Watching(1, 500, only));

    ledger.Watch(1, 500, 0);
    CHECK(ledger.Watching(1, 500, only) && only == 0);
    CHECK(!ledger.Watching(1, 501, only));   // another object
    CHECK(!ledger.Watching(2, 500, only));   // another bot

    // Reports for other objects or bots are ignored.
    ledger.NoteItem(1, 501, 7741, 1);
    ledger.NoteItem(2, 500, 7741, 1);
    CHECK(ledger.Find(1)->items.empty());

    ledger.NoteItem(1, 500, 7741, 1);
    ledger.NoteItem(1, 500, 7740, 1);
    ledger.NoteItem(1, 500, 7741, 2);        // same item again: merged
    ledger.NoteItem(1, 500, 0, 5);           // not an item
    ledger.NoteItem(1, 500, 4540, 0);        // nothing taken
    ledger.NoteCopper(1, 500, 120);
    ledger.NoteCopper(1, 500, 30);
    CHECK(ledger.Find(1)->items.size() == 2);
    CHECK(LootedItemCount(*ledger.Find(1)) == 4);
    CHECK(FormatLootedItems(*ledger.Find(1)) == "7741:3,7740:1,copper:150");
    CHECK(!ledger.Find(1)->finished);

    ledger.NoteWindowEnd(1, 501, false);     // wrong object: still open
    CHECK(!ledger.Find(1)->finished);
    ledger.NoteWindowEnd(1, 500, false);
    CHECK(ledger.Find(1)->finished && !ledger.Find(1)->refused);
    // A closed window takes no more reports and is no longer "watching".
    ledger.NoteItem(1, 500, 4540, 1);
    CHECK(LootedItemCount(*ledger.Find(1)) == 4);
    CHECK(!ledger.Watching(1, 500, only));

    // A refused window.
    ledger.Watch(3, 700, 7741);
    CHECK(ledger.Watching(3, 700, only) && only == 7741);
    ledger.NoteWindowEnd(3, 700, true);
    CHECK(ledger.Find(3)->finished && ledger.Find(3)->refused);
    CHECK(FormatLootedItems(*ledger.Find(3)) == "-");

    // Money alone.
    ledger.Watch(4, 800, 0);
    ledger.NoteCopper(4, 800, 77);
    CHECK(FormatLootedItems(*ledger.Find(4)) == "copper:77");

    ledger.Forget(1);
    CHECK(!ledger.Find(1));
    ledger.Clear();
    CHECK(ledger.Empty());

    // The shared ledger starts empty.
    CHECK(LootOrders().Empty());
}

static void TestLootItemWanted()
{
    CHECK(LootItemWanted(0, 7741));
    CHECK(LootItemWanted(7741, 7741));
    CHECK(!LootItemWanted(7741, 7740));
}

static void TestLootMethod()
{
    int method = -1;
    CHECK(ParseLootMethod("ffa", method) && method == kLootFfa);
    CHECK(ParseLootMethod("FFA", method) && method == kLootFfa);
    CHECK(ParseLootMethod("group", method) && method == kLootGroup);
    CHECK(ParseLootMethod("rr", method) && method == kLootRoundRobin);
    CHECK(ParseLootMethod("roundrobin", method) && method == kLootRoundRobin);
    CHECK(ParseLootMethod("master", method) && method == kLootMaster);
    CHECK(ParseLootMethod("nbg", method) && method == kLootNeedBeforeGreed);
    CHECK(ParseLootMethod("NeedBeforeGreed", method) && method == kLootNeedBeforeGreed);
    method = -1;
    CHECK(!ParseLootMethod("", method) && method == -1);
    CHECK(!ParseLootMethod("everyone", method));
    CHECK(!ParseLootMethod("2", method));

    // The names read back: every method has its own.
    CHECK(std::strcmp(LootMethodName(kLootFfa), "ffa") == 0);
    CHECK(std::strcmp(LootMethodName(kLootRoundRobin), "rr") == 0);
    CHECK(std::strcmp(LootMethodName(kLootMaster), "master") == 0);
    CHECK(std::strcmp(LootMethodName(kLootGroup), "group") == 0);
    CHECK(std::strcmp(LootMethodName(kLootNeedBeforeGreed), "nbg") == 0);
    CHECK(std::strcmp(LootMethodName(9), "unknown") == 0);
    for (int m = 0; m <= 4; ++m)
    {
        int parsed = -1;
        CHECK(ParseLootMethod(LootMethodName(m), parsed) && parsed == m);
    }

    int threshold = 0;
    CHECK(ParseLootThreshold("1", threshold) && threshold == 1);
    CHECK(ParseLootThreshold("2", threshold) && threshold == 2);
    CHECK(ParseLootThreshold("5", threshold) && threshold == 5);
    CHECK(ParseLootThreshold("uncommon", threshold) && threshold == 2);
    CHECK(ParseLootThreshold("Green", threshold) && threshold == 2);
    CHECK(ParseLootThreshold("rare", threshold) && threshold == 3);
    CHECK(ParseLootThreshold("epic", threshold) && threshold == 4);
    CHECK(ParseLootThreshold("legendary", threshold) && threshold == 5);
    CHECK(!ParseLootThreshold("0", threshold));      // poor: the core refuses it
    CHECK(!ParseLootThreshold("6", threshold));
    CHECK(!ParseLootThreshold("10", threshold));
    CHECK(!ParseLootThreshold("", threshold));
    CHECK(!ParseLootThreshold("gold", threshold));
}

// A use with nothing to walk to (on the bot itself or a unit) is allowed in
// combat: a fight does not stop it, it goes on to the action. A use that walks
// to a game object still stops when a fight starts.
static void TestInPlaceUseInCombat()
{
    TaskState state = MakeInPlaceTask(TaskKind::Use);
    CHECK(!state.walks);
    TaskFacts facts;
    facts.inCombat = true;
    facts.inRange = true;
    TaskVerdict verdict = AdvanceTask(state, facts, 0);
    CHECK(verdict.step == TaskStep::BeginAct);
    CHECK(state.stage == TaskStage::Act);
    facts.actStarted = true;
    verdict = AdvanceTask(state, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Wait);
    facts.actDone = true;
    verdict = AdvanceTask(state, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Succeed);

    TaskState walking = MakeTask(TaskKind::Use, 20.0f, true);
    CHECK(walking.walks);
    TaskFacts fight = Walking(20.0f);
    fight.inCombat = true;
    verdict = AdvanceTask(walking, fight, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeInCombat) == 0);
}

// A later order cancels a running task. Only an order that sets the bot's
// movement itself leaves the cancelled walk's stance alone; after an attack,
// a focus or a pull the bot re-forms as a finished walk would.
static void TestLaterOrderMovement()
{
    CHECK(LaterOrderTakesMovement(LaterOrder::Move));
    CHECK(LaterOrderTakesMovement(LaterOrder::Stay));
    CHECK(LaterOrderTakesMovement(LaterOrder::Follow));
    CHECK(LaterOrderTakesMovement(LaterOrder::Summon));
    CHECK(!LaterOrderTakesMovement(LaterOrder::Attack));
    CHECK(!LaterOrderTakesMovement(LaterOrder::Focus));
    CHECK(!LaterOrderTakesMovement(LaterOrder::Pull));
}

// `cmd open` on a chest waits for a loot window, not for an object's reaction:
// the 5 s Opening cast of a locked chest (Baelog's Chest, lock 93) does not fit
// in 4 s. Once a cast is under way the act stage covers the rest of the cast
// plus the wait for its result, never less than before and never unbounded.
static void TestActBudgetCoversTheCast()
{
    CHECK(ActBudgetMs(TaskKind::Open, false) == kOpenEffectMs);
    CHECK(ActBudgetMs(TaskKind::Open, true) == kLootWindowMs);
    CHECK(ActBudgetMs(TaskKind::Loot, true) == kLootWindowMs);
    CHECK(ActBudgetMs(TaskKind::Use, false) == kUseCastMs);
    CHECK(MakeTask(TaskKind::Open, 5.0f, true, true).actBudgetMs == kLootWindowMs);
    CHECK(MakeTask(TaskKind::Open, 5.0f, true).actBudgetMs == kOpenEffectMs);

    TaskState chest = MakeTask(TaskKind::Open, 3.0f, false, true);
    chest.stageMs = 500; // one refused attempt before the cast started
    CHECK(ActBudgetAfterCast(chest, true, 5000) == 500 + 5000 + kLootWindowMs);

    TaskState lever = MakeTask(TaskKind::Open, 3.0f, false);
    CHECK(ActBudgetAfterCast(lever, false, 0) == kOpenEffectMs);       // instant: unchanged
    CHECK(ActBudgetAfterCast(lever, false, 3000) == 3000 + kOpenEffectMs);

    TaskState use = MakeInPlaceTask(TaskKind::Use);
    CHECK(ActBudgetAfterCast(use, false, 1000) == 1000 + kUseCastMs);
    CHECK(ActBudgetAfterCast(use, false, 600000) == kMaxWalkMs);       // capped

    // The extended budget holds the stage open through the cast.
    TaskState open = MakeTask(TaskKind::Open, 3.0f, false, true);
    open.actBudgetMs = ActBudgetAfterCast(open, true, 5000);
    TaskFacts started;
    started.inRange = true;
    started.actStarted = true;
    CHECK(AdvanceTask(open, started, 9000).step == TaskStep::Wait);
    CHECK(AdvanceTask(open, started, 4000).step == TaskStep::Fail);
}

// A refused open says why: the bot's own gates (moving, another cast), the
// range, or the core's cast check by number.
static void TestOpenRefusal()
{
    CHECK(std::strcmp(CastReasonCode("moving"), "busy") == 0);
    CHECK(OpenRefusalText("moving", 0).find("moving") != std::string::npos);
    CHECK(OpenRefusalText("casting", 0).find("casting") != std::string::npos);
    CHECK(OpenRefusalText("range", 0).find("close enough") != std::string::npos);
    CHECK(OpenRefusalText("los", 0).find("line of sight") != std::string::npos);
    CHECK(OpenRefusalText("", 12) == "The opening cast failed (spell check 12).");
    CHECK(OpenRefusalText("", 0) == "The bot could not start opening it.");
    CHECK(OpenRefusalText("moving", 33).find("moving") != std::string::npos); // the reason wins
}

// The hold: orders on a corpse or an object hold the bot still while the action
// starts and runs; a use with nothing to walk to keeps the bot's stance.
static void TestActHold()
{
    CHECK(HoldsForAct(true, false));
    CHECK(!HoldsForAct(true, true));
    CHECK(!HoldsForAct(false, true));
    CHECK(!HoldsForAct(false, false));
    CHECK(kActTries >= 2);
}

// A fight during the act of an order that walked ends it with in-combat at the
// next look (the hold is released), and a refused start is not retried in it;
// a result already in still counts, and a use in place keeps going.
static void TestCombatEndsAct()
{
    TaskState chest = MakeTask(TaskKind::Open, 3.0f, true, true);
    TaskFacts facts = Walking(3.0f);
    facts.inRange = true;
    CHECK(AdvanceTask(chest, facts, 0).step == TaskStep::BeginAct);
    facts.actStarted = true;
    CHECK(MayRetryAct(chest, facts));
    facts.inCombat = true;
    CHECK(!MayRetryAct(chest, facts));
    TaskVerdict verdict = AdvanceTask(chest, facts, kTaskTickMs);
    CHECK(verdict.step == TaskStep::Fail && std::strcmp(verdict.code, kCodeInCombat) == 0);

    TaskState done = MakeTask(TaskKind::Loot, 3.0f, true);
    TaskFacts walk = Walking(3.0f);
    walk.inRange = true;
    AdvanceTask(done, walk, 0);
    walk.inCombat = true;
    walk.actDone = true;
    CHECK(AdvanceTask(done, walk, kTaskTickMs).step == TaskStep::Succeed);

    TaskState potion = MakeInPlaceTask(TaskKind::Use);
    TaskFacts inPlace;
    inPlace.inRange = true;
    AdvanceTask(potion, inPlace, 0);
    inPlace.inCombat = true;
    CHECK(MayRetryAct(potion, inPlace));
    CHECK(AdvanceTask(potion, inPlace, kTaskTickMs).step == TaskStep::Wait);

    TaskFacts gone;
    gone.botAvailable = false;
    CHECK(!MayRetryAct(potion, gone));
}

int main()
{
    TestStandingPoint();
    TestWalkBudget();
    TestRangeChecks();
    TestWalkThenAct();
    TestAlreadyInRange();
    TestStall();
    TestWalkTimeout();
    TestWalkRefusals();
    TestActStage();
    TestTeleportEndsUse();
    TestInPlaceUseInCombat();
    TestLaterOrderMovement();
    TestObjectEffect();
    TestLockVerdict();
    TestCastReasonCodes();
    TestActBudgetCoversTheCast();
    TestOpenRefusal();
    TestActHold();
    TestCombatEndsAct();
    TestLootLedger();
    TestLootItemWanted();
    TestLootMethod();
    std::printf("remote commander tasks: %d checks passed\n", checks);
    return 0;
}
