// Standalone regression test for the CC mark hold policy: when the party
// leaves a Moon / "rti cc" mark alone (target picks, area spells, enemy-
// healer interrupts, a running Blade Flurry), when its own CC bot spares it with
// area spells, and which mob the CC chooser keeps.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_cc_mark_hold_policy.cpp -o /tmp/test_cc_mark_hold_policy
//   /tmp/test_cc_mark_hold_policy

#include "../ai/playerbot/strategy/values/CcMarkHoldPolicy.h"

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

static CcMarkHoldState Free(long combatSeconds, bool ccBotReady)
{
    CcMarkHoldState state;
    state.combatSeconds = combatSeconds;
    state.ccBotReady = ccBotReady;
    return state;
}

int main()
{
    // Unpulled pack mob: always left alone, CC bot or not.
    {
        CcMarkHoldState state;
        state.inCombat = false;
        CHECK(CcMarkHeld(state));
    }

    // Crowd-controlled or under a CC cast bar: held at any combat time,
    // with or without a ready CC bot.
    {
        CcMarkHoldState state = Free(45, false);
        state.crowdControlled = true;
        CHECK(CcMarkHeld(state));
        state = Free(45, false);
        state.ccCastBarOnIt = true;
        CHECK(CcMarkHeld(state));
    }

    // Build 11 pack pull: the pack entered combat at 13:44:40.8, the party
    // turned on the Moon mob at 13:44:44 (old 3 s window over) and the mage
    // began its first Polymorph at 13:44:46.6. With a ready CC bot the mark
    // is now held through all of that.
    CHECK(CcMarkHeld(Free(0, true)));
    CHECK(CcMarkHeld(Free(4, true)));   // tank / priest / Blade Flurry moment
    CHECK(CcMarkHeld(Free(6, true)));   // first Polymorph start
    CHECK(CcMarkHeld(Free(CcMarkHoldSeconds, true)));

    // The window is bounded: a mark still free after it is a normal enemy
    // (owner rule: a free Moon mob must not kill the party).
    CHECK(!CcMarkHeld(Free(CcMarkHoldSeconds + 1, true)));
    CHECK(!CcMarkHeld(Free(24, true)));

    // No ready CC bot (none assigned, dead, out of range, spell missing or
    // on cooldown): free from the first second, as before.
    CHECK(!CcMarkHeld(Free(0, false)));
    CHECK(!CcMarkHeld(Free(2, false)));

    // Unknown combat time (not a creature): no window.
    CHECK(!CcMarkHeld(Free(-1, true)));

    // Monotone: adding any holding fact never releases a held mark.
    {
        CcMarkHoldState state = Free(3, true);
        CHECK(CcMarkHeld(state));
        state.crowdControlled = true;
        CHECK(CcMarkHeld(state));
        state.ccCastBarOnIt = true;
        CHECK(CcMarkHeld(state));
    }

    // The CC bot's own area spells: its mark is spared while other enemies
    // are left (build 11: the mage's Flamestrike hit its free Moon mob 55 s
    // into the fight, long after the hold window). Once only CC marks are
    // left they are killed last and AoE is fine; another bot's mark is not
    // this bot's business.
    CHECK(CcOwnerSparesMark(true, false));
    CHECK(!CcOwnerSparesMark(true, true));
    CHECK(!CcOwnerSparesMark(false, false));
    CHECK(!CcOwnerSparesMark(false, true));

    // Build 13 gate (Stonevault Oracle pack, Moon on one Oracle, the tank
    // pulling the other): two party hits on the Moon mob before the first
    // sheep at 9.8 s, i.e. inside the hold. Every enemy-area spell -
    // instant (Thunder Clap, Arcane Explosion), chained (Cleave, Multi-Shot)
    // or lingering - now spares a held mark, not only lingering ones.
    CHECK(CcAreaSpellBlocked(false, true, false, false));   // held mark in blast
    CHECK(CcAreaSpellBlocked(true, false, false, false));   // sheep in blast
    CHECK(CcAreaSpellBlocked(false, false, true, false));   // CC bot's own mark
    CHECK(!CcAreaSpellBlocked(false, false, true, true));   // only marks left: kill them
    CHECK(!CcAreaSpellBlocked(false, false, false, false)); // nothing protected near
    // A mark the hold has released (window over, nobody can CC it) is a
    // normal enemy for area spells too: the caller passes held = false.
    CHECK(!CcMarkHeld(Free(CcMarkHoldSeconds + 1, true)));
    CHECK(!CcAreaSpellBlocked(false, CcMarkHeld(Free(CcMarkHoldSeconds + 1, true)), false, false));
    CHECK(CcAreaSpellBlocked(false, CcMarkHeld(Free(5, true)), false, false));

    // Interrupts on an enemy healer (Kick, Shield Bash, ...) skip a held
    // mark: a Moon Oracle casting Healing Wave in its hold window is the CC
    // bot's, and Kick / Shield Bash deal damage.
    CHECK(!CcEnemyHealerPickAllowed(true));
    CHECK(CcEnemyHealerPickAllowed(false));

    // Blade Flurry procs onto any enemy within 5 yd of the rogue: a running
    // one is cancelled while a protected enemy is in reach (build 13: the
    // first sheep broke 3 s after it landed), never when it is not up or
    // nothing protected is near. Start and cancel use one radius, at least
    // the core's 5 yd proc reach.
    CHECK(CcCancelBladeFlurry(true, true, false));
    CHECK(!CcCancelBladeFlurry(true, false, false));
    CHECK(!CcCancelBladeFlurry(false, true, true));
    // Turtle Blade Flurry is a toggle that stays up from the last pull.
    // Build 14 gate: three Blade Flurry hits on the Moon mob at 14-17 s,
    // after the 10 s window, before the first sheep. Next to a mark whose
    // CC bot is ready, the toggle is dropped (and not started) even when
    // the hold window is over.
    CHECK(!CcMarkHeld(Free(14, true)));
    CHECK(CcCancelBladeFlurry(true, false, true));
    CHECK(!CcCancelBladeFlurry(false, false, true));
    // Hysteresis: the start is blocked farther out than the cancel fires,
    // and the cancel still covers the core's 5 yd proc reach.
    CHECK(CcBladeFlurryCancelRadius >= 5.0f);
    CHECK(CcBladeFlurryStartRadius > CcBladeFlurryCancelRadius);
    // The rogue's own target in CC (its own Gouge) with nothing marked:
    // the proc never hits the victim, so it is not counted and Blade
    // Flurry is neither cancelled nor blocked.
    CHECK(!CcGuardCountsUnit(true));
    CHECK(CcGuardCountsUnit(false));
    CHECK(!CcCancelBladeFlurry(true, CcGuardCountsUnit(true), false));
    // A ready CC mark still counts when it is the rogue's own target.
    CHECK(CcCancelBladeFlurry(true, CcGuardCountsUnit(true), true));

    // CC chooser: the assigned mark, once picked, is never replaced by a
    // later free pick; any other interim pick may still be replaced.
    CHECK(CcChooserKeepsResult(true));
    CHECK(!CcChooserKeepsResult(false));

    // Owner decision: the tank's Taunt (and nothing else) passes the hold,
    // while the held mark is not CC'd yet, no CC cast bar is on it and it is
    // hitting a healer.
    CHECK(CcHeldMarkTauntAllowed(true, false, false, true));
    CHECK(!CcHeldMarkTauntAllowed(false, false, false, true));  // not the tank's Taunt (snare, DPS, ...)
    CHECK(!CcHeldMarkTauntAllowed(true, true, false, true));    // already sheeped
    CHECK(!CcHeldMarkTauntAllowed(true, false, true, true));    // sheep on its way
    CHECK(!CcHeldMarkTauntAllowed(true, false, false, false));  // hitting the tank, a DPS or nobody

    // CC cast reach: Polymorph (30 yd) starts within 25 yd and in sight,
    // so a mark moving during the 1.5 s cast stays in range. Out of sight
    // or beyond 25 yd the reach runs first (build 11: four Polymorph starts
    // that never finished).
    CHECK(CcCastReach(30.0f) == 25.0f);
    CHECK(CcCastInReach(20.0f, 30.0f, true));
    CHECK(CcCastInReach(25.0f, 30.0f, true));
    CHECK(!CcCastInReach(27.0f, 30.0f, true));
    CHECK(!CcCastInReach(10.0f, 30.0f, false));
    // Short-range CC (melee reach, 10 yd and below) keeps its full range.
    CHECK(CcCastReach(10.0f) == 10.0f);
    CHECK(CcCastReach(5.0f) == 5.0f);
    CHECK(CcCastInReach(5.0f, 5.0f, true));

    std::printf("cc mark hold policy: %d checks passed\n", checks);
    return 0;
}
