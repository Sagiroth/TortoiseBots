// Standalone check for ai/playerbot/ThreatCapPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_threat_cap_policy.cpp -o /tmp/test_threat_cap_policy
//   /tmp/test_threat_cap_policy
#include "../ai/playerbot/ThreatCapPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

    // ThreatSuccessorNotHolder (AiPlayerbot.DungeonRunThreatSuccessorNotHolder): the reference is the highest tank the
    // boss is NOT hitting. Tanks are {threat, offTank, onBoss (hits the boss), holdsBoss (the boss hits it)}.
    // successor_is_main_tank_when_offtank_holds (fails without the change: the pick is the holder's 6000): MT 300 does
    // not hold, OT 6000 holds, key on -> 300 and not the off-tank.
    {
        const std::vector<ThreatCapTank> offTankHolds = {{300.0f, false, true, false}, {6000.0f, true, true, true}};
        CHECK(ThreatCapReference(offTankHolds, true, true) == 300.0f);
        CHECK(!ThreatCapPickReference(offTankHolds, true, true).offTank);
        // Key off: today's pick, the holding off-tank's own threat.
        CHECK(ThreatCapReference(offTankHolds, true, false) == 6000.0f);
        CHECK(ThreatCapPickReference(offTankHolds, true, false).offTank);
        // The key is only for the successor cap: without it the highest tank stands.
        CHECK(ThreatCapReference(offTankHolds, false, true) == 6000.0f);
        CHECK(ThreatCapPickReference(offTankHolds, true, true).holder == ThreatCapPick::Holder::Off);
        CHECK(ThreatCapPickReference(offTankHolds, true, false).holder == ThreatCapPick::Holder::Off);
    }
    // The unchanged case: the main tank holds, the off-tank (3000) is the successor, key on or off.
    {
        const std::vector<ThreatCapTank> mainHolds = {{6000.0f, false, true, true}, {3000.0f, true, true, false}};
        CHECK(ThreatCapReference(mainHolds, true, true) == 3000.0f);
        CHECK(ThreatCapPickReference(mainHolds, true, true).offTank);
        CHECK(ThreatCapReference(mainHolds, true, false) == 3000.0f);
        CHECK(ThreatCapPickReference(mainHolds, true, true).holder == ThreatCapPick::Holder::Main);
        CHECK(std::string(ThreatCapHolderWord(ThreatCapPick::Holder::Main)) == "main");
        CHECK(std::string(ThreatCapHolderWord(ThreatCapPick::Holder::Off)) == "off");
        CHECK(std::string(ThreatCapHolderWord(ThreatCapPick::Holder::None)) == "none");
    }
    // All non-holders under the floor: the floor, never 0. The holder 20000 puts the 1 % share at 200; a 50 main tank
    // and a 0 off-tank read 200. With a huge holder the share floor wins over the fixed 100.
    CHECK(ThreatCapReference({{50.0f, false, true, false}, {20000.0f, true, true, true}}, true, true) == 200.0f);
    CHECK(ThreatCapReference({{0.0f, false, true, false}, {20000.0f, true, true, true}}, true, true) == 200.0f);
    CHECK(ThreatCapReference({{0.0f, false, true, false}, {5000.0f, true, true, true}}, true, true) == 100.0f);
    CHECK(ThreatCapReference({{0.0f, false, true, false}, {2000000.0f, true, true, true}}, true, true) == 20000.0f);
    // Above the floor the non-holder's own threat stands.
    CHECK(ThreatCapReference({{250.0f, false, true, false}, {20000.0f, true, true, true}}, true, true) == 250.0f);
    // Two off-tanks and a main tank, the main tank holds: the higher off-tank.
    CHECK(ThreatCapReference({{9000.0f, false, true, true}, {400.0f, true, true, false}, {700.0f, true, true, false}},
                             true, true) == 700.0f);
    // Edge: every tank on the list holds the boss (only the holder is alive, in the world and not charmed): the
    // old pick stands, the holder's own threat. A dead off-tank, then a charmed main tank (Brain Wash), are left off
    // the list by the caller: the lone holder is the reference and nothing falls to 0.
    CHECK(ThreatCapReference({{6000.0f, false, true, true}}, true, true) == 6000.0f);
    CHECK(!ThreatCapPickReference({{6000.0f, false, true, true}}, true, true).offTank);
    CHECK(ThreatCapReference({{6000.0f, true, true, true}}, true, true) == 6000.0f);   // the off-tank is the lone holder
    CHECK(ThreatCapPickReference({{6000.0f, true, true, true}}, true, true).offTank);
    // A charmed main tank (left off the list): the off-tank not holding is the only reference, and a charmed tank is
    // never read as the reference even when its stale threat would have been the highest.
    CHECK(ThreatCapReference({{800.0f, true, true, false}}, true, true) == 800.0f);
    CHECK(ThreatCapPickReference({{800.0f, true, true, false}}, true, true).offTank);
    // The boss has no victim: nobody holds it, so every tank is a non-holder and the reference is the highest tank,
    // whatever the key. Today's pick differs only when an off-tank above 50 % of the main tank is on the boss; it then
    // picks that off-tank (MT 6000, OT 4000 -> 4000 today, 6000 with the key on).
    CHECK(ThreatCapReference({{6000.0f, false, false, false}, {1000.0f, true, false, false}}, true, true) == 6000.0f);
    CHECK(ThreatCapReference({{6000.0f, false, false, false}, {1000.0f, true, false, false}}, true, false) == 6000.0f);
    CHECK(ThreatCapReference({{6000.0f, false, false, false}, {4000.0f, true, false, false}}, true, false) == 4000.0f);
    CHECK(ThreatCapReference({{6000.0f, false, false, false}, {4000.0f, true, false, false}}, true, true) == 6000.0f);
    CHECK(!ThreatCapPickReference({{6000.0f, false, false, false}, {4000.0f, true, false, false}}, true, true).offTank);
    CHECK(ThreatCapPickReference({{6000.0f, false, false, false}, {4000.0f, true, false, false}}, true, true).holder ==
          ThreatCapPick::Holder::None);
    // A damage dealer holds the boss: no tank holds it, so the holder word reads none and the highest tank stands.
    CHECK(ThreatCapPickReference({{300.0f, false, true, false}, {200.0f, true, true, false}}, true, true).holder ==
          ThreatCapPick::Holder::None);
    // A single tank in the raid: it is the reference whether it holds or not (a holder only has itself).
    CHECK(ThreatCapReference({{1000.0f, false, true, false}}, true, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false, true, true}}, true, true) == 1000.0f);
    // No tanks at all: still -1, the caller's "no reference".
    CHECK(ThreatCapReference({}, true, true) == -1.0f);

    // ThreatHighLineIsEightyEverywhere: every target vetoes at 80 % of the reference tank, successor bosses included.
    CHECK(kThreatHighLinePct == 80);

    // ThreatPctSaturated: the threat values carry a uint8 percentage of the reference tank's threat. botThreat * 100 /
    // maxThreat above 255 used to wrap (300 % read as 44 %), and the cap then let the attack through exactly
    // where the margin matters most. 255 stays 255 and every larger ratio saturates at it.
    CHECK(ThreatPctSaturated(255.0f, 100.0f) == 255);
    CHECK(ThreatPctSaturated(256.0f, 100.0f) == 255);
    CHECK(ThreatPctSaturated(300.0f, 100.0f) == 255);
    CHECK(ThreatPctSaturated(600.0f, 100.0f) == 255);
    CHECK(ThreatPctSaturated(64.0f, 100.0f) == 64);
    CHECK(ThreatPctSaturated(0.0f, 100.0f) == 0);
    CHECK(ThreatPctSaturated(150.0f, 0.0f) == 0);  // no reference: the caller answers "no tank" before this
    CHECK(ThreatPctSaturated(-10.0f, 100.0f) == 0);
    // A saturated read still vetoes: the cap sees 255, not the wrapped 44.
    CHECK(ThreatPctSaturated(300.0f, 100.0f) >= kThreatHighLinePct);

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
