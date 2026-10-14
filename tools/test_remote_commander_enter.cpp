// Standalone tests for `cmd enter` (issue #884): the argument parsing, the choice of
// the nearest instance entrance, the requirement verdict, the per-bot walk / wait /
// arrive state machine and the tally behind the order's single final answer.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_remote_commander_enter.cpp -o /tmp/test_remote_commander_enter
//   /tmp/test_remote_commander_enter

#include "../runtime/RemoteCommanderEnter.h"
#include "../runtime/RemoteCommanderPolicy.h"

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

static std::vector<std::string> Args(std::string const& text) { return SplitOrderArgs(text); }

static void TestParse()
{
    EnterOrder order;
    CHECK(ParseEnterOrder(Args("2"), order) && !order.nearest && order.triggerId == 2 && order.bot.empty());
    CHECK(ParseEnterOrder(Args("1466 Tanky"), order) && order.triggerId == 1466 && order.bot == "Tanky");
    CHECK(ParseEnterOrder(Args("near"), order) && order.nearest && order.triggerId == 0 && order.bot.empty());
    CHECK(ParseEnterOrder(Args("NEAR Healy"), order) && order.nearest && order.bot == "Healy");
    CHECK(ParseEnterOrder(Args("999999"), order) && order.triggerId == 999999);
    // Refused: nothing, zero, a negative or hex id, a word, too long, too many words.
    CHECK(!ParseEnterOrder(Args(""), order));
    CHECK(!ParseEnterOrder(Args("0"), order));
    CHECK(!ParseEnterOrder(Args("-3"), order));
    CHECK(!ParseEnterOrder(Args("0x10"), order));
    CHECK(!ParseEnterOrder(Args("zf"), order));
    CHECK(!ParseEnterOrder(Args("1000000"), order));
    CHECK(!ParseEnterOrder(Args("12 Tanky Healy"), order));
    CHECK(!ParseEnterOrder(Args("entry:12"), order));
    // `enter` is an owner-or-GM order that the commander knows, and fails closed otherwise.
    CHECK(IsKnownOrder("enter"));
    CHECK(TierForOrder("enter") == OrderTier::OwnerOrGm);
    CHECK(!MayIssueOrder(true, true, TierForOrder("enter"), false));
    CHECK(MayIssueOrder(true, true, TierForOrder("enter"), true));
    CHECK(!MayIssueOrder(false, true, TierForOrder("enter"), true));
}

static void TestEntranceAndNearest()
{
    CHECK(IsInstanceEntrance(true, true, 1, 209));
    CHECK(!IsInstanceEntrance(false, true, 1, 209));  // not a teleport trigger
    CHECK(!IsInstanceEntrance(true, false, 209, 1));  // the way out
    CHECK(IsInstanceEntrance(true, true, 230, 229));  // a dungeon inside a dungeon
    CHECK(!IsInstanceEntrance(true, true, 209, 209));

    uint32_t id = 0;
    std::vector<TriggerCandidate> none;
    CHECK(!PickNearestTrigger(none, 150.0f, id));
    std::vector<TriggerCandidate> far = { { 7, 151.0f }, { 8, 400.0f } };
    CHECK(!PickNearestTrigger(far, 150.0f, id));
    std::vector<TriggerCandidate> some = { { 9, 80.0f }, { 4, 30.5f }, { 5, 31.0f } };
    CHECK(PickNearestTrigger(some, 150.0f, id) && id == 4);
    // Exactly on the limit counts; equally near goes to the lowest id.
    std::vector<TriggerCandidate> ties = { { 12, 20.0f }, { 3, 20.0f }, { 8, 20.0f } };
    CHECK(PickNearestTrigger(ties, 150.0f, id) && id == 3);
    std::vector<TriggerCandidate> edge = { { 6, 150.0f } };
    CHECK(PickNearestTrigger(edge, 150.0f, id) && id == 6);
    // Id 0 is never a trigger; a NaN distance is never in reach.
    std::vector<TriggerCandidate> bad = { { 0, 1.0f }, { 2, std::nanf("") } };
    CHECK(!PickNearestTrigger(bad, 150.0f, id));
}

static EntryFacts Ready()
{
    EntryFacts facts;
    facts.level = 47;
    facts.requiredLevel = 44;
    return facts;
}

static void TestRequirements()
{
    EntryFacts facts = Ready();
    CHECK(JudgeEntry(facts).ok);

    facts = Ready();
    facts.level = 43;
    EntryVerdict verdict = JudgeEntry(facts);
    CHECK(!verdict.ok && std::strcmp(verdict.code, kCodeRequirement) == 0);
    CHECK(verdict.message.find("level 44") != std::string::npos && verdict.message.find("43") != std::string::npos);
    // The server's switch and the Lunatic override lift the level gate.
    facts.ignoreLevel = true;
    CHECK(JudgeEntry(facts).ok);
    facts.ignoreLevel = false;
    facts.lunaticOverride = true;
    CHECK(JudgeEntry(facts).ok);

    facts = Ready();
    facts.conditionFailed = true;
    verdict = JudgeEntry(facts);
    CHECK(!verdict.ok && std::strcmp(verdict.code, kCodeRequirement) == 0 && verdict.message.find("key") != std::string::npos);
    facts.conditionMessage = "You need the Scarlet Key.";
    CHECK(JudgeEntry(facts).message == "You need the Scarlet Key.");

    facts = Ready();
    facts.raidMap = true;
    verdict = JudgeEntry(facts);
    CHECK(!verdict.ok && std::strcmp(verdict.code, kCodeRequirement) == 0 && verdict.message.find("raid group") != std::string::npos);
    facts.inRaidGroup = true;
    CHECK(JudgeEntry(facts).ok);
    facts.inRaidGroup = false;
    facts.ignoreRaid = true;
    CHECK(JudgeEntry(facts).ok);

    facts = Ready();
    facts.instanceCountOk = false;
    verdict = JudgeEntry(facts);
    CHECK(!verdict.ok && std::strcmp(verdict.code, kCodeInstanceLimit) == 0);

    facts = Ready();
    facts.phaseOpen = false;
    facts.requiredPhase = 3;
    verdict = JudgeEntry(facts);
    CHECK(!verdict.ok && std::strcmp(verdict.code, kCodeRequirement) == 0 && verdict.message.find("phase 4") != std::string::npos);

    // The core's order: a level gate is named before the instance limit.
    facts = Ready();
    facts.level = 10;
    facts.instanceCountOk = false;
    CHECK(std::strcmp(JudgeEntry(facts).code, kCodeRequirement) == 0);

    // A GameMaster skips level, condition, raid group and the limit, but not the content phase.
    facts = Ready();
    facts.gm = true;
    facts.level = 1;
    facts.conditionFailed = true;
    facts.raidMap = true;
    facts.instanceCountOk = false;
    CHECK(JudgeEntry(facts).ok);
    facts.phaseOpen = false;
    CHECK(!JudgeEntry(facts).ok);
}

static EnterFacts Far(float distance)
{
    EnterFacts facts;
    facts.distance = distance;
    return facts;
}

static void TestBudgets()
{
    EnterState state = MakeEnterState(40.0f, 100.0f, true);
    CHECK(state.walkBudgetMs == WalkBudgetMs(40.0f));
    CHECK(state.leadWaitBudgetMs == WalkBudgetMs(100.0f) + kEnterArriveMs);
    CHECK(EnterBudgetMs(100.0f, false) == WalkBudgetMs(100.0f) + kEnterArriveMs);
    CHECK(EnterBudgetMs(100.0f, true) == WalkBudgetMs(100.0f) + 2 * kEnterArriveMs);
}

// The lead bot: walk, in range, send the trigger, arrive.
static void TestLeadWalksThenArrives()
{
    EnterState state = MakeEnterState(40.0f, 40.0f, false);
    EnterFacts facts = Far(40.0f);
    CHECK(AdvanceEnter(state, facts, 250).step == EnterStep::Wait);
    facts.distance = 20.0f;
    CHECK(AdvanceEnter(state, facts, 250).step == EnterStep::Wait);
    facts.inRange = true;
    facts.distance = 1.0f;
    EnterVerdict verdict = AdvanceEnter(state, facts, 250);
    CHECK(verdict.step == EnterStep::SendTrigger && state.stage == EnterStage::Arrive);
    // The transfer: out of the world, then on the dungeon's map.
    EnterFacts transit;
    transit.available = false;
    transit.inTransit = true;
    CHECK(AdvanceEnter(state, transit, 250).step == EnterStep::Wait);
    EnterFacts there;
    there.arrived = true;
    CHECK(AdvanceEnter(state, there, 250).step == EnterStep::Succeed);
}

static void TestAlreadyInRange()
{
    EnterState state = MakeEnterState(0.0f, 0.0f, false);
    EnterFacts facts;
    facts.inRange = true;
    CHECK(AdvanceEnter(state, facts, 0).step == EnterStep::SendTrigger);
}

static void TestFollowerWaitsForLead()
{
    EnterState state = MakeEnterState(10.0f, 10.0f, true);
    EnterFacts facts = Far(10.0f);
    facts.inRange = true;
    // In range, the lead bot not in yet: hold, do not send.
    CHECK(AdvanceEnter(state, facts, 250).step == EnterStep::Wait && state.stage == EnterStage::WaitLead);
    CHECK(AdvanceEnter(state, facts, 250).step == EnterStep::Wait);
    // Combat while waiting ends it.
    EnterFacts fight = facts;
    fight.inCombat = true;
    EnterState fightState = state;
    CHECK(std::strcmp(AdvanceEnter(fightState, fight, 250).code, kCodeInCombat) == 0);
    // The lead bot is in: now the follower goes.
    facts.leadEntered = true;
    CHECK(AdvanceEnter(state, facts, 250).step == EnterStep::SendTrigger && state.stage == EnterStage::Arrive);

    // A follower that reaches the trigger after the lead bot got in goes at once.
    EnterState late = MakeEnterState(10.0f, 10.0f, true);
    EnterFacts lateFacts = Far(2.0f);
    lateFacts.inRange = true;
    lateFacts.leadEntered = true;
    CHECK(AdvanceEnter(late, lateFacts, 250).step == EnterStep::SendTrigger);
}

static void TestFollowerStaysOutWhenLeadFails()
{
    EnterState state = MakeEnterState(10.0f, 10.0f, true);
    EnterFacts facts = Far(10.0f);
    facts.leadFailed = true;
    EnterVerdict verdict = AdvanceEnter(state, facts, 250);
    CHECK(verdict.step == EnterStep::Fail && std::strcmp(verdict.code, kCodeCancelled) == 0);
    // The lead bot itself is not a follower.
    EnterState lead = MakeEnterState(10.0f, 10.0f, false);
    CHECK(AdvanceEnter(lead, facts, 250).step == EnterStep::Wait);
}

static void TestFollowerWaitTimeout()
{
    EnterState state = MakeEnterState(10.0f, 10.0f, true);
    EnterFacts facts = Far(10.0f);
    facts.inRange = true;
    CHECK(AdvanceEnter(state, facts, 250).step == EnterStep::Wait);
    EnterVerdict verdict = AdvanceEnter(state, facts, state.leadWaitBudgetMs);
    CHECK(verdict.step == EnterStep::Fail && std::strcmp(verdict.code, kCodeTimeout) == 0);
}

static void TestWalkRefusals()
{
    // Stalled short of the trigger.
    EnterState stall = MakeEnterState(30.0f, 30.0f, false);
    EnterFacts facts = Far(30.0f);
    EnterVerdict verdict;
    for (uint32_t t = 0; t < kStallMs + 1000; t += 250)
    {
        verdict = AdvanceEnter(stall, facts, 250);
        if (verdict.step == EnterStep::Fail)
            break;
    }
    CHECK(verdict.step == EnterStep::Fail && std::strcmp(verdict.code, kCodeUnreachable) == 0);

    // Walk budget used up while still closing in, too slowly: 60 yd, 1.2 yd of progress every 1.5 s.
    EnterState slow = MakeEnterState(60.0f, 60.0f, false);
    float distance = 60.0f;
    verdict = EnterVerdict();
    for (uint32_t t = 0; t < kMaxWalkMs + 1000 && verdict.step == EnterStep::Wait; t += 250)
    {
        if (t > 0 && t % 1500 == 0)
            distance -= 1.2f;
        verdict = AdvanceEnter(slow, Far(distance), 250);
    }
    CHECK(verdict.step == EnterStep::Fail && std::strcmp(verdict.code, kCodeTimeout) == 0);

    // Combat, death, a teleport away.
    EnterState fight = MakeEnterState(30.0f, 30.0f, false);
    EnterFacts inFight = Far(30.0f);
    inFight.inCombat = true;
    CHECK(std::strcmp(AdvanceEnter(fight, inFight, 250).code, kCodeInCombat) == 0);
    EnterState dead = MakeEnterState(30.0f, 30.0f, false);
    EnterFacts gone = Far(30.0f);
    gone.available = false;
    CHECK(std::strcmp(AdvanceEnter(dead, gone, 250).code, kCodeUnavailable) == 0);
}

static void TestArriveFailures()
{
    // The packet went, the bot is back outside and not travelling: the entry did not go through.
    EnterState state = MakeEnterState(0.0f, 0.0f, false);
    EnterFacts facts;
    facts.inRange = true;
    CHECK(AdvanceEnter(state, facts, 0).step == EnterStep::SendTrigger);
    EnterFacts outside;
    EnterVerdict verdict = AdvanceEnter(state, outside, 250);
    CHECK(verdict.step == EnterStep::Fail && std::strcmp(verdict.code, kCodeFailed) == 0);

    // A transfer that never ends times out.
    EnterState slow = MakeEnterState(0.0f, 0.0f, false);
    CHECK(AdvanceEnter(slow, facts, 0).step == EnterStep::SendTrigger);
    EnterFacts transit;
    transit.available = false;
    transit.inTransit = true;
    CHECK(AdvanceEnter(slow, transit, 1000).step == EnterStep::Wait);
    verdict = AdvanceEnter(slow, transit, kEnterArriveMs);
    CHECK(verdict.step == EnterStep::Fail && std::strcmp(verdict.code, kCodeTimeout) == 0);
}

static EnterTally Tally(uint32_t total)
{
    EnterTally tally;
    tally.total = total;
    return tally;
}

static void TestTally()
{
    // All in: one ACK with the count.
    EnterTally tally = Tally(3);
    NoteEntered(tally, true);
    CHECK(tally.leadEntered && !EnterComplete(tally));
    NoteEntered(tally, false);
    NoteEntered(tally, false);
    CHECK(EnterComplete(tally));
    EnterAnswer answer = FinalEnterAnswer(tally);
    CHECK(answer.ok && answer.count == 3 && answer.detail == "entered");

    // The lead bot fails: ERR with its reason; the followers' "cancelled" does not hide it.
    tally = Tally(3);
    NoteFailed(tally, true, "Tanky", kCodeRequirement, "Needs level 44 (the bot is level 40).");
    CHECK(tally.leadFailed && !tally.leadEntered);
    NoteFailed(tally, false, "Healy", kCodeCancelled, "The lead bot did not get in, so the rest stay out.");
    NoteFailed(tally, false, "Dps", kCodeCancelled, "The lead bot did not get in, so the rest stay out.");
    CHECK(EnterComplete(tally));
    answer = FinalEnterAnswer(tally);
    CHECK(!answer.ok && answer.code == kCodeRequirement);
    CHECK(answer.message == "Tanky: Needs level 44 (the bot is level 40).");

    // A real failure replaces an earlier cancellation.
    tally = Tally(2);
    NoteFailed(tally, false, "A", kCodeCancelled, "stopped");
    NoteFailed(tally, false, "B", kCodeInstanceLimit, "too many");
    answer = FinalEnterAnswer(tally);
    CHECK(answer.code == kCodeInstanceLimit && answer.message == "B: too many");

    // The first real failure stays.
    tally = Tally(3);
    NoteFailed(tally, false, "A", kCodeTimeout, "slow");
    NoteFailed(tally, false, "B", kCodeUnreachable, "stuck");
    NoteEntered(tally, false);
    answer = FinalEnterAnswer(tally);
    CHECK(answer.code == kCodeTimeout && answer.message == "A: slow (1 of 3 got in)");
    CHECK(answer.count == 1);

    // The lead is in and a follower fails: partial, an ERR that says how many got in.
    tally = Tally(2);
    NoteEntered(tally, true);
    NoteFailed(tally, false, "Healy", kCodeFailed, "The entry did not go through.");
    answer = FinalEnterAnswer(tally);
    CHECK(!answer.ok && answer.count == 1 && answer.message.find("1 of 2 got in") != std::string::npos);

    // One bot named: scope bot, total 1.
    tally = Tally(1);
    NoteEntered(tally, false);
    answer = FinalEnterAnswer(tally);
    CHECK(answer.ok && answer.count == 1);
    CHECK(!EnterComplete(Tally(1)));
}

int main()
{
    TestParse();
    TestEntranceAndNearest();
    TestRequirements();
    TestBudgets();
    TestLeadWalksThenArrives();
    TestAlreadyInRange();
    TestFollowerWaitsForLead();
    TestFollowerStaysOutWhenLeadFails();
    TestFollowerWaitTimeout();
    TestWalkRefusals();
    TestArriveFailures();
    TestTally();
    std::printf("remote commander enter: %d checks passed\n", checks);
    return 0;
}
