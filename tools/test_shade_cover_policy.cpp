// Standalone check for the Shade cover rules in ai/playerbot/BossTacticsPolicy.h (walk 56 F3: Shades on uncursed
// healers):
//   g++ -std=c++17 -Wall -Wextra tools/test_shade_cover_policy.cpp -o /tmp/test_shade_cover_policy && /tmp/test_shade_cover_policy
#include "../ai/playerbot/BossTacticsPolicy.h"

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
    // Under the cover the lowest-member line does not hold it (the low member is usually the eaten healer) ...
    in.lowestMemberPct = 20;
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
    std::cout << "shade cover policy: ok\n";
    return 0;
}
