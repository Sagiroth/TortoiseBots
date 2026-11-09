// Standalone check for ai/playerbot/ThreatCapPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_threat_cap_policy.cpp -o /tmp/test_threat_cap_policy
//   /tmp/test_threat_cap_policy
#include "../ai/playerbot/ThreatCapPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots threat cap policy tests...\n";

    // ThreatCapIsTheHighestTankByDefault: without a `threat_successor` row the reference is the highest tank, whatever
    // the target's taunt immunity. Tanks {1000, 600}: 1000, not 600.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{600.0f, false}, {1000.0f, true, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}, {400.0f, true, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({}, false) == -1.0f);
    CHECK(!ThreatCapPickReference({{1000.0f, false}, {600.0f, true, true}}, false).offTank);

    // ThreatCapsAtTheAssignedOffTank: on a target whose boss data asks for a successor (Hakkar) the damage dealers stay
    // below the assigned off-tank so a Cause Insanity threat reset hands the boss to it. Main 1000, off-tank 600: 600.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}}, true) == 600.0f);
    // An off-tank already above the main tank stands at its own threat: the cap is the highest off-tank, not always
    // the second number.
    CHECK(ThreatCapReference({{600.0f, false}, {1000.0f, true, true}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}, {400.0f, true, true}}, true) == 600.0f);
    // Two off-tanks, the higher of them stands.
    CHECK(ThreatCapReference({{1000.0f, false}, {400.0f, true, true}, {700.0f, true, true}}, true) == 700.0f);

    // TauntImmuneSuccessorIgnoresHalfFloor: the floor is absolute (1 % of the highest tank, never below 100 threat),
    // not half of the main tank's threat. Tanks at 1000 and 300 cap at the off-tank's 300 — the old 50 % floor sat the
    // cap at 1000 and Cause Insanity then deleted the main tank's threat, moving the boss to the top damage dealer.
    CHECK(kThreatCapOffTankMinShare == 0.01f);
    CHECK(kThreatCapOffTankMinThreat == 100.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {300.0f, true, true}}, true) == 300.0f);
    CHECK(ThreatCapPickReference({{1000.0f, false}, {300.0f, true, true}}, true).offTank);
    // Well above the floor: unchanged by the new floor.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}}, true) == 600.0f);

    // ThreatCapIgnoresATinyOffTankThreat: an off-tank below the absolute floor is not the successor — capping the
    // damage dealers there would sit the cap at almost nothing and stop all damage, so the highest tank's threat
    // stands instead. A 50 off-tank on a 1000 main tank is below 100; 101 is above it.
    CHECK(ThreatCapReference({{1000.0f, false}, {50.0f, true, true}}, true) == 1000.0f);
    CHECK(!ThreatCapPickReference({{1000.0f, false}, {50.0f, true, true}}, true).offTank);
    CHECK(ThreatCapReference({{1000.0f, false}, {101.0f, true, true}}, true) == 101.0f);
    // A charmed main tank whose threat is gone leaves the off-tank's 600 as the reference.
    CHECK(ThreatCapReference({{0.0f, false}, {600.0f, true, true}}, true) == 600.0f);
    // The 1 % share wins over the fixed 100 on a huge main-tank lead: 2 000 000 * 1 % = 20 000.
    CHECK(ThreatCapReference({{2000000.0f, false}, {15000.0f, true, true}}, true) == 2000000.0f);
    CHECK(ThreatCapReference({{2000000.0f, false}, {25000.0f, true, true}}, true) == 25000.0f);

    // ThreatCapWithoutAZeroReference: an off-tank with no threat on the target (dead, not yet engaged, or its threat
    // deleted) never becomes the reference: a zero reads as "the tank has no threat" and vetoes every damage action
    // (Hakkar's Cause Insanity zeroing the main tank was the bug). The highest tank stands instead.
    CHECK(ThreatCapReference({{1000.0f, false}, {0.0f, true, true}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {0.0f, true, true}, {0.0f, true, true}}, true) == 1000.0f);
    // The main tank is charmed and its threat gone; the off-tank still stands as the successor.
    CHECK(ThreatCapReference({{0.0f, false}, {600.0f, true, true}}, true) == 600.0f);

    // ThreatCapWithoutAnAssignedOffTank: a lone tank (or none of the assigned off-tanks on the target) leaves the
    // highest tank as the reference, never a zero.
    CHECK(ThreatCapReference({{1000.0f, false}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{900.0f, false}, {1000.0f, false}}, true) == 1000.0f);
    CHECK(ThreatCapReference({}, true) == -1.0f);

    // ThreatCapFallsBackWhileTheOffTanksAreAway: Hakkar's off-tanks are away on Sons (not on the boss), so their threat
    // on the boss is the leftover of an earlier hold. The 1 % reference would freeze every damage dealer at 2000 while
    // the main tank holds 40000: while no off-tank has the boss as its victim the reference is the main tank's again
    // (the old 50 % floor). With an off-tank on the boss the successor rule stands.
    CHECK(kThreatCapOffTankAwayShare == 0.5f);
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, false}}, true) == 40000.0f);
    CHECK(!ThreatCapPickReference({{40000.0f, false}, {2000.0f, true, false}}, true).offTank);
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, true}}, true) == 2000.0f);
    CHECK(ThreatCapPickReference({{40000.0f, false}, {2000.0f, true, true}}, true).offTank);
    // Away but still above the old 50 % floor: that off-tank is the cap, as before the 1 % rule.
    CHECK(ThreatCapReference({{40000.0f, false}, {25000.0f, true, false}}, true) == 25000.0f);
    // One of two off-tanks on the boss: the successor rule over both.
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, true}, {900.0f, true, false}}, true) == 2000.0f);
    // The cap is only for the successor target: the plain max ignores the flag.
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, false}}, false) == 40000.0f);

    // ThreatCapInterruptExempt(interrupt, key, botThreat, tankThreat, flatThreat, ranged) is the call
    // ThreatMultiplier::GetValue makes before its high-threat veto (heals are exempt beside it). Tank 1000 throughout.

    // interrupt_exempt_behind_key: key off is never exempt, at any threat; a non-interrupt never is either.
    CHECK(ThreatCapInterruptExempt(true, true, 800.0f, 1000.0f, 0.0f, false));
    CHECK(!ThreatCapInterruptExempt(true, false, 800.0f, 1000.0f, 0.0f, false));
    CHECK(!ThreatCapInterruptExempt(true, false, 0.0f, 1000.0f, 0.0f, true));
    CHECK(!ThreatCapInterruptExempt(false, true, 800.0f, 1000.0f, 0.0f, false));
    CHECK(!ThreatCapInterruptExempt(false, false, 0.0f, 1000.0f, 0.0f, false));

    // The core's pull line (ThreatManager::getHostileTarget): 110 % in melee reach, 130 % from range.
    CHECK(kAggroPullMeleeFactor == 1.1f);
    CHECK(kAggroPullRangedFactor == 1.3f);
    CHECK(AggroPullFactor(false) == 1.1f);
    CHECK(AggroPullFactor(true) == 1.3f);

    // kick_at_105_exempt: the failing case under round 1's 100 % ceiling. A rogue at 105 % of the tank in melee with
    // Kick (no flat threat) does not pull the mob, so it kicks (live AFTER round 1: 3594 `MULT threat x0.000` on the
    // rogue against 42 OK, Kick 0.469).
    CHECK(ThreatCapInterruptExempt(true, true, 1050.0f, 1000.0f, 0.0f, false));
    // kick_at_111_melee_vetoed: over the 110 % melee line the veto stands.
    CHECK(!ThreatCapInterruptExempt(true, true, 1110.0f, 1000.0f, 0.0f, false));
    // The same 111 % from range is under the 130 % line.
    CHECK(ThreatCapInterruptExempt(true, true, 1110.0f, 1000.0f, 0.0f, true));
    // At the line itself it is vetoed (strictly under): 1300 ranged.
    CHECK(!ThreatCapInterruptExempt(true, true, 1300.0f, 1000.0f, 0.0f, true));

    // counterspell_flat_threat_counts: Counterspell carries 300 flat threat (tw_world_spell_threat 2139). A mage at
    // 100 % of the tank plus 300 is 130 %, at the ranged line: vetoed. At 80 % plus 300 = 110 % it is under: exempt.
    CHECK(!ThreatCapInterruptExempt(true, true, 1000.0f, 1000.0f, 300.0f, true));
    CHECK(ThreatCapInterruptExempt(true, true, 800.0f, 1000.0f, 300.0f, true));
    // Without the flat threat the 100 % mage would read exempt: the flat threat is what vetoes it.
    CHECK(ThreatCapInterruptExempt(true, true, 1000.0f, 1000.0f, 0.0f, true));
    // Shield Bash (180 flat) in melee at 95 %: 1130 is over 1100, vetoed; at 90 % (1080) exempt.
    CHECK(!ThreatCapInterruptExempt(true, true, 950.0f, 1000.0f, 180.0f, false));
    CHECK(ThreatCapInterruptExempt(true, true, 900.0f, 1000.0f, 180.0f, false));

    // interrupt_no_tank_keeps_the_veto: with no tank threat on the mob (0, or GetTankThreat's -1 for no tank on it)
    // there is no line to stay under; the back-off answers as today.
    CHECK(!ThreatCapInterruptExempt(true, true, 0.0f, 0.0f, 0.0f, false));
    CHECK(!ThreatCapInterruptExempt(true, true, 50.0f, 0.0f, 0.0f, true));
    CHECK(!ThreatCapInterruptExempt(true, true, 0.0f, -1.0f, 0.0f, false));

    // interrupt_uses_interrupt_target_numbers: the caster to kick is not always the current target. The bot sits at
    // 120 % on its current target and at 50 % on the caster: the caster's numbers decide, so the Kick goes.
    CHECK(!ThreatCapInterruptExempt(true, true, 1200.0f, 1000.0f, 0.0f, false));
    CHECK(ThreatCapInterruptExempt(true, true, 500.0f, 1000.0f, 0.0f, false));

    // ThreatCapPercentOf (the veto row's pct=): unsaturated; a tank with no threat reads 100, no tank on the target
    // (-1) reads 0 like the high-threat line.
    CHECK(ThreatCapPercentOf(800.0f, 1000.0f) == 80.0f);
    CHECK(ThreatCapPercentOf(3000.0f, 1000.0f) == 300.0f);
    CHECK(ThreatCapPercentOf(50.0f, 0.0f) == 100.0f);
    CHECK(ThreatCapPercentOf(50.0f, -1.0f) == 0.0f);

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
