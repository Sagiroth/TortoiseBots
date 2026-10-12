#pragma once

// Pure policy for a CC mark (Moon, or a bot's assigned "rti cc" mark) while
// other enemies still fight the group: when the party leaves it alone (no
// picks, Taunt, DoT spread, area spells), when its own CC bot spares it with
// area spells, and which mob the CC bot's chooser keeps. Kept core-free so
// the standalone g++ policy test can include it; the callers in
// TargetValue.cpp and CcTargetValue.cpp fill in the game state.

namespace ai
{
    // How long a CC mark whose CC bot is ready stays off-limits at the start
    // of its fight (whole seconds of creature combat). A pack mob enters
    // combat when the first pack member is pulled (creature group assist),
    // so the clock runs before the CC bot can act: in-game (build 11) the
    // CC mage was still finishing its opener on the pull target and began
    // its first Polymorph 6 s after the pack entered combat. The old 3 s
    // window had run out by then and the tank, the priest and Blade Flurry
    // were already on the Moon mob. 10 s covers a finishing cast, one
    // Polymorph and one retry; after that a mark that is still free is a
    // normal enemy (owner rule: a free mark must not kill the party).
    constexpr long CcMarkHoldSeconds = 10;

    struct CcMarkHoldState
    {
        bool crowdControlled = false; // breakable CC, fear or banish on it now
        bool inCombat = true;         // false: unpulled pack mob
        bool ccCastBarOnIt = false;   // a group member's CC cast bar aims at it
        bool ccBotReady = false;      // its CC bot can CC it now (see caller)
        long combatSeconds = -1;      // creature combat time; -1 = unknown
    };

    // The mark is held (the party leaves it alone) while it is
    // crowd-controlled, not pulled yet, under a CC cast bar, or in its hold
    // window with a ready CC bot. Monotone in every flag, so a caller may
    // stop filling the state as soon as this returns true.
    inline bool CcMarkHeld(CcMarkHoldState const& state)
    {
        if (state.crowdControlled || !state.inCombat || state.ccCastBarOnIt)
            return true;
        return state.ccBotReady && state.combatSeconds >= 0 &&
            state.combatSeconds <= CcMarkHoldSeconds;
    }

    // The CC bot's own area spells spare its mark for the whole fight while
    // it would still CC the mark (other enemies left), not only in the hold
    // window: a Flamestrike patch or Blizzard on its own mark breaks the
    // sheep it casts next, and a Frost Nova root blocks it ("never CC over
    // CC"). Once only CC marks are left they are killed and AoE is allowed.
    inline bool CcOwnerSparesMark(bool ownReadyMark, bool onlyCcMarksLeft)
    {
        return ownReadyMark && !onlyCcMarksLeft;
    }

    // CC target chooser: once the bot's own assigned mark is the result, a
    // later candidate (an open-world free pick) never replaces it.
    inline bool CcChooserKeepsResult(bool resultIsAssignedMark)
    {
        return resultIsAssignedMark;
    }

    // Owner decision (2026-10-12): a held mark that is not crowd-controlled
    // yet and is hitting a healer may be TAUNTED by the tank. Taunt only (no
    // damage, the tank keeps its main target); nothing else passes the hold.
    // Not while a CC cast bar is on it: the taunted mob runs to the tank and
    // can leave the caster's range or sight before the cast ends.
    inline bool CcHeldMarkTauntAllowed(bool tankTaunt, bool crowdControlled,
        bool ccCastBarOnIt, bool victimIsHealer)
    {
        return tankTaunt && !crowdControlled && !ccCastBarOnIt && victimIsHealer;
    }

    // A CC cast starts only this far inside the spell's range: the mark is
    // fighting and moving, and a target that leaves range (or sight) during
    // the 1.5 s cast bar makes the cast fail when it finishes. The CC reach
    // action closes to the same distance. Short-range CC keeps its range.
    constexpr float CcCastRangeMargin = 5.0f;

    inline float CcCastReach(float maxRange)
    {
        return maxRange > 2.0f * CcCastRangeMargin ? maxRange - CcCastRangeMargin : maxRange;
    }

    // The CC bot may start its CC cast on the mark: in line of sight and
    // within CcCastReach. Otherwise the reach prerequisite moves it first.
    inline bool CcCastInReach(float distance, float maxRange, bool inLineOfSight)
    {
        return inLineOfSight && distance <= CcCastReach(maxRange);
    }
}
