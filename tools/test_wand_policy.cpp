// Standalone regression test for "a caster with mana casts instead of
// wanding" (mage, priest, warlock): stop a running wand while a wanted
// nuke or heal is held back by the GCD each Turtle wand shot renews,
// keep it when the spell is blocked for any other reason.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_wand_policy.cpp -o /tmp/test_wand_policy
//   /tmp/test_wand_policy

#include "../ai/playerbot/WandPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    using P = WandSpellProbe;

    // Wand Shoot (5019) category in Turtle spell data.
    CHECK(WAND_SHOOT_CATEGORY == 351);

    // Live case: wanding at full mana, the nuke refused only by the
    // wand's GCD -> stop the wand.
    CHECK(ShouldStopWandToCast(WandState{true, P::GlobalCooldownOnly, false, false}));
    CHECK(ShouldStopWandToCast(WandState{true, P::Castable, false, false}));

    // Out of mana (or no nuke known): the wand is the right tool.
    CHECK(!ShouldStopWandToCast(WandState{true, P::Blocked, false, false}));

    // Not wanding: nothing to stop.
    CHECK(!ShouldStopWandToCast(WandState{false, P::Castable, false, false}));
    CHECK(!ShouldStopWandToCast(WandState{false, P::GlobalCooldownOnly, false, false}));

    // Raid "has aggro -> shoot" wands on purpose: leave it alone.
    CHECK(!ShouldStopWandToCast(WandState{true, P::Castable, true, false}));
    CHECK(!ShouldStopWandToCast(WandState{true, P::GlobalCooldownOnly, true, false}));

    // Below the "no mana" line the existing "no mana -> shoot" row wands on
    // purpose; stopping there would only ping-pong with it.
    CHECK(!ShouldStopWandToCast(WandState{true, P::Castable, false, true}));
    CHECK(!ShouldStopWandToCast(WandState{true, P::GlobalCooldownOnly, false, true}));

    // Near the tank's threat the "threat" multiplier vetoes the nukes and
    // they fall back to the wand: keep it (live: Uldaman run 4 mage, stop
    // wand -> "shoot" again two seconds later at 85-100% mana).
    CHECK(!ShouldStopWandToCast(WandState{true, P::Castable, false, false, P::Blocked, true}));
    CHECK(!ShouldStopWandToCast(WandState{true, P::GlobalCooldownOnly, false, false, P::Blocked, true}));
    // A wanted heal is never threat-gated: it still stops the wand.
    CHECK(ShouldStopWandToCast(WandState{true, P::Blocked, false, false, P::GlobalCooldownOnly, true}));
    CHECK(ShouldStopWandToCast(WandState{true, P::Castable, false, false, P::Castable, true}));
    // Warlock (Shadow Bolt) and priest nukes under the same veto: keep it.
    CHECK(!ShouldStopWandToCast(WandState{true, P::GlobalCooldownOnly, false, false, P::Blocked, true}));
    // Veto off: back to the GCD rule.
    CHECK(ShouldStopWandToCast(WandState{true, P::GlobalCooldownOnly, false, false, P::Blocked, false}));

    // Priest heals: a wanted heal held back only by the wand's GCD stops
    // the wand, even with no nuke wanted (holy/discipline without offdps)
    // and below the "no mana" line (heal rows outrank that wand row).
    CHECK(ShouldStopWandToCast(WandState{true, P::Blocked, false, false, P::GlobalCooldownOnly}));
    CHECK(ShouldStopWandToCast(WandState{true, P::Blocked, false, false, P::Castable}));
    CHECK(ShouldStopWandToCast(WandState{true, P::Blocked, false, true, P::GlobalCooldownOnly}));
    // No heal wanted or none affordable: back to the nuke rule.
    CHECK(!ShouldStopWandToCast(WandState{true, P::Blocked, false, false, P::Blocked}));
    CHECK(ShouldStopWandToCast(WandState{true, P::GlobalCooldownOnly, false, false, P::Blocked}));
    // Not wanding: nothing to stop for a heal either.
    CHECK(!ShouldStopWandToCast(WandState{false, P::Blocked, false, false, P::Castable}));

    // Probe combination: the better outcome wins (Frostbolt not known at
    // level 1-3, Fireball is; Mind Flay is a talent, Smite is not).
    CHECK(BetterWandSpellProbe(P::Blocked, P::GlobalCooldownOnly) == P::GlobalCooldownOnly);
    CHECK(BetterWandSpellProbe(P::GlobalCooldownOnly, P::Blocked) == P::GlobalCooldownOnly);
    CHECK(BetterWandSpellProbe(P::Castable, P::Blocked) == P::Castable);
    CHECK(BetterWandSpellProbe(P::GlobalCooldownOnly, P::Castable) == P::Castable);
    CHECK(BetterWandSpellProbe(P::Blocked, P::Blocked) == P::Blocked);

    // Probe classification. The core answers NOT_READY for the wand's GCD
    // before it checks silence, school lockout, form or line of sight, so a
    // NOT_READY only counts as "GCD only" when none of those blocks the spell.
    using B = WandCastBlockers;
    CHECK(ClassifyWandSpellProbe(false, true, B{}) == P::GlobalCooldownOnly);
    CHECK(ClassifyWandSpellProbe(true, false, B{}) == P::Castable);
    // Refused for something else (mana, not known): blocked, whatever else.
    CHECK(ClassifyWandSpellProbe(false, false, B{}) == P::Blocked);
    // Silenced mage: Frostbolt is impossible, keep the wand (review finding:
    // before, this read "GCD only" and the wand stopped and restarted every swing).
    CHECK(ClassifyWandSpellProbe(false, true, B{true, false, false, false}) == P::Blocked);
    // Counterspelled: school locked out (prohibited school or per-spell cooldown).
    CHECK(ClassifyWandSpellProbe(false, true, B{false, true, false, false}) == P::Blocked);
    // Smite in Shadowform: the form forbids it.
    CHECK(ClassifyWandSpellProbe(false, true, B{false, false, true, false}) == P::Blocked);
    // Target out of line of sight; the probe ignores range, so even a
    // "castable" answer is blocked then.
    CHECK(ClassifyWandSpellProbe(false, true, B{false, false, false, true}) == P::Blocked);
    CHECK(ClassifyWandSpellProbe(true, false, B{false, false, false, true}) == P::Blocked);
    // End to end: a silenced mage at full mana keeps wanding.
    CHECK(!ShouldStopWandToCast(WandState{true, ClassifyWandSpellProbe(false, true, B{true, false, false, false}), false, false}));
    // A heal out of line of sight does not stop the wand either.
    CHECK(!ShouldStopWandToCast(WandState{true, P::Blocked, false, false,
        ClassifyWandSpellProbe(false, true, B{false, false, false, true})}));

    // Healer wand gate (HealerMayWand). Not a grouped healer: never held back.
    {
        HealerWandGate g;
        CHECK(HealerMayWand(g));
        g.memberNeedsHeal = true;
        CHECK(HealerMayWand(g));
    }
    // Grouped healer, healthy party, mob fighting the tank: wand.
    {
        HealerWandGate g;
        g.groupedHealer = true;
        g.targetEngaged = true;
        CHECK(HealerMayWand(g));
        // Uldaman 2026-10-14: the tank drops under 90 % -> no wand, even at
        // full mana with a "shoot" still queued from the healthy tick.
        g.memberNeedsHeal = true;
        CHECK(!HealerMayWand(g));
        // Out of mana the wand is all that is left.
        g.noMana = true;
        CHECK(HealerMayWand(g));
    }
    // A fresh mob (nobody of the party fights it yet): never, not even
    // out of mana - the healer would pull it onto itself.
    {
        HealerWandGate g;
        g.groupedHealer = true;
        CHECK(!HealerMayWand(g));
        g.noMana = true;
        CHECK(!HealerMayWand(g));
    }
    // A running wand the gate no longer allows stops, whatever the probes
    // say and even under the raid "has aggro" row; no wand, nothing to stop.
    {
        WandState s{true, P::Blocked, false, false};
        s.healerMustStop = true;
        CHECK(ShouldStopWandToCast(s));
        s.keepWandForThreat = true;
        CHECK(ShouldStopWandToCast(s));
        s.noManaRowActive = true;
        CHECK(ShouldStopWandToCast(s));
        s.wandAutoRepeat = false;
        CHECK(!ShouldStopWandToCast(s));
        // Default: unchanged behaviour.
        WandState d{true, P::Blocked, false, false};
        CHECK(!d.healerMustStop);
        CHECK(!ShouldStopWandToCast(d));
    }

    // Wait after the stop: until the next shot would have fired, plus the
    // react delay; never more than one swing.
    CHECK(WandStopWaitMs(1200, 1800, 100) == 1300);
    CHECK(WandStopWaitMs(0, 1800, 100) == 100);
    CHECK(WandStopWaitMs(5000, 1800, 100) == 1900);
    CHECK(WandStopWaitMs(700, 0, 100) == 800);

    std::printf("wand policy: %d checks passed\n", checks);
    return 0;
}
