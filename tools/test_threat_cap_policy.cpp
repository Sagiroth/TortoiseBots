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
    // ThreatCapFallsBackWhileTheOffTanksAreAway holds with the key ON too (the holdsBoss flag is not set in these
    // inputs: nobody is read as holding, the main tank included, so only the onBoss gate decides).
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, false}}, true, true) == 40000.0f);
    CHECK(!ThreatCapPickReference({{40000.0f, false}, {2000.0f, true, false}}, true, true).offTank);
    CHECK(ThreatCapReference({{40000.0f, false}, {25000.0f, true, false}}, true, true) == 25000.0f);
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, true}, {900.0f, true, false}}, true, true) == 2000.0f);

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
    // The reference is the non-holder's REAL threat, 0 included (no synthetic floor): a non-holder at 50 behind a 20000
    // holder reads 50, not 200; a wiped one reads 0 and ThreatPctOf holds the damage dealers on it.
    CHECK(ThreatCapReference({{50.0f, false, true, false}, {20000.0f, true, true, true}}, true, true) == 50.0f);
    CHECK(ThreatCapReference({{0.0f, false, true, false}, {20000.0f, true, true, true}}, true, true) == 0.0f);
    CHECK(ThreatCapPickReference({{0.0f, false, true, false}, {20000.0f, true, true, true}}, true, true).threat == 0.0f);
    CHECK(ThreatCapReference({{250.0f, false, true, false}, {20000.0f, true, true, true}}, true, true) == 250.0f);
    // reference_zero_holds_the_damage_dealers: ThreatPctOf reads a 0 reference on an engaged target as 100 (vetoed at
    // kThreatHighLinePct), never as no cap; a negative reference (no tank) is the only "no cap"; a tiny reference
    // saturates; out of combat or fleeing keep their old answers. Key off reaches it with the same values as before.
    CHECK(ThreatPctOf(5000.0f, 0.0f, true, false) == 100);
    CHECK(ThreatPctOf(5000.0f, 0.0f, true, false) >= kThreatHighLinePct);
    CHECK(ThreatPctOf(0.0f, 0.0f, true, false) >= kThreatHighLinePct);
    CHECK(ThreatPctOf(5000.0f, -1.0f, true, false) == 0);
    CHECK(ThreatPctOf(5000.0f, 0.0f, false, false) == 0);   // not in combat: no veto, as before
    CHECK(ThreatPctOf(5000.0f, 0.0f, true, true) == 0);     // fleeing target with a 0 reference: unchanged path
    CHECK(ThreatPctOf(5000.0f, 40.0f, true, false) == 255); // a tiny reference saturates, vetoed
    CHECK(ThreatPctOf(5000.0f, 40.0f, true, true) == 0);    // fleeing target: unchanged
    CHECK(ThreatPctOf(500.0f, 1000.0f, true, false) == 50);
    CHECK(ThreatPctOf(500.0f, 1000.0f, true, false) == ThreatPctSaturated(500.0f, 1000.0f));
    // hakkar_away_key_on_equals_today (fails with the first version of the key: it gave 2000): the main tank holds with
    // 40000 and the off-tanks are on Sons (not on the boss) with 2000 left over: with the key on the pick is today's.
    {
        const std::vector<ThreatCapTank> away = {{40000.0f, false, true, true}, {2000.0f, true, false, false}};
        CHECK(ThreatCapReference(away, true, true) == 40000.0f);
        CHECK(!ThreatCapPickReference(away, true, true).offTank);
        CHECK(ThreatCapReference(away, true, true) == ThreatCapReference(away, true, false));
        // Away but above the old 50 % share: today's pick, the off-tank, with the key on too.
        const std::vector<ThreatCapTank> awayHigh = {{40000.0f, false, true, true}, {25000.0f, true, false, false}};
        CHECK(ThreatCapReference(awayHigh, true, true) == 25000.0f);
        CHECK(ThreatCapReference(awayHigh, true, true) == ThreatCapReference(awayHigh, true, false));
    }
    // non_holder_not_on_boss_falls_back: the off-tank holds the boss, the main tank is off it (held on a Shade, in the
    // pit): the main tank is no successor, the pick is today's. A non-holder on the boss next to it still counts.
    CHECK(ThreatCapReference({{300.0f, false, false, false}, {6000.0f, true, true, true}}, true, true) == 6000.0f);
    CHECK(ThreatCapReference({{300.0f, false, false, false}, {6000.0f, true, true, true}}, true, true) ==
          ThreatCapReference({{300.0f, false, false, false}, {6000.0f, true, true, true}}, true, false));
    CHECK(ThreatCapReference({{9000.0f, false, false, false}, {400.0f, true, true, false}, {6000.0f, true, true, true}},
                             true, true) == 400.0f);
    // The wrong-way read the first version had: a higher non-holder that is not on the boss must not be the reference.
    CHECK(ThreatCapReference({{9000.0f, false, false, false}, {400.0f, true, true, false}, {6000.0f, true, true, true}},
                             true, true) != 9000.0f);
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
    // The boss has no victim (or a damage dealer holds it) and the tanks hit it: nobody holds it, so every tank on the
    // boss is a non-holder and the reference is the highest tank (MT 6000, OT 4000 -> 4000 today, 6000 with the key on).
    CHECK(ThreatCapReference({{6000.0f, false, true, false}, {1000.0f, true, true, false}}, true, true) == 6000.0f);
    CHECK(ThreatCapReference({{6000.0f, false, true, false}, {1000.0f, true, true, false}}, true, false) == 1000.0f);
    CHECK(ThreatCapReference({{6000.0f, false, true, false}, {4000.0f, true, true, false}}, true, false) == 4000.0f);
    CHECK(ThreatCapReference({{6000.0f, false, true, false}, {4000.0f, true, true, false}}, true, true) == 6000.0f);
    CHECK(!ThreatCapPickReference({{6000.0f, false, true, false}, {4000.0f, true, true, false}}, true, true).offTank);
    CHECK(ThreatCapPickReference({{6000.0f, false, true, false}, {4000.0f, true, true, false}}, true, true).holder ==
          ThreatCapPick::Holder::None);
    // Nobody holds it and nobody hits it either (all tanks away): today's pick, the key changes nothing.
    CHECK(ThreatCapReference({{6000.0f, false, false, false}, {4000.0f, true, false, false}}, true, true) == 4000.0f);
    CHECK(ThreatCapReference({{6000.0f, false, false, false}, {4000.0f, true, false, false}}, true, true) ==
          ThreatCapReference({{6000.0f, false, false, false}, {4000.0f, true, false, false}}, true, false));
    // A damage dealer holds the boss: no tank holds it, so the holder word reads none and the highest tank stands.
    CHECK(ThreatCapPickReference({{300.0f, false, true, false}, {200.0f, true, true, false}}, true, true).holder ==
          ThreatCapPick::Holder::None);
    // A single tank in the raid: it is the reference whether it holds or not (a holder only has itself).
    CHECK(ThreatCapReference({{1000.0f, false, true, false}}, true, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false, true, true}}, true, true) == 1000.0f);
    // No tanks at all: still -1, the caller's "no reference".
    CHECK(ThreatCapReference({}, true, true) == -1.0f);

    // HexSnapshot (BossTacticsHex row): who wore Jin'do's Hex, and one row per Hex.
    CHECK(kJindoHexSpellId == 17172);
    CHECK(HexHolderOf(true, 0) == HexHolder::Main);
    CHECK(HexHolderOf(true, 1) == HexHolder::Off);
    CHECK(HexHolderOf(true, 2) == HexHolder::Off);
    CHECK(HexHolderOf(true, -1) == HexHolder::Dps);
    CHECK(HexHolderOf(false, 0) == HexHolder::None);
    CHECK(std::string(HexHolderWord(HexHolder::Main)) == "main");
    CHECK(std::string(HexHolderWord(HexHolder::Off)) == "off");
    CHECK(std::string(HexHolderWord(HexHolder::Dps)) == "dps");
    CHECK(std::string(HexHolderWord(HexHolder::None)) == "none");
    // One row per Hex: the first logs, a re-read 5 s later (the Hex still up) does not, the next Hex 20 s on does.
    CHECK(HexSnapshotDue(0, 1000));
    CHECK(!HexSnapshotDue(1000, 1000 + 5000));
    CHECK(!HexSnapshotDue(1000, 1000 + kHexSnapshotGapMs - 1));
    CHECK(HexSnapshotDue(1000, 1000 + kHexSnapshotGapMs));
    CHECK(HexSnapshotDue(1000, 1000 + 20000));
    CHECK(HexSnapshotDue(0xFFFFFF00u, 0x00004000u)); // clock wrap

    // hexed_main_tank_stays_the_reference (fails when the Hex read is dropped): the off-tank holds Jin'do, the main tank
    // wears the Hex (polymorphed, not hitting) with its threat wiped to 300. It still counts as on the boss, so the
    // reference is the main tank's 300, not the new holder's 6000 the damage dealers could climb to 80 % of.
    CHECK(ThreatCapOnBoss(true, false));
    CHECK(ThreatCapOnBoss(false, true));
    CHECK(!ThreatCapOnBoss(false, false));
    {
        const std::vector<ThreatCapTank> hexedMain = {{300.0f, false, ThreatCapOnBoss(false, true), false},
                                                       {6000.0f, true, ThreatCapOnBoss(true, false), true}};
        CHECK(ThreatCapReference(hexedMain, true, true) == 300.0f);
        CHECK(!ThreatCapPickReference(hexedMain, true, true).offTank);
        // The same tank neither hitting nor Hexed (held on a Shade): not on the boss, today's pick.
        const std::vector<ThreatCapTank> awayMain = {{300.0f, false, ThreatCapOnBoss(false, false), false},
                                                      {6000.0f, true, ThreatCapOnBoss(true, false), true}};
        CHECK(ThreatCapReference(awayMain, true, true) == 6000.0f);
    }
    // hexed_dps_that_is_no_longer_the_victim_still_logs (fails with the victim-only probe, which finds no wearer): the
    // scan hands the helper every member that carries the Hex; a DPS (tank index -1) with no earlier row is picked and
    // reads holder=dps whatever the boss's victim is now (the helper has no victim input at all).
    {
        std::vector<HexMember> wearers(1);
        wearers[0].key = 77;
        wearers[0].tankIndex = -1;
        CHECK(HexWearerToLog(wearers, 50000) == 0);
        CHECK(std::string(HexHolderWord(HexHolderOf(true, wearers[0].tankIndex))) == "dps");
        // Its row was written 3 s ago: one row per Hex, none now; a second wearer without a row is picked instead.
        wearers[0].lastLoggedMs = 47000;
        CHECK(HexWearerToLog(wearers, 50000) == -1);
        wearers.resize(2);
        wearers[1].key = 78;
        wearers[1].tankIndex = 2;
        CHECK(HexWearerToLog(wearers, 50000) == 1);
        CHECK(HexWearerToLog({}, 50000) == -1);
        // The next Hex, 20 s on, logs again.
        CHECK(HexWearerToLog(wearers, 67000) == 0);
    }
    CHECK(HexScanDue(0, 1000));
    CHECK(!HexScanDue(1000, 1999));
    CHECK(HexScanDue(1000, 1000 + kHexScanGapMs));
    CHECK(HexScanDue(0xFFFFFF00u, 0x00000400u)); // clock wrap
    // hex_on_second_offtank_names_that_tank: ot_threat shows the wearer when an off-tank wore it, else the first off-tank.
    CHECK(HexOffTankIndex(2) == 2);
    CHECK(HexOffTankIndex(1) == 1);
    CHECK(HexOffTankIndex(0) == 1);
    CHECK(HexOffTankIndex(-1) == 1);

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

    // cure_not_vetoed_at_high_threat (AiPlayerbot.DungeonRunCureIgnoresThreat, fails without the cure exemption):
    // ThreatCapExempt is the call ThreatMultiplier::GetValue makes before its high-threat veto (it returns 1.0 when this
    // is true). A healer at 100 % of the reference casts `dispel magic on party` on the Hexed tank: key on, exempt (the
    // cure runs); key off, not exempt (vetoed as today). Heals are exempt either way; a nuke never is.
    CHECK(ThreatPctOf(10000.0f, 10000.0f, true, false) >= kThreatHighLinePct);
    CHECK(ThreatCapExempt(false, true, true));
    CHECK(!ThreatCapExempt(false, true, false));
    CHECK(ThreatCapExempt(true, false, false));
    CHECK(ThreatCapExempt(true, false, true));
    CHECK(!ThreatCapExempt(false, false, true));
    CHECK(!ThreatCapExempt(false, false, false));

    // successor_no_projection_escape (AiPlayerbot.DungeonRunThreatSuccessorStrict, fails while the escape stays): a damage
    // dealer at 85 % (8500 of 10000) rising 50/s projects to 8750 in 5 s, under the tank: today it escapes the veto. On a
    // successor target with the key on it does not.
    CHECK(ThreatPctOf(8500.0f, 10000.0f, true, false) == 85);
    CHECK(!HighThreatProjectionEscapes(true, true, 8500.0f, 50.0f, 10000.0f));
    // The lead's case: 90 % (9000), passing the tank in 6 s (167/s): 9835 in 5 s, today's escape; strict: vetoed.
    CHECK(HighThreatProjectionEscapes(true, false, 9000.0f, 1000.0f / 6.0f, 10000.0f));
    CHECK(!HighThreatProjectionEscapes(true, true, 9000.0f, 1000.0f / 6.0f, 10000.0f));
    // Key off, or a target without the row: today's escape exactly.
    CHECK(HighThreatProjectionEscapes(true, false, 8500.0f, 50.0f, 10000.0f));
    CHECK(HighThreatProjectionEscapes(false, true, 8500.0f, 50.0f, 10000.0f));
    CHECK(HighThreatProjectionEscapes(false, false, 8500.0f, 50.0f, 10000.0f));
    // Projecting past the tank, or not rising: no escape anywhere (today's rule).
    CHECK(!HighThreatProjectionEscapes(false, false, 9000.0f, 300.0f, 10000.0f));
    CHECK(!HighThreatProjectionEscapes(false, false, 8500.0f, 0.0f, 10000.0f));
    CHECK(!HighThreatProjectionEscapes(false, false, 8500.0f, -20.0f, 10000.0f));
    CHECK(kHighThreatProjectionSeconds == 5.0f);

    // successor_white_stops_at_92_resumes_under_85: the white-damage latch on a successor target.
    CHECK(kSuccessorWhiteStopPct == 92);
    CHECK(kSuccessorWhiteResumePct == 85);
    {
        SuccessorWhiteLatch latch;
        CHECK(!latch.Update(true, false, 91));   // running
        CHECK(latch.Update(true, false, 92));    // stopped
        CHECK(latch.Update(true, false, 88));    // still stopped inside the band
        CHECK(latch.Update(true, false, 85));    // 85 is still in the band
        CHECK(!latch.Update(true, false, 84));   // running again
        CHECK(!latch.Update(true, false, 88));   // running: 88 does not stop a running bot
        CHECK(latch.Update(true, false, 255));   // a saturated read stops
        // A tank is never stopped, and its read clears the latch.
        CHECK(!latch.Update(true, true, 200));
        CHECK(!latch.stopped);
        // A non-successor target clears the latch: the next successor read starts from the stop line again.
        CHECK(latch.Update(true, false, 95));
        CHECK(!latch.Update(false, false, 95));
        CHECK(!latch.Update(true, false, 90));
        SuccessorWhiteLatch tank;
        CHECK(!tank.Update(true, true, 255));
    }

    // successor_white_free_at_zero_reference (Strict): a tank Hex leaves the successor reference at 0, which reads 100 %
    // for every damage dealer. The white damage keeps running (the specials stay held by the 0-reference veto), and a
    // stopped latch clears, so the Hex window costs no white damage.
    {
        SuccessorWhiteLatch latch;
        CHECK(!latch.Update(true, false, ThreatPctOf(500.0f, 0.0f, true, false), true));
        CHECK(latch.Update(true, false, 95, false));
        CHECK(!latch.Update(true, false, 100, true));
        CHECK(!latch.stopped);
        CHECK(!latch.Update(true, false, 90, false)); // back from the Hex: starts from the 92 % stop line again
    }

    // successor_white_stop_wins_the_cycle (Strict): the stop must take effect in the cycle the latch flips, even when a
    // higher-relevance action is runnable. A small model of the engine's cycle: the trigger pass runs first, then the
    // highest queued action runs (one per cycle). The old design queued "stop auto attack" at ACTION_HIGH (20); with a
    // heal at ACTION_EMERGENCY (90) runnable, the heal ran and the swing went on past 92 %. The trigger now stops the
    // swing itself through SuccessorWhiteStopNow in the trigger pass, so it is stopped whatever runs next.
    {
        struct Cycle
        {
            bool whiteRunning = true;
            std::string ran;
            void Run(SuccessorWhiteLatch& latch, std::uint8_t pct, bool queueStopInstead)
            {
                std::vector<std::pair<std::string, float>> queue = {{"emergency heal", 90.0f}};
                if (queueStopInstead)
                {
                    if (latch.Update(true, false, pct, false) && whiteRunning)
                        queue.push_back({"stop auto attack", 20.0f});
                }
                else if (SuccessorWhiteStopNow(latch, true, false, pct, false, whiteRunning))
                    whiteRunning = false;
                std::pair<std::string, float> best = queue[0];
                for (const auto& entry : queue)
                    if (entry.second > best.second)
                        best = entry;
                ran = best.first;
                if (ran == "stop auto attack")
                    whiteRunning = false;
            }
        };
        SuccessorWhiteLatch oldLatch;
        Cycle oldCycle;
        oldCycle.Run(oldLatch, 93, true);
        CHECK(oldCycle.ran == "emergency heal");
        CHECK(oldCycle.whiteRunning); // the old queued stop lost the cycle

        SuccessorWhiteLatch latch;
        Cycle cycle;
        cycle.Run(latch, 91, false);
        CHECK(cycle.whiteRunning);    // under the line: nothing stopped
        cycle.Run(latch, 93, false);
        CHECK(cycle.ran == "emergency heal");
        CHECK(!cycle.whiteRunning);   // stopped in the same cycle the heal ran
        // Restarted by another attack action while still latched: stopped again on the next pass.
        cycle.whiteRunning = true;
        cycle.Run(latch, 88, false);
        CHECK(!cycle.whiteRunning);
        // Nothing to stop: no stop call, the latch still holds.
        CHECK(!SuccessorWhiteStopNow(latch, true, false, 88, false, false));
        CHECK(latch.stopped);
        // Tanks and a 0 reference never stop.
        SuccessorWhiteLatch tank;
        CHECK(!SuccessorWhiteStopNow(tank, true, true, 200, false, true));
        SuccessorWhiteLatch hexed;
        CHECK(!SuccessorWhiteStopNow(hexed, true, false, 100, true, true));
    }

    // cursed_main_tank_is_successor (AiPlayerbot.DungeonRunThreatSuccessorKeepsAway, fails without the Delusions mark):
    // the main tank wears Delusions and is on its Shade (6000 Jin'do threat, not hitting him); the off-tank holds at
    // 9000. Key on (the caller marks `cursed` and runs ThreatCapCursedOnBoss): the cursed main tank is on the boss and the
    // reference is its 6000. Key off (no mark, no call): today's fallback to the holder's 9000.
    CHECK(kJindoDelusionsSpellId == 24306);
    {
        ThreatCapTank cursedMain{6000.0f, false, ThreatCapOnBoss(false, false), false};
        cursedMain.cursed = true;
        std::vector<ThreatCapTank> keyOn = {cursedMain, {9000.0f, true, ThreatCapOnBoss(true, false), true}};
        ThreatCapCursedOnBoss(keyOn);
        CHECK(keyOn[0].onBoss);
        CHECK(ThreatCapReference(keyOn, true, true) == 6000.0f);
        CHECK(!ThreatCapPickReference(keyOn, true, true).offTank);
        const std::vector<ThreatCapTank> keyOff = {{6000.0f, false, ThreatCapOnBoss(false, false), false},
                                                   {9000.0f, true, ThreatCapOnBoss(true, false), true}};
        CHECK(ThreatCapReference(keyOff, true, true) == 9000.0f);
    }
    // cursed_tank_without_threat_is_not_successor: a cursed off-tank at 0 (or under half the highest tank) stays off the
    // boss, so the main tank holding at 9000 stays the reference instead of a 0 that would hold every damage dealer for
    // the whole curse. At exactly half it counts.
    CHECK(!ThreatCapCursedCountsOnBoss(0.0f, 9000.0f));
    CHECK(!ThreatCapCursedCountsOnBoss(4499.0f, 9000.0f));
    CHECK(ThreatCapCursedCountsOnBoss(4500.0f, 9000.0f));
    CHECK(!ThreatCapCursedCountsOnBoss(0.0f, 0.0f));
    {
        for (float otThreat : {0.0f, 2000.0f})
        {
            ThreatCapTank cursedOff{otThreat, true, ThreatCapOnBoss(false, false), false};
            cursedOff.cursed = true;
            std::vector<ThreatCapTank> tanks = {{9000.0f, false, ThreatCapOnBoss(true, false), true}, cursedOff};
            ThreatCapCursedOnBoss(tanks);
            CHECK(!tanks[1].onBoss);
            CHECK(ThreatCapReference(tanks, true, true) == 9000.0f);
        }
        // An uncursed tank off the boss is never lifted by the call.
        std::vector<ThreatCapTank> plain = {{9000.0f, false, true, true}, {6000.0f, true, false, false}};
        ThreatCapCursedOnBoss(plain);
        CHECK(!plain[1].onBoss);
    }

    // successor_white_leaves_a_swing_on_an_add (Strict): the latch holds on Jin'do, but an action moved the bot's swing
    // onto a Shade or an add: that swing is not white damage on the successor and is not stopped. On the boss it is.
    {
        CHECK(!SuccessorWhiteOnTarget(true, false, false, false));  // swinging at an add
        CHECK(!SuccessorWhiteOnTarget(false, false, true, false));  // shooting an add
        CHECK(!SuccessorWhiteOnTarget(false, true, false, false));  // victim set, not swinging
        CHECK(SuccessorWhiteOnTarget(true, true, false, false));
        CHECK(SuccessorWhiteOnTarget(false, false, true, true));
        SuccessorWhiteLatch latch;
        CHECK(!SuccessorWhiteStopNow(latch, true, false, 95, false, SuccessorWhiteOnTarget(true, false, false, false)));
        CHECK(latch.stopped);                                        // the latch still holds for the boss
        CHECK(SuccessorWhiteStopNow(latch, true, false, 95, false, SuccessorWhiteOnTarget(true, true, false, false)));
    }

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
