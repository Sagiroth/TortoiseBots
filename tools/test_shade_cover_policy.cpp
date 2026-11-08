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

// Step 1: the successor rule names the off-tank only.
static void TestCoverRowThreatGateIsTheOffTanks()
{
    CHECK(ShadeCoverThreatGate(CarrierStrikeRole::OffTank, true));
    CHECK(!ShadeCoverThreatGate(CarrierStrikeRole::OffTank, false));
    CHECK(!ShadeCoverThreatGate(CarrierStrikeRole::Dps, true));
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
    std::cout << "shade cover policy: ok\n";
    return 0;
}
