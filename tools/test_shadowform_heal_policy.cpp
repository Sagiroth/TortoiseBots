// Standalone regression test for "a shadow priest keeps Shadowform"
// (Tortoise Trials #1556, trial `priest-shadow-form-prepull`).
//
// Over 4 reps the shadow priest dropped Shadowform 19 times: 10 drops for a
// Renew on a tank another healer already covered, and 8 orphan drops with no
// heal after them. Each orphan came right after a GCD: a wand shot (on Turtle
// every wand shot puts the GCD on the shooter for the whole swing), Mind
// Blast or Vampiric Embrace. The heal answered "not ready", the engine fell
// through to Shoot, and the form stayed off for up to 16 s.
//
// Engine order (Engine.cpp): isUseful() first, then the "remove shadowform"
// prerequisite, then isPossible(). So the form is gone before anyone asks
// whether the heal can go out. The gate (ShadowformHealPolicy.h) closes the
// heal in Shadowform unless, the form aside, it could be cast right now, and
// the offheal Renew yields a covered ally to the covering healer.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_shadowform_heal_policy.cpp -o /tmp/test_shadowform_heal_policy
//   /tmp/test_shadowform_heal_policy

#include "../ai/playerbot/policy/heal/ShadowformHealPolicy.h"

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
    using P = FormHealProbe;

    // The bug: in Shadowform, right after a wand shot the heal answers
    // NOT_READY (neither castable nor the form's own error), so the gate
    // must stay closed and the engine must never reach "remove shadowform".
    CHECK(!ShadowformHealGateOpen(true, ClassifyFormHealProbe(false, false), false));
    // Same with the heal refusing for the form's error but the wand swing
    // still running: belt and braces, the gate stays closed.
    CHECK(!ShadowformHealGateOpen(true, P::FormOnly, true));

    // In Shadowform the gate is open when the heal could go out at once:
    // refused only for the form itself, or outright castable, and no blocker.
    CHECK(ShadowformHealGateOpen(true, P::FormOnly, false));
    CHECK(ShadowformHealGateOpen(true, P::Castable, false));
    // A castable heal during the bot's own GCD still waits: dropping the
    // form into the GCD is the orphan-drop bug.
    CHECK(!ShadowformHealGateOpen(true, P::Castable, true));

    // Out of Shadowform the gate is open whatever the probe says: holy and
    // discipline priests (never in form) are untouched.
    CHECK(ShadowformHealGateOpen(false, P::Castable, false));
    CHECK(ShadowformHealGateOpen(false, P::Castable, true));
    CHECK(ShadowformHealGateOpen(false, P::FormOnly, false));
    CHECK(ShadowformHealGateOpen(false, P::FormOnly, true));
    CHECK(ShadowformHealGateOpen(false, P::Blocked, false));
    CHECK(ShadowformHealGateOpen(false, P::Blocked, true));

    // Probe classification: the form's own cast error (not SPELL_CAST_OK)
    // reads as FormOnly; anything else is Blocked.
    CHECK(ClassifyFormHealProbe(true, false) == P::Castable);
    CHECK(ClassifyFormHealProbe(true, true) == P::Castable);
    CHECK(ClassifyFormHealProbe(false, true) == P::FormOnly);
    CHECK(ClassifyFormHealProbe(false, false) == P::Blocked);

    // D1: the covered medium-band Renew case from the trial yields.
    CHECK(ShadowformOffhealYields(true, false, false, true));
    // Flipping any one input keeps the heal: not in form; healer spec;
    // at or below 50% (the off-heal must never be held back there); no
    // other fit healer covering.
    CHECK(!ShadowformOffhealYields(false, false, false, true));
    CHECK(!ShadowformOffhealYields(true, true, false, true));
    CHECK(!ShadowformOffhealYields(true, false, true, true));
    CHECK(!ShadowformOffhealYields(true, false, false, false));

    std::printf("shadowform heal policy: %d checks passed\n", checks);
    return 0;
}
