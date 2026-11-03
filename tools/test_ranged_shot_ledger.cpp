// Standalone check for runtime/RangedShotLedger.h: the wedged rescue reads a ranged bot's shots from an authoritative
// count, never from a snapshot of the ranged attack timer. A bot that keeps firing every 1.4, 2.0 or 3.0 s keeps
// participating however the once-a-second reads fall against its shots, and a bot with auto-repeat on that fires
// nothing for the window becomes eligible for the rescue.
//   g++ -std=c++17 -Wall -Wextra tools/test_ranged_shot_ledger.cpp -o /tmp/test_ranged_shot_ledger
#include "../runtime/RangedShotLedger.h"
#include "../runtime/DungeonRunPolicy.h"
#include "../runtime/MemberParticipationPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::DungeonRun;

// The glue's clock, as Commander.cpp MemberProgressed/ReadMemberParticipation drive it: one read a second against
// the ledger count. Returns false once the bot reads as not participating at some read after `warmupMs`.
struct Sim
{
    RangedShotLedger ledger;
    std::uint32_t actMs = 0;
    std::uint32_t lastCount = 0;
    bool observed = false;

    // One run tick at `now`: the read, then MemberParticipates on the resulting fact.
    bool Tick(std::uint64_t guid, std::uint32_t now)
    {
        std::uint32_t const count = ledger.Count(guid);
        bool const shot = RangedShotFiredSince(observed, lastCount, count);
        if (ParticipationProgress(true, 0, observed, 0, false, shot))
            actMs = now;
        lastCount = count;
        observed = true;
        MemberFact m;
        m.actedMs = actMs ? now - actMs : 0xFFFFFFFF;
        return MemberParticipates(m);
    }
};

// A bot firing every `periodMs`, first shot at `phaseMs`, read once a second from t = 1000: from its second read on
// it participates at every read, whatever the phase. `shootingUntilMs` stops the shots (the bot cannot shoot).
static bool KeepsParticipating(std::uint32_t periodMs, std::uint32_t phaseMs)
{
    Sim sim;
    std::uint64_t const guid = 42;
    std::uint32_t nextShot = phaseMs;
    for (std::uint32_t now = 1000; now <= 120000; now += 1000)
    {
        while (nextShot <= now)
        {
            sim.ledger.Record(guid);
            nextShot += periodMs;
        }
        bool const participates = sim.Tick(guid, now);
        // The very first reads have no earlier count to compare (observed is false), and the first full shot period
        // may not have passed; after the window of the first shot the bot must always participate.
        if (now > phaseMs + 2 * periodMs + 1000 && !participates)
            return false;
    }
    return true;
}

static void TestFiringBotKeepsParticipatingAtAnyPhase()
{
    for (std::uint32_t period : {1400u, 2000u, 3000u})
        for (std::uint32_t phase = 0; phase < period; phase += 50)
            CHECK(KeepsParticipating(period, phase));
}

// Auto-repeat on, fired for a while, then no shots at all: the clock goes stale and the member is eligible once the
// participation window has passed.
static void TestSilentBotBecomesEligible()
{
    Sim sim;
    std::uint64_t const guid = 7;
    std::uint32_t now = 1000;
    for (; now <= 20000; now += 1000)
    {
        if (now % 2000 == 0)
            sim.ledger.Record(guid);
        sim.Tick(guid, now);
    }
    CHECK(sim.Tick(guid, now)); // fired a moment ago: participating
    // Shots stop: no count moves. Within the window it still participates, then it does not.
    bool stale = false;
    std::uint32_t staleAfter = 0;
    for (std::uint32_t t = now + 1000; t <= now + 30000; t += 1000)
    {
        bool const participates = sim.Tick(guid, t);
        if (!participates && !stale)
        {
            stale = true;
            staleAfter = t - now;
        }
        if (stale)
            CHECK(!participates);
    }
    CHECK(stale);
    CHECK(staleAfter <= 8000);
}

// A count that never moves (auto-repeat on, the bot cannot shoot) is no progress from the first read.
static void TestNeverShootingIsNoProgress()
{
    Sim sim;
    for (std::uint32_t now = 1000; now <= 30000; now += 1000)
        CHECK(!sim.Tick(1, now));
}

// Another bot's shots do not count for this one.
static void TestCountsArePerBot()
{
    RangedShotLedger ledger;
    ledger.Record(1);
    ledger.Record(1);
    ledger.Record(2);
    CHECK(ledger.Count(1) == 2);
    CHECK(ledger.Count(2) == 1);
    CHECK(ledger.Count(3) == 0);
    CHECK(!RangedShotFiredSince(true, 2, ledger.Count(1)));
    CHECK(RangedShotFiredSince(true, 1, ledger.Count(1)));
    CHECK(!RangedShotFiredSince(false, 0, 5)); // the first read: nothing to compare with
}

// The ledger never outgrows its bound; an entry dropped under it restarts at 0, which reads as a shot (safe side).
static void TestBoundedLedger()
{
    RangedShotLedger ledger;
    for (std::uint64_t guid = 1; guid <= RangedShotLedger::kMaxEntries + 100; ++guid)
        ledger.Record(guid);
    CHECK(ledger.Size() <= RangedShotLedger::kMaxEntries);
}

// Without the host hook armed, evidence is missing for a bot with auto-repeat on (fail safe: not eligible), and
// never for a bot without it or once armed.
static void TestMissingEvidenceFailsSafe()
{
    RangedShotLedger ledger;
    CHECK(!ledger.Armed());
    CHECK(RangedShotEvidenceMissing(ledger.Armed(), true));
    CHECK(!RangedShotEvidenceMissing(ledger.Armed(), false));
    ledger.Arm();
    CHECK(ledger.Armed());
    CHECK(!RangedShotEvidenceMissing(ledger.Armed(), true));
}

int main()
{
    TestFiringBotKeepsParticipatingAtAnyPhase();
    TestSilentBotBecomesEligible();
    TestNeverShootingIsNoProgress();
    TestCountsArePerBot();
    TestBoundedLedger();
    TestMissingEvidenceFailsSafe();
    std::cout << "test_ranged_shot_ledger: ok\n";
    return 0;
}
