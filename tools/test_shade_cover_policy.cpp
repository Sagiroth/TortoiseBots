// Standalone check for the Shade cover rules in ai/playerbot/BossTacticsPolicy.h (walk 56 F3: Shades on uncursed
// healers):
//   g++ -std=c++17 -Wall -Wextra tools/test_shade_cover_policy.cpp -o /tmp/test_shade_cover_policy && /tmp/test_shade_cover_policy
#include "../ai/playerbot/BossTacticsPolicy.h"
#include "../ai/playerbot/ThreatCapPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

// Step 1: who the cover row asks about. Only a cursed bot can hit a Shade; a healer keeps its own carrier-strike guards
// and the main tank never leaves the boss.
static void TestCoverRowAsksCursedDpsAndFreeOffTank()
{
    CHECK(ShadeCoverAsks(CarrierStrikeRole::Dps, true, false));
    CHECK(ShadeCoverAsks(CarrierStrikeRole::OffTank, true, false));
    CHECK(!ShadeCoverAsks(CarrierStrikeRole::OffTank, true, true));   // an off-tank holding the boss is not asked
    CHECK(!ShadeCoverAsks(CarrierStrikeRole::MainTank, true, false));
    CHECK(!ShadeCoverAsks(CarrierStrikeRole::MainTank, true, true));
    CHECK(!ShadeCoverAsks(CarrierStrikeRole::Healer, true, false));
    CHECK(!ShadeCoverAsks(CarrierStrikeRole::Dps, false, false));     // uncursed: cannot hit the Shade
    CHECK(!ShadeCoverAsks(CarrierStrikeRole::OffTank, false, false));
}

// Step 1: the reason word, one case per word.
static void TestCoverRowReasonWords()
{
    ShadeCoverFacts on;
    on.distanceYd = 12.0f;
    on.onThisShade = true;
    on.hasTarget = true;
    CHECK(ShadeCoverMissReason(on, kShadeCoverReachYd) == ShadeCoverMiss::None);

    ShadeCoverFacts f;
    f.distanceYd = 12.0f;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::NoTarget);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::NoTarget), "no_target") == 0);
    f.hasTarget = true;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::OtherTarget);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::OtherTarget), "other_target") == 0);
    f.onBoss = true;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::OnBoss);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::OnBoss), "on_boss") == 0);
    f.threatGate = true;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::ThreatGate);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::ThreatGate), "threat_gate") == 0);
    f.distanceYd = 31.0f;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::OutOfRange);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::OutOfRange), "out_of_range") == 0);
    f.onOtherShade = true; // busy on another Shade wins over range
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::OtherShade);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::OtherShade), "other_shade") == 0);
    f.charmed = true;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::Charmed);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::Charmed), "charmed") == 0);
    f.alive = false;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::Dead);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::Dead), "dead") == 0);
    // A charmed carrier that the charm happens to point at the Shade is still on it.
    ShadeCoverFacts charmedOn = on;
    charmedOn.charmed = true;
    CHECK(ShadeCoverMissReason(charmedOn, kShadeCoverReachYd) == ShadeCoverMiss::None);
    CHECK(kShadeCoverReachYd == 30.0f);
}

// Step 1: the successor rule names the off-tank only; step 2: the cover (with the carrier strike) lifts it.
static void TestCoverRowThreatGateIsTheOffTanks()
{
    CHECK(ShadeCoverThreatGate(CarrierStrikeRole::OffTank, true));
    CHECK(!ShadeCoverThreatGate(CarrierStrikeRole::OffTank, false));
    CHECK(!ShadeCoverThreatGate(CarrierStrikeRole::Dps, true));
    CHECK(!ShadeCoverThreatGate(CarrierStrikeRole::OffTank, true, true));
}

// --- step 2: AiPlayerbot.DungeonRunShadeHealerCover ----------------------------------------------------------------

static const std::uint32_t kShade = 14986;
static const std::uint32_t kDelusions = 24306;

static BossTacticRow KillFirst(std::uint32_t target, std::uint8_t priority, std::uint32_t botAura)
{
    BossTacticRow row;
    row.map = 309;
    row.bossEntry = 11380;
    row.kind = BossTacticKind::KillFirst;
    row.targetEntry = target;
    row.priority = priority;
    row.botAura = botAura;
    return row;
}

static ActiveBossRows Active(const std::vector<BossTacticRow>& rows)
{
    ActiveBossRows out;
    for (const BossTacticRow& row : rows)
        out.push_back(&row);
    return out;
}

static TacticCandidate Shade(float distance, bool victimHealer)
{
    TacticCandidate c;
    c.entry = kShade;
    c.distance = distance;
    c.pickable = true;
    c.victimHealer = victimHealer;
    return c;
}

// The off-tank at Jin'do: cursed, the boss is not on it, a threat_successor fight, the raid otherwise safe.
static CarrierStrikeInput FreeOffTank()
{
    CarrierStrikeInput in;
    in.role = CarrierStrikeRole::OffTank;
    in.cursed = true;
    in.shadeInReach = true;
    in.holdsBoss = false;
    in.threatSuccessor = true;
    in.lowestMemberPct = 72;
    in.tankPct = 80;
    in.otherHealers = 3;
    in.manaPct = 60;
    in.dispelNowPending = false;
    in.shadeCover = false;
    in.lowestCoverPct = 72;
    return in;
}

// The spec's failing case: the off-tank is cursed and not the holder, and a Shade eats a healer 12 yd away. Before the
// key the successor rule kept it on the boss (ThreatSuccessor); with it the off-tank picks that Shade and strikes.
static void cursed_offtank_takes_shade_on_healer()
{
    std::vector<BossTacticRow> rows{KillFirst(kShade, 0, kDelusions)};
    std::vector<TacticCandidate> c{Shade(12.0f, true)};
    int const pick = PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true);
    CHECK(pick == 0);
    CarrierStrikeInput in = FreeOffTank();
    in.shadeCover = ShadeCoverTier(c[pick], true); // what the trigger reads: the pick's victim is a healer, within 30 yd
    CHECK(in.shadeCover);
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Strike);
    // Under the cover the eaten healer is left out of the lowest-member line (the low member is usually that healer) ...
    in.lowestMemberPct = 20;
    in.lowestCoverPct = 72;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Strike);
    // ... the main-tank line still does.
    in.tankPct = 59;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardTank);
}

// The holder stays: a cursed off-tank the boss is hitting never leaves it, cover or not (the gate stays).
static void TestHoldingOffTankDoesNotLeave()
{
    CarrierStrikeInput in = FreeOffTank();
    in.holdsBoss = true;
    in.shadeCover = true;
    CHECK(CarrierStrikeAllowed(in, 50) != CarrierStrikeVerdict::Strike);
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::ThreatSuccessor);
    in.threatSuccessor = false;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::HoldsBoss);
    // The main tank never moves, whatever the cover says.
    CarrierStrikeInput mt = FreeOffTank();
    mt.role = CarrierStrikeRole::MainTank;
    mt.shadeCover = true;
    CHECK(CarrierStrikeAllowed(mt, 50) == CarrierStrikeVerdict::MainTank);
    mt.holdsBoss = true;
    CHECK(CarrierStrikeAllowed(mt, 50) == CarrierStrikeVerdict::MainTank);
}

// An uncursed bot never picks a Shade: the fight scan drops the gated row for it, so no carrier row is open, and the
// strike refuses it even with the cover on.
static void TestUncursedNeverPicksAShade()
{
    std::vector<BossTacticRow> uncursed{KillFirst(15112, 2, 0)}; // the Brain Wash Totem row stays, the Shade row is gone
    std::vector<TacticCandidate> c{Shade(5.0f, true), Shade(12.0f, true)};
    CHECK(PickCarrierOverride(Active(uncursed), c, kCarrierHoldReachYd, true) == -1);
    CHECK(PickCarrierOverride(Active(uncursed), c, kCarrierHoldReachYd, false) == -1);
    CarrierStrikeInput in = FreeOffTank();
    in.cursed = false;
    in.shadeCover = true;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::ThreatSuccessor);
    in.threatSuccessor = false;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::NotCarrier);
    CHECK(!ShadeCoverAsks(CarrierStrikeRole::Dps, false, false));
}

// Flag off: the pick is the nearest Shade and the successor rule holds the off-tank, exactly as before.
static void TestFlagOffKeepsTodaysPick()
{
    std::vector<BossTacticRow> rows{KillFirst(kShade, 0, kDelusions)};
    std::vector<TacticCandidate> c{Shade(20.0f, true), Shade(5.0f, false)};
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd) == 1);
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, false) == 1);
    CarrierStrikeInput in = FreeOffTank(); // shadeCover false: what the trigger reads with the key off
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::ThreatSuccessor);
    // A Shade on a damage dealer never lets the off-tank go either (the cover is a healer's).
    in.shadeCover = ShadeCoverTier(c[1], true);
    CHECK(!in.shadeCover);
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::ThreatSuccessor);
}

// Two Shades: the one on a healer comes first, even when the other is nearer; past 30 yd the nearest wins again.
static void TestTwoShadesHealerFirst()
{
    std::vector<BossTacticRow> rows{KillFirst(kShade, 0, kDelusions)};
    std::vector<TacticCandidate> c{Shade(5.0f, false), Shade(20.0f, true)};
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true) == 1);
    // Two on healers: the nearer of them.
    c.push_back(Shade(14.0f, true));
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true) == 2);
    // Healer Shades past the cover reach rank as any other Shade.
    c[1].distance = 31.0f;
    c[2].distance = 35.0f;
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true) == 0);
    // An unpickable Shade on a healer is no pick.
    c[1].distance = 10.0f;
    c[1].pickable = false;
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true) == 0);
}

// A cursed healer keeps every guard of its own: the cover changes nothing for it.
static void TestCursedHealerKeepsItsGuards()
{
    CarrierStrikeInput in = FreeOffTank();
    in.role = CarrierStrikeRole::Healer;
    in.shadeCover = true;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Strike);
    in.lowestMemberPct = 20;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardLowMember);
    in.lowestMemberPct = 72;
    in.otherHealers = 1;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardHealers);
    // A cursed DPS is the kill-first node's, never the strike's.
    CarrierStrikeInput dps = FreeOffTank();
    dps.role = CarrierStrikeRole::Dps;
    dps.shadeCover = true;
    CHECK(CarrierStrikeAllowed(dps, 50) == CarrierStrikeVerdict::Dps);
}

// Step 1: a 2 s grace, then one row per 5 s per (member, Shade); a pause past the seen grace starts a new miss.
static void TestCoverRowThrottle()
{
    ShadeCoverLogRecord r;
    CHECK(!ShadeCoverLogDue(r, 1000));  // the miss starts
    CHECK(!ShadeCoverLogDue(r, 2000));  // 1 s: still the grace
    CHECK(ShadeCoverLogDue(r, 3000));   // 2 s: due
    CHECK(!ShadeCoverLogDue(r, 4000));  // noted every second, as the trigger does
    CHECK(!ShadeCoverLogDue(r, 5000));
    CHECK(!ShadeCoverLogDue(r, 6000));
    CHECK(!ShadeCoverLogDue(r, 7000));
    CHECK(!ShadeCoverLogDue(r, 7999));  // 4.999 s since the row
    CHECK(ShadeCoverLogDue(r, 8000));   // 5 s
    // Not noted for more than the seen grace (3 s): a new miss, with its own 2 s grace.
    CHECK(!ShadeCoverLogDue(r, 20000));
    CHECK(!ShadeCoverLogDue(r, 21000));
    CHECK(ShadeCoverLogDue(r, 22000));
    // The clock wrap.
    ShadeCoverLogRecord w;
    CHECK(!ShadeCoverLogDue(w, 0xFFFFFC00u));
    CHECK(ShadeCoverLogDue(w, 0x00000400u)); // 2048 ms later across the wrap
    CHECK(kShadeCoverLogGapMs == 5000);
    CHECK(kShadeCoverMaxEntries > 0);
}

// M1: the off-tank pulled the Shade onto itself (victim = self). Without the latch the cover dropped at once and the
// verdict went back to ThreatSuccessor; with it the strike goes on until the Shade dies, the curse ends or 15 s pass.
static void cover_holds_after_offtank_pulls_shade()
{
    ShadeCoverLatch latch;
    std::uint64_t const shade = 0xF130003A8A000001ull;
    CHECK(ShadeCoverLatchHolds(latch, shade, true, true, false, 1000));    // on the priest: taken
    CHECK(ShadeCoverLatchHolds(latch, shade, true, false, true, 3000));    // pulled onto the off-tank: still covered
    CarrierStrikeInput in = FreeOffTank();
    in.shadeCover = true;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Strike);
    CHECK(ShadeCoverLatchHolds(latch, shade, true, false, true, 15999));   // 14.999 s after the healer
    CHECK(!ShadeCoverLatchHolds(latch, shade, true, false, true, 16000));  // 15 s: over
    CHECK(latch.shade == 0);
    // Back on a healer: a fresh latch, and the window restarts from that sight.
    CHECK(ShadeCoverLatchHolds(latch, shade, true, true, false, 20000));
    CHECK(ShadeCoverLatchHolds(latch, shade, true, false, true, 34000));
    // The Shade's victim is a damage dealer: no cover, latch or not.
    CHECK(!ShadeCoverLatchHolds(latch, shade, true, false, false, 34500));
    // The curse ends: the latch goes with it.
    CHECK(ShadeCoverLatchHolds(latch, shade, true, true, false, 40000));
    CHECK(!ShadeCoverLatchHolds(latch, shade, false, false, true, 41000));
    CHECK(!ShadeCoverLatchHolds(latch, shade, true, false, true, 42000));
    // The Shade dies (the pick no longer names it): the latch ends.
    CHECK(ShadeCoverLatchHolds(latch, shade, true, true, false, 50000));
    CHECK(!ShadeCoverLatchHolds(latch, 0, true, false, false, 51000));
    CHECK(!ShadeCoverLatchHolds(latch, shade, true, false, true, 52000));
    // Another Shade on the off-tank was never latched.
    CHECK(ShadeCoverLatchHolds(latch, shade, true, true, false, 60000));
    CHECK(!ShadeCoverLatchHolds(latch, shade + 1, true, false, true, 61000));
}

// M2: the eaten healer is the only one left out; the off-tank at 25 % does not leave Jin'do.
static void cover_keeps_own_low_health_guard()
{
    CarrierStrikeInput in = FreeOffTank();
    in.shadeCover = true;
    in.lowestMemberPct = 25; // the off-tank itself
    in.lowestCoverPct = 25;  // ... still counted under the cover
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardLowMember);
    // Another member low (not the Shade's victim): held too.
    in.lowestMemberPct = 30;
    in.lowestCoverPct = 30;
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardLowMember);
    // A caller that never filled the cover line keeps the off-tank home.
    CarrierStrikeInput unfilled;
    unfilled = FreeOffTank();
    unfilled.shadeCover = true;
    unfilled.lowestCoverPct = 0;
    CHECK(CarrierStrikeAllowed(unfilled, 50) == CarrierStrikeVerdict::GuardLowMember);
}

// M3: a Shade 12 yd below in the troll pit is no cover pick, whatever its victim.
static void TestPitShadeIsNotCovered()
{
    std::vector<BossTacticRow> rows{KillFirst(kShade, 0, kDelusions)};
    std::vector<TacticCandidate> c{Shade(8.0f, false), Shade(14.0f, true)};
    c[1].dropYd = 12.0f; // the bot stands 12 yd above it
    CHECK(!ShadeCoverTier(c[1], true));
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true) == 0);
    c[1].dropYd = -12.0f; // the bot in the pit, the Shade up on the room floor
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true) == 0);
    c[1].dropYd = 5.5f;   // a step or a slope: same floor
    CHECK(PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true) == 1);
    CHECK(!ShadeCoverSameFloor(12.0f));
    CHECK(ShadeCoverSameFloor(kCarrierPitDropYd));
    // The row calls a pit Shade out of range.
    ShadeCoverFacts f;
    f.distanceYd = 14.0f;
    f.dropYd = 12.0f;
    f.onBoss = true;
    f.hasTarget = true;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::OutOfRange);
}

// A tie at equal distance with the key on: two healer Shades keep the first one (the old tie-break); a healer Shade
// against a plain one at the same distance wins.
static void TestEqualDistanceTie()
{
    std::vector<BossTacticRow> rows{KillFirst(kShade, 0, kDelusions)};
    std::vector<TacticCandidate> both{Shade(10.0f, true), Shade(10.0f, true)};
    CHECK(PickCarrierOverride(Active(rows), both, kCarrierHoldReachYd, true) == 0);
    std::vector<TacticCandidate> mixed{Shade(10.0f, false), Shade(10.0f, true)};
    CHECK(PickCarrierOverride(Active(rows), mixed, kCarrierHoldReachYd, true) == 1);
    CHECK(PickCarrierOverride(Active(rows), mixed, kCarrierHoldReachYd, false) == 0);
    // A healer Shade at 31-40 yd against a plain one at 10: the nearer, as before.
    std::vector<TacticCandidate> far{Shade(10.0f, false), Shade(36.0f, true)};
    CHECK(PickCarrierOverride(Active(rows), far, kCarrierHoldReachYd, true) == 0);
}

// L2: a member that died while missing the Shade gets one `dead` row per Shade, not one every 5 s.
static void TestLogBookDeadOnce()
{
    ShadeCoverLogBook book;
    CHECK(!book.Note(1, 7, true, false, 1000));
    CHECK(book.Note(1, 7, true, false, 3000));      // the miss row after the grace
    CHECK(book.MissOpen(1, 7, 3500));
    CHECK(!book.Note(1, 7, true, true, 4000));      // died: inside the 5 s gap
    int deadRows = 0;
    for (std::uint32_t t = 5000; t <= 70000; t += 1000)
        deadRows += book.Note(1, 7, true, true, t) ? 1 : 0;
    CHECK(deadRows == 1);
    CHECK(book.MissOpen(1, 7, 70500));
    // On the Shade: the record is dropped.
    CHECK(!book.Note(2, 7, true, false, 1000));
    CHECK(book.Has(2, 7));
    CHECK(!book.Note(2, 7, false, false, 1500));
    CHECK(!book.Has(2, 7));
    CHECK(!book.MissOpen(2, 7, 1600));
}

// L3: at the cap the book drops stale records first, then the oldest quarter; it never clears the live misses.
static void TestLogBookCapPrunesOldest()
{
    ShadeCoverLogBook book;
    std::uint32_t const now = 100000;
    // Fill to the cap, all live; the first ones noted earliest.
    for (std::uint64_t i = 0; i < kShadeCoverMaxEntries; ++i)
        book.Note(i, 1, true, false, now - 2000 + static_cast<std::uint32_t>(i)); // within the 3 s seen grace
    CHECK(book.Size() == kShadeCoverMaxEntries);
    book.Note(9999, 1, true, false, now);
    CHECK(book.Size() <= kShadeCoverMaxEntries);
    CHECK(book.Size() > kShadeCoverMaxEntries / 2); // not cleared
    CHECK(book.Has(9999, 1));
    CHECK(!book.Has(0, 1));                                     // the oldest went
    CHECK(book.Has(kShadeCoverMaxEntries - 1, 1));             // the youngest stayed
    // Stale records go first: a full book of stale records leaves only the new one.
    ShadeCoverLogBook stale;
    for (std::uint64_t i = 0; i < kShadeCoverMaxEntries; ++i)
        stale.Note(i, 2, true, false, 1000);
    stale.Note(9999, 2, true, false, 1000 + 10000);
    CHECK(stale.Size() == 1);
    // A live record keeps its grace and gap across a prune: its row is not due again inside 5 s.
    ShadeCoverLogBook keep;
    keep.Note(5000, 3, true, false, now - 2500);
    CHECK(keep.Note(5000, 3, true, false, now - 500)); // its row
    for (std::uint64_t i = 0; i + 1 < kShadeCoverMaxEntries; ++i)
        keep.Note(i, 3, true, false, now - 2000 + static_cast<std::uint32_t>(i)); // all older than keep's last note
    CHECK(keep.Size() == kShadeCoverMaxEntries);
    keep.Note(9998, 3, true, false, now);
    CHECK(keep.Has(5000, 3));
    CHECK(!keep.Note(5000, 3, true, false, now + 100)); // its 5 s gap survived the prune
}

// L6: an off-tank the carrier strike's guards hold back is named `guard`, not `on_boss`.
static void TestGuardWord()
{
    CHECK(ShadeCoverOffTankGuard(CarrierStrikeRole::OffTank, true, 59, 80, 50));  // main tank under 60 %
    CHECK(ShadeCoverOffTankGuard(CarrierStrikeRole::OffTank, true, 80, 40, 50));  // own health under LowHealth
    CHECK(!ShadeCoverOffTankGuard(CarrierStrikeRole::OffTank, true, 80, 80, 50));
    CHECK(!ShadeCoverOffTankGuard(CarrierStrikeRole::OffTank, false, 10, 10, 50)); // cover off: threat_gate's case
    CHECK(!ShadeCoverOffTankGuard(CarrierStrikeRole::Dps, true, 10, 10, 50));
    ShadeCoverFacts f;
    f.distanceYd = 10.0f;
    f.onBoss = true;
    f.hasTarget = true;
    f.guard = true;
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::Guard);
    CHECK(std::strcmp(ShadeCoverMissWord(ShadeCoverMiss::Guard), "guard") == 0);
    f.threatGate = true; // the successor rule comes first
    CHECK(ShadeCoverMissReason(f, kShadeCoverReachYd) == ShadeCoverMiss::ThreatGate);
}

// Codex round 2: the off-tank pulled its Shade (victim = self, no healer tier any more) and a nearer Shade on a damage
// dealer stands beside it. The general pick takes the nearer one; resolving the latch first keeps the pulled Shade, and
// the latch holds on it. Without the latch-first resolve the latch saw the other guid and ended.
static void latched_shade_survives_a_nearer_shade()
{
    std::uint64_t const pulled = 0xF130003A8A000010ull;
    std::uint64_t const nearer = 0xF130003A8A000011ull;
    std::vector<BossTacticRow> rows{KillFirst(kShade, 0, kDelusions)};
    std::vector<TacticCandidate> c{Shade(8.0f, false), Shade(3.0f, false)}; // [0] pulled, on the off-tank; [1] nearer
    std::uint64_t const guids[2] = {pulled, nearer};
    int const general = PickCarrierOverride(Active(rows), c, kCarrierHoldReachYd, true);
    CHECK(general == 1); // the general pick replaces the pulled Shade

    ShadeCoverLatch latch;
    CHECK(ShadeCoverLatchHolds(latch, pulled, true, true, false, 1000)); // taken off the priest
    ShadeCoverLatchedShade s;
    s.found = true;
    s.carrierEntry = true;
    s.alive = true;
    s.attackable = true;
    s.distanceYd = 8.0f;
    s.onSelf = true;
    std::uint64_t const pick = ShadeCoverResolvePick(latch, true, s, guids[general], 4000);
    CHECK(pick == pulled);
    CHECK(ShadeCoverLatchHolds(latch, pick, true, false, true, 4000)); // the cover holds on it
    // What the general pick alone did: another guid ends the latch.
    ShadeCoverLatch lost;
    CHECK(ShadeCoverLatchHolds(lost, pulled, true, true, false, 1000));
    CHECK(!ShadeCoverLatchHolds(lost, guids[general], true, false, false, 4000));

    // Each fact ends the precedence and the general pick stands.
    auto const general_if = [&](ShadeCoverLatchedShade f, bool cursed, std::uint32_t now)
    { return ShadeCoverResolvePick(latch, cursed, f, nearer, now) == nearer; };
    CHECK(general_if(s, false, 4000));                        // the curse ended
    CHECK(general_if(s, true, 1000 + kShadeCoverLatchMs));     // the 15 s window closed
    { ShadeCoverLatchedShade f = s; f.alive = false;       CHECK(general_if(f, true, 4000)); }
    { ShadeCoverLatchedShade f = s; f.found = false;       CHECK(general_if(f, true, 4000)); }
    { ShadeCoverLatchedShade f = s; f.attackable = false;  CHECK(general_if(f, true, 4000)); }
    { ShadeCoverLatchedShade f = s; f.carrierEntry = false; CHECK(general_if(f, true, 4000)); }
    { ShadeCoverLatchedShade f = s; f.onSelf = false;      CHECK(general_if(f, true, 4000)); }
    { ShadeCoverLatchedShade f = s; f.distanceYd = 31.0f;  CHECK(general_if(f, true, 4000)); }
    { ShadeCoverLatchedShade f = s; f.dropYd = 12.0f;      CHECK(general_if(f, true, 4000)); }
    ShadeCoverLatch none;
    CHECK(ShadeCoverResolvePick(none, true, s, nearer, 4000) == nearer);
}

// Opus round 2 (R2-1): the off-tank was Hexed and holds 30 % of the main tank's Jin'do threat; it is cursed and a Shade
// eats a healer. Leaving would drop it off the successor list (KeepsAway keeps a cursed tank only at half the top
// tank's threat), so the cover does not move it: it stays on Jin'do as the successor reference.
static void cover_refused_when_offtank_threat_below_half()
{
    CHECK(!ShadeCoverThreatAllows(300.0f, 1000.0f, kThreatCapOffTankAwayShare));
    CHECK(!ShadeCoverThreatAllows(0.0f, 0.0f, kThreatCapOffTankAwayShare));       // no threat at all
    CHECK(ShadeCoverThreatAllows(500.0f, 1000.0f, kThreatCapOffTankAwayShare));    // the line itself
    CHECK(ShadeCoverThreatAllows(1000.0f, 1000.0f, kThreatCapOffTankAwayShare));   // the top tank
    // The trigger: the cover is the pick's tier and the threat line together.
    std::vector<TacticCandidate> c{Shade(15.0f, true)};
    CarrierStrikeInput in = FreeOffTank();
    in.shadeCover = ShadeCoverTier(c[0], true) && ShadeCoverThreatAllows(300.0f, 1000.0f, kThreatCapOffTankAwayShare);
    CHECK(!in.shadeCover);
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::ThreatSuccessor);
    in.shadeCover = ShadeCoverTier(c[0], true) && ShadeCoverThreatAllows(600.0f, 1000.0f, kThreatCapOffTankAwayShare);
    CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Strike);
}

int main()
{
    TestCoverRowAsksCursedDpsAndFreeOffTank();
    TestCoverRowReasonWords();
    TestCoverRowThreatGateIsTheOffTanks();
    TestCoverRowThrottle();
    cursed_offtank_takes_shade_on_healer();
    TestHoldingOffTankDoesNotLeave();
    TestUncursedNeverPicksAShade();
    TestFlagOffKeepsTodaysPick();
    TestTwoShadesHealerFirst();
    TestCursedHealerKeepsItsGuards();
    cover_holds_after_offtank_pulls_shade();
    cover_keeps_own_low_health_guard();
    TestPitShadeIsNotCovered();
    TestEqualDistanceTie();
    TestLogBookDeadOnce();
    TestLogBookCapPrunesOldest();
    TestGuardWord();
    latched_shade_survives_a_nearer_shade();
    cover_refused_when_offtank_threat_below_half();
    std::cout << "shade cover policy: ok\n";
    return 0;
}
