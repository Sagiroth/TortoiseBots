#pragma once

// Shadow priest keeps Shadowform (Tortoise Trials #1556, trial
// `priest-shadow-form-prepull`): over 4 reps the shadow priest dropped
// Shadowform 19 times - 10 drops for a Renew on a tank a living healer
// already covered (63-70 % health, holy priest alive, in range, 20 %+ mana),
// and 8 orphan drops with no heal after them. Each orphan came right after
// a GCD: a wand shot (on Turtle every wand shot puts the GCD on the shooter
// for the whole swing, WandPolicy.h), Mind Blast or Vampiric Embrace. The
// heal answered "not ready", the engine fell through to Shoot, and the form
// stayed off for up to 16 s until the Shadowform row re-cast it.
//
// Engine order (Engine.cpp): the engine checks isUseful(), then pushes the
// "remove shadowform" prerequisite, then re-pushes the heal and only then
// checks isPossible(). So the form is removed before anyone asks whether
// the heal can go out now (GCD, wand swing, mana). The gate below closes a
// heal in Shadowform unless, the form aside, it could be cast right now; the
// offheal Renew yields a covered ally to the covering healer instead.
//
// No core includes: callers translate game state into plain inputs, so the
// rules stay testable in tools/test_shadowform_heal_policy.cpp.

namespace ai
{
    // How a heal probe (PlayerbotAI::CanCastSpell with its check result) came
    // out, read the form aside.
    enum class FormHealProbe
    {
        Castable,  // CanCastSpell said yes.
        FormOnly,  // it said no, with exactly the form's own cast error.
        Blocked    // refused for anything else (GCD, mana, cooldowns...).
    };

    // Turn a probe (castable / refused with the form's own error) into a
    // FormHealProbe. `formOnly` is true when the refusal result equals the
    // spell's GetErrorAtShapeshiftedCast for the bot's form, and that error
    // is not SPELL_CAST_OK.
    inline FormHealProbe ClassifyFormHealProbe(bool castable, bool formOnly)
    {
        if (castable)
            return FormHealProbe::Castable;
        return formOnly ? FormHealProbe::FormOnly : FormHealProbe::Blocked;
    }

    // Open when not in Shadowform; in Shadowform open iff the probe is not
    // Blocked and no blocker holds (silenced, own GCD running, wand swing
    // running). Range and line of sight are not blockers: the heal actions
    // walk into range ("reach party member to heal"), just as they do today.
    inline bool ShadowformHealGateOpen(bool inShadowform, FormHealProbe probe, bool blocked)
    {
        if (!inShadowform)
            return true;
        return probe != FormHealProbe::Blocked && !blocked;
    }

    // True when the Renew (D1) or the wand-stop heal target (D3) yields to a
    // healer that already covers the ally: in Shadowform, an offheal priest
    // (not healer spec), the member at or above the low band (below 50 % the
    // off-heal must keep firing), and another fit healer covers them.
    inline bool ShadowformOffhealYields(bool inShadowform, bool healerSpec, bool belowLowHealth, bool anotherHealerCovers)
    {
        return inShadowform && !healerSpec && !belowLowHealth && anotherHealerCovers;
    }
}
