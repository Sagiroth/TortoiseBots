#include "../ai/playerbot/WarriorGapCloserPolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
    // A warrior that knows both gap closers, both ready, 15 yd from an
    // engaged mob in sight, out of combat.
    WarriorGapCloserFacts Base()
    {
        WarriorGapCloserFacts f;
        f.enabled = true;
        f.warriorInCombat = false;
        f.isTank = true;
        f.rescue = false;
        f.combatDistance = 15.0f;
        f.lineOfSight = true;
        f.pullAllowed = true;
        f.chargeKnown = true;
        f.chargeReady = true;
        f.interceptKnown = true;
        f.interceptReady = true;
        f.inBerserkerStance = false;
        f.rage = 0;
        f.tmRank = 5;
        return f;
    }
}

int main()
{
    std::cout << "Starting TortoiseBots warrior gap closer policy tests...\n";

    // Out of combat: Charge, from any stance (the Battle Stance swap is the
    // prerequisite), at 0 rage, tank or dps.
    {
        WarriorGapCloserFacts f = Base();
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Charge);
        f.isTank = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Charge);
        f.inBerserkerStance = true;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Charge);
        f.chargeReady = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f.chargeReady = true;
        f.chargeKnown = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f = Base();
        f.enabled = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
    }
    std::cout << "  [PASS] out of combat the gap closer is Charge\n";

    // Min and max range (core edge distance): never under 8 yd, never past 25.
    {
        WarriorGapCloserFacts f = Base();
        f.combatDistance = 7.9f;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f.combatDistance = 3.0f;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f.combatDistance = 8.0f;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Charge);
        f.combatDistance = 25.0f;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Charge);
        f.combatDistance = 25.1f;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        CHECK(!WarriorGapCloserInRange(7.99f));
        CHECK(WarriorGapCloserInRange(8.0f));
        CHECK(WarriorGapCloserInRange(25.0f));
        CHECK(!WarriorGapCloserInRange(30.0f));

        // Rescue in combat obeys the same window.
        f = Base();
        f.warriorInCombat = true;
        f.rescue = true;
        f.rage = 30;
        f.combatDistance = 6.0f;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f.combatDistance = 12.0f;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Intercept);
    }
    std::cout << "  [PASS] 8-25 yd window respected\n";

    // In combat: Charge is never chosen; a tank Intercepts only a mob on a
    // non-tank member and only when the post-swap bar pays 10 rage.
    {
        WarriorGapCloserFacts f = Base();
        f.warriorInCombat = true;
        f.rage = 50;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None); // own target: walk
        f.rescue = true;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Intercept);

        // Tactical Mastery 5/5 keeps 25: enough. 1/5 keeps 5: not enough.
        f.tmRank = 1;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f.tmRank = 2;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Intercept);
        // No Tactical Mastery: the swap empties the bar.
        f.tmRank = 0;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        // Already in Berserker Stance the whole bar counts.
        f.inBerserkerStance = true;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::Intercept);
        f.rage = 9;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        CHECK(!WarriorCanAffordIntercept(false, 100, 0));
        CHECK(WarriorCanAffordIntercept(false, 100, 2));
        CHECK(!WarriorCanAffordIntercept(false, 9, 5));
        CHECK(WarriorCanAffordIntercept(true, 10, 0));

        f = Base();
        f.warriorInCombat = true;
        f.rescue = true;
        f.rage = 30;
        f.interceptReady = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f.interceptReady = true;
        f.interceptKnown = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);

        // A dps warrior in combat walks.
        f = Base();
        f.warriorInCombat = true;
        f.isTank = false;
        f.rescue = true;
        f.rage = 50;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
    }
    std::cout << "  [PASS] in combat only a tank rescue Intercepts, rage after the swap\n";

    // A crowd-controlled target (the Sapped mob) or a held CC mark is never a
    // gap-closer target, in or out of combat.
    {
        WarriorGapCloserFacts f = Base();
        f.targetCrowdControlled = true;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f = Base();
        f.targetHeldCcMark = true;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f = Base();
        f.warriorInCombat = true;
        f.rescue = true;
        f.rage = 50;
        f.targetCrowdControlled = true;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
    }
    std::cout << "  [PASS] a CC'd target is never charged\n";

    // Line of sight and the pull rule.
    {
        WarriorGapCloserFacts f = Base();
        f.lineOfSight = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);
        f = Base();
        f.pullAllowed = false;
        CHECK(ChooseWarriorGapCloser(f) == WarriorGapCloser::None);

        // An engaged mob is always allowed; an unengaged one in an instance
        // only on an ordered melee pull of that mob; open world always.
        CHECK(WarriorGapCloserPullAllowed(true, true, false));
        CHECK(!WarriorGapCloserPullAllowed(false, true, false));
        CHECK(WarriorGapCloserPullAllowed(false, true, true));
        CHECK(WarriorGapCloserPullAllowed(false, false, false));
    }
    std::cout << "  [PASS] no charge into an unpulled pack\n";

    // Rogue-Sap pull: the tank (out of combat) picks the mob on the rogue
    // over its own target, and skips the Sapped one.
    {
        WarriorGapCloserFacts sapped = Base();
        sapped.rescue = false;
        sapped.targetCrowdControlled = true;
        sapped.pullAllowed = false; // a sapped mob is not in combat
        WarriorGapCloserFacts onRogue = Base();
        onRogue.rescue = true;
        onRogue.combatDistance = 18.0f;
        std::vector<WarriorGapCloserFacts> c = { onRogue, sapped };
        CHECK(PickWarriorGapCloserTarget(c) == 0);
        c = { sapped };
        CHECK(PickWarriorGapCloserTarget(c) == 1);

        // Healer rescue in combat: the mob on the healer is the candidate the
        // caller lists first; out of range it falls to the next one.
        WarriorGapCloserFacts onHealer = Base();
        onHealer.warriorInCombat = true;
        onHealer.rescue = true;
        onHealer.rage = 40;
        onHealer.combatDistance = 20.0f;
        WarriorGapCloserFacts ownTarget = Base();
        ownTarget.warriorInCombat = true;
        ownTarget.rage = 40;
        c = { onHealer, ownTarget };
        CHECK(PickWarriorGapCloserTarget(c) == 0);
        CHECK(ChooseWarriorGapCloser(c[0]) == WarriorGapCloser::Intercept);
        c[0].combatDistance = 30.0f;
        CHECK(PickWarriorGapCloserTarget(c) == 2); // nothing: run in
        std::vector<WarriorGapCloserFacts> none;
        CHECK(PickWarriorGapCloserTarget(none) == 0);
    }
    std::cout << "  [PASS] rescue picks the mob on the member, never the Sapped one\n";


    std::cout << "All warrior gap closer policy checks PASSED!\n";
    return 0;
}
