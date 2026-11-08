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

    // interrupt_exempt_behind_key: ThreatCapInterruptExempt is the call ThreatMultiplier::GetValue makes before its
    // high-threat veto (heals are exempt beside it). A pure interrupt (Kick, Pummel, Shield Bash, Bash, Counterspell,
    // Silence) is exempt only with AiPlayerbot.InterruptIgnoresThreat on: a rogue at the 80 % line stood idle at full
    // energy in 21 of 50 Kick windows (Tortoise Trials DPS-ROG-KICK 0.42). Key off is never exempt, at any threat.
    CHECK(ThreatCapInterruptExempt(true, true, 80.0f));
    CHECK(!ThreatCapInterruptExempt(true, false, 80.0f));
    CHECK(!ThreatCapInterruptExempt(true, false, 0.0f));
    CHECK(!ThreatCapInterruptExempt(false, true, 80.0f));
    CHECK(!ThreatCapInterruptExempt(false, false, 0.0f));

    // interrupt_ceiling_at_tank_threat: at 100 % of the tank's threat or more the interrupt's own threat (Counterspell
    // 300 flat, Shield Bash 180 plus damage) would pull the mob, so the veto stands again.
    CHECK(kThreatCapInterruptCeilingPct == 100.0f);
    CHECK(ThreatCapInterruptExempt(true, true, 99.9f));
    CHECK(!ThreatCapInterruptExempt(true, true, 100.0f));
    CHECK(!ThreatCapInterruptExempt(true, true, 300.0f));

    // ThreatCapPercentOf: unsaturated (a uint8 percentage wraps above 255); a tank with no threat reads 100 (the bot
    // is the one the mob turns to), no tank on the target (-1) reads 0 like the high-threat line.
    CHECK(ThreatCapPercentOf(800.0f, 1000.0f) == 80.0f);
    CHECK(ThreatCapPercentOf(1000.0f, 1000.0f) == 100.0f);
    CHECK(ThreatCapPercentOf(3000.0f, 1000.0f) == 300.0f);
    CHECK(ThreatCapPercentOf(50.0f, 0.0f) == 100.0f);
    CHECK(ThreatCapPercentOf(0.0f, 0.0f) == 100.0f);
    CHECK(ThreatCapPercentOf(50.0f, -1.0f) == 0.0f);
    CHECK(!ThreatCapInterruptExempt(true, true, ThreatCapPercentOf(50.0f, 0.0f)));

    // interrupt_uses_interrupt_target_ratio: the caster to kick is not always the current target. The bot sits at
    // 120 % on its current target (tank 1000, bot 1200) and at 50 % on the caster (tank 1000, bot 500): the caster's
    // ratio decides, so the Kick goes; the current target's ratio would have vetoed it.
    float const currentTargetPct = ThreatCapPercentOf(1200.0f, 1000.0f);
    float const interruptTargetPct = ThreatCapPercentOf(500.0f, 1000.0f);
    CHECK(!ThreatCapInterruptExempt(true, true, currentTargetPct));
    CHECK(ThreatCapInterruptExempt(true, true, interruptTargetPct));
    // And the other way round: low on the current target, over the tank on the caster: vetoed.
    CHECK(!ThreatCapInterruptExempt(true, true, ThreatCapPercentOf(1500.0f, 1000.0f)));

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
