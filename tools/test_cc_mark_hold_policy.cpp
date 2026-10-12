// Standalone regression test for the CC mark hold policy: when the party
// leaves a Moon / "rti cc" mark alone, when its own CC bot spares it with
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

    // CC chooser: the assigned mark, once picked, is never replaced by a
    // later free pick; any other interim pick may still be replaced.
    CHECK(CcChooserKeepsResult(true));
    CHECK(!CcChooserKeepsResult(false));

    std::printf("cc mark hold policy: %d checks passed\n", checks);
    return 0;
}
