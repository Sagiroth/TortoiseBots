// Standalone test for the `.bot why` decision trace policy: the reason a
// triggered-but-unchosen action lost, the 20-lines-per-second budget, and the
// start / stop / timeout lifecycle of one trace.
//
// The cooldown fixture is the live case the trace exists for: a protection
// warrior's `taunt` fires from `lose aggro`, isPossible() says no because the
// spell is still on its 10 s cooldown (SpellCastResult NOT_READY), and the
// trace must say `cooldown`, not a bare `not-possible`.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_why_trace_policy.cpp -o /tmp/test_why_trace_policy
//   /tmp/test_why_trace_policy

#include "../runtime/WhyTracePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots::WhyTrace;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static Candidate Fired(char const* name, float relevance)
{
    Candidate c;
    c.name = name;
    c.relevance = relevance;
    return c;
}

int main()
{
    // --- reason classification -------------------------------------------------

    // Live fixture: taunt triggered off a non-tank victim, has its target,
    // the strategy is active, isUseful() true, but the 10 s taunt cooldown is
    // still running when isPossible() asks CanCastSpell (NOT_READY).
    {
        Candidate taunt = Fired("taunt", 41.0f);
        taunt.possible = false;
        taunt.cast = CastCheck::NotReady;
        taunt.spellCooldownMs = 8000;
        CHECK(Classify(taunt) == LossReason::Cooldown);
        CHECK(std::string(LossReasonName(LossReason::Cooldown)) == "cooldown");
    }

    // Same taunt one second later: the spell cooldown is gone and only the
    // global cooldown holds it.
    {
        Candidate taunt = Fired("taunt", 41.0f);
        taunt.possible = false;
        taunt.cast = CastCheck::NotReady;
        taunt.gcdRemainingMs = 640;
        CHECK(Classify(taunt) == LossReason::Gcd);
    }

    // A spell with no own cooldown and NOT_READY is a cast still in flight.
    {
        Candidate cast = Fired("shadow bolt", 40.0f);
        cast.possible = false;
        cast.cast = CastCheck::NotReady;
        CHECK(Classify(cast) == LossReason::Gcd);
    }

    // isPossible() false with an explicit range refusal.
    {
        Candidate reach = Fired("taunt", 40.0f);
        reach.possible = false;
        reach.cast = CastCheck::OutOfRange;
        CHECK(Classify(reach) == LossReason::OutOfRange);
    }

    // isPossible() false before the cast check: no target to taunt.
    {
        Candidate taunt = Fired("taunt", 40.0f);
        taunt.possible = false;
        taunt.hasTarget = false;
        CHECK(Classify(taunt) == LossReason::NoTarget);
        CHECK(std::string(LossReasonName(LossReason::NoTarget)) == "no-target");
    }

    // The action's strategy is not attached to the engine at all.
    {
        Candidate stale = Fired("taunt", 40.0f);
        stale.strategyActive = false;
        stale.hasTarget = false;
        CHECK(Classify(stale) == LossReason::StrategyInactive);
        CHECK(std::string(LossReasonName(LossReason::StrategyInactive)) == "strategy-inactive");
    }

    // isUseful() false: the action cannot help right now.
    {
        Candidate buff = Fired("battle shout", 20.0f);
        buff.useful = false;
        CHECK(Classify(buff) == LossReason::Useless);
        CHECK(std::string(LossReasonName(LossReason::Useless)) == "not-useful");
    }

    // isPossible() false with nothing more specific.
    {
        Candidate mount = Fired("mount", 20.0f);
        mount.possible = false;
        CHECK(Classify(mount) == LossReason::NotPossible);
        CHECK(std::string(LossReasonName(LossReason::NotPossible)) == "not-possible");
    }

    // Never popped: a higher-relevance action ran and broke the loop.
    {
        Candidate queued = Fired("follow", 1.0f);
        queued.queuedOnly = true;
        CHECK(Classify(queued) == LossReason::LowerRelevance);
        CHECK(std::string(LossReasonName(LossReason::LowerRelevance)) == "lower-relevance");
    }

    // The engine's own gates keep their own names.
    {
        Candidate vetoed = Fired("melee", 30.0f);
        vetoed.vetoed = true;
        CHECK(Classify(vetoed) == LossReason::Vetoed);
        Candidate backedOff = Fired("grind", 10.0f);
        backedOff.backedOff = true;
        CHECK(Classify(backedOff) == LossReason::Backoff);
        Candidate prereq = Fired("reach melee", 31.0f);
        prereq.prereq = true;
        CHECK(Classify(prereq) == LossReason::Prereq);
        Candidate failed = Fired("loot", 5.0f);
        failed.failed = true;
        CHECK(Classify(failed) == LossReason::Failed);
    }

    // Every reason has a token, and no two tokens collide.
    {
        LossReason const all[] = {
            LossReason::LowerRelevance, LossReason::NotPossible, LossReason::Useless,
            LossReason::Cooldown, LossReason::Gcd, LossReason::OutOfRange,
            LossReason::NoTarget, LossReason::StrategyInactive, LossReason::Vetoed,
            LossReason::Backoff, LossReason::Prereq, LossReason::Failed,
            LossReason::NotChosen,
        };
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
        {
            CHECK(LossReasonName(all[i]) != nullptr);
            CHECK(LossReasonName(all[i])[0] != '\0');
            for (size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); ++j)
                CHECK(std::string(LossReasonName(all[i])) != LossReasonName(all[j]));
        }
    }

    // --- the fixed log schema --------------------------------------------------

    // A parser reads TBWHY| lines by field position: this is the exact tick
    // line for a taunt that lost to its cooldown while the tank's target was
    // on the healer.
    {
        std::string const line = FormatTickLine("Matth", 12345, 7, "combat", "sunder armor", 45.0f, true,
            "0x00000000000010c1:Jin'do", "0x00000000000020a5:Bubbslord", "taunt(41.00,cooldown)");
        CHECK(line == "TBWHY|tick|Matth|t=12.345|n=7|st=combat|chosen=sunder armor(45.00)|"
            "tgt=0x00000000000010c1:Jin'do|victim=0x00000000000020a5:Bubbslord|lost=taunt(41.00,cooldown)");
    }
    {
        // No chosen action, no target, nothing lost: the fields stay in place
        // with '-' so a reader never has to reflow.
        std::string const line = FormatTickLine("Matth", 0, 1, "non-combat", "", 0.0f, false,
            "", "", "");
        CHECK(line == "TBWHY|tick|Matth|t=0.000|n=1|st=non-combat|chosen=-|tgt=-|victim=-|lost=-");
    }
    {
        // A pipe in a name would split the line: it becomes '/'.
        std::string const line = FormatTickLine("Mat|th", 1000, 2, "combat", "tau|nt", 1.0f, true,
            "0x1:Ji|n'do", "", FormatLostEntry("reach|melee", 2.5f, LossReason::OutOfRange));
        CHECK(line.find("Mat/th") != std::string::npos);
        CHECK(line.find("tau/nt") != std::string::npos);
        CHECK(line.find("Ji/n'do") != std::string::npos);
        CHECK(line.find("reach/melee(2.50,out-of-range)") != std::string::npos);
    }
    {
        CHECK(FormatLostEntry("taunt", 41.0f, LossReason::Cooldown) == "taunt(41.00,cooldown)");
        CHECK(FormatTarget("0xabc", "Jin'do") == "0xabc:Jin'do");
        CHECK(FormatTarget("", "") == "-");
    }

    // --- line budget: 20 lines per second -------------------------------------

    {
        LineBudget budget;
        for (uint32_t i = 0; i < kMaxLinesPerSecond; ++i)
            CHECK(budget.Take(1000));
        CHECK(!budget.Take(1000));
        CHECK(!budget.Take(1999));
        CHECK(budget.dropped() == 2);
        CHECK(budget.Take(2000));  // next window
        CHECK(budget.dropped() == 2);
    }
    {
        // A stalled clock (same millisecond) does not open the next window.
        LineBudget budget;
        for (uint32_t i = 0; i < kMaxLinesPerSecond; ++i)
            CHECK(budget.Take(5000));
        CHECK(!budget.Take(5000));
        CHECK(!budget.Take(5999));
        CHECK(budget.Take(6000));
    }
    {
        // A long jump skips whole windows instead of replaying them.
        LineBudget budget;
        for (uint32_t i = 0; i < kMaxLinesPerSecond; ++i)
            CHECK(budget.Take(0));
        CHECK(!budget.Take(0));
        for (uint32_t i = 0; i < kMaxLinesPerSecond; ++i)
            CHECK(budget.Take(60000));
        CHECK(!budget.Take(60000));
    }

    // --- duration parsing ------------------------------------------------------

    CHECK(ClampSeconds(30) == 30);
    CHECK(ClampSeconds(0) == 1);
    CHECK(ClampSeconds(-5) == 1);
    CHECK(ClampSeconds(120) == 120);
    CHECK(ClampSeconds(121) == 120);
    CHECK(ClampSeconds(100000) == 120);

    {
        unsigned seconds = 0;
        CHECK(ParseSeconds("", seconds) && seconds == kDefaultSeconds);
        CHECK(ParseSeconds("60", seconds) && seconds == 60);
        CHECK(ParseSeconds(" 0 ", seconds) && seconds == 1);
        CHECK(ParseSeconds("999", seconds) && seconds == kMaxSeconds);
        CHECK(!ParseSeconds("abc", seconds));
        CHECK(!ParseSeconds("12x", seconds));
        CHECK(!ParseSeconds("-", seconds));
    }

    // --- session lifecycle: start, stop, timeout -------------------------------

    {
        Session session;
        CHECK(!session.active());
        CHECK(session.Start(1000, 30));
        CHECK(session.active());
        CHECK(session.seconds() == 30);
        CHECK(!session.Start(2000, 30));  // one trace at a time
        CHECK(!session.ShouldStop(30999));
        CHECK(session.ShouldStop(31000)); // exactly the timeout
        session.Stop(StopReason::Timeout, 31000);
        CHECK(!session.active());
        CHECK(session.stopReason() == StopReason::Timeout);
        CHECK(std::string(StopReasonName(session.stopReason())) == "timeout");
        CHECK(!session.ShouldStop(999999));
    }
    {
        // The bot dying does not end the trace; a stop does.
        Session session;
        session.Start(0, 120);
        session.NoteTick(31000);
        session.NoteChosen("taunt", 31000);
        session.NoteLoss(LossReason::Cooldown, 31000);
        CHECK(session.ticks() == 1);
        session.Stop(StopReason::BotGone, 5000);
        CHECK(!session.active());
        CHECK(session.ticks() == 1);
        CHECK(session.chosenCount("taunt") == 1);
        CHECK(session.lossCount(LossReason::Cooldown) == 1);
        CHECK(std::string(StopReasonName(session.stopReason())) == "bot-gone");
    }
    {
        // Timeout is computed from monotonic milliseconds, not wall time; a
        // wrap-free difference is enough for the 120 s cap.
        Session session;
        CHECK(session.Start(100000, 1));
        CHECK(!session.ShouldStop(100500));
        CHECK(session.ShouldStop(101000));
    }
    {
        // A summary names the stop and the counts.
        Session session;
        session.Start(0, 30);
        session.NoteChosen("taunt", 100);
        session.NoteChosen("taunt", 100);
        session.NoteLoss(LossReason::LowerRelevance, 100);
        session.Stop(StopReason::Timeout, 30000);
        std::string const summary = session.Summary("Matth");
        CHECK(summary.find("Matth") != std::string::npos);
        CHECK(summary.find("timeout") != std::string::npos);
        CHECK(summary.find("taunt") != std::string::npos);
    }

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("why trace policy: all checks passed\n");
    return 0;
}
