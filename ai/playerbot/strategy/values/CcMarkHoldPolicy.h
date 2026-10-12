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

    // "Only CC marks are left" (the party then kills the marks last, the CC
    // bot stops re-CCing its mark, AoE is allowed again) needs at least one
    // live enemy seen in the group's attackers list, and no unmarked one.
    // An empty list (stale cache, or nothing fighting yet) is not "only
    // marks left": before, it was true by default, so a CC bot could refuse
    // its own mark and the party could open on an unpulled mark. The unit
    // the caller excepts (the CC bot's mark it is about to CC) counts as
    // seen but is never the unmarked enemy, so a mark that is the only enemy
    // left still reads "only marks left": it is killed, not sheeped.
    inline bool CcOnlyMarksLeft(int liveEnemiesSeen, bool unmarkedEnemySeen)
    {
        return liveEnemiesSeen > 0 && !unmarkedEnemySeen;
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

    // An enemy-area spell is not cast while its blast reaches a mob in
    // breakable CC, a held mark or (for the CC bot) its own ready mark.
    // Instant areas and chains (Cleave, Thunder Clap, Arcane Explosion,
    // Multi-Shot) spare a held mark too, not only lingering ones: the hold
    // is "nobody touches it", and in the build 13 gate the tank hit the
    // Moon mob twice inside the hold, before the first sheep (9.8 s), while
    // it fought the pack mob next to it.
    // The hold is short (cast bar or the first CcMarkHoldSeconds), so the
    // area spells only wait that long.
    inline bool CcAreaSpellBlocked(bool breakableCcInBlast, bool heldMarkInBlast,
        bool ownReadyMarkInBlast, bool onlyCcMarksLeft)
    {
        return breakableCcInBlast || heldMarkInBlast ||
            CcOwnerSparesMark(ownReadyMarkInBlast, onlyCcMarksLeft);
    }

    // Single-target picks outside the target tournaments (interrupts on an
    // enemy healer: Kick, Shield Bash, Pummel, Counterspell, Hammer of
    // Justice, ...) skip a held mark like the tournaments do. They read the
    // raw attack list, so a Moon mob that heals (Stonevault Oracle: Healing
    // Wave) could draw the rogue's Kick and the tank's Shield Bash in its hold
    // window, and Kick and Shield Bash deal damage in vanilla.
    inline bool CcEnemyHealerPickAllowed(bool heldMark)
    {
        return !heldMark;
    }

    // Blade Flurry copies each swing onto a random enemy within 5 yd of the
    // rogue (core spell_rogue_blade_flurry), sheeped or not. On Turtle it is
    // a toggle (spell 13877: infinite duration, 4 s cooldown), so it stays
    // up from an earlier pull and refusing to *start* it beside a protected
    // enemy almost never applies. A running Blade Flurry is cancelled while
    // a protected enemy (breakable CC or a held mark) is in reach, and also
    // next to a CC mark whose CC bot is ready to (re)sheep it, even after
    // the hold window: the splash is not a target choice, the party still
    // fights a free mark, only without the cleave that keeps breaking or
    // pre-empting the sheep (build 13 gate: sheep broke 3 s after it
    // landed; build 14 gate: three Blade Flurry hits on the Moon mob at
    // 14-17 s, before the first sheep). Once only CC marks are left the
    // caller passes neither, and Blade Flurry is allowed again.
    inline bool CcCancelBladeFlurry(bool bladeFlurryUp, bool protectedEnemyInReach,
        bool readyCcMarkInReach)
    {
        return bladeFlurryUp && (protectedEnemyInReach || readyCcMarkInReach);
    }

    // CC target chooser: once the bot's own assigned mark is the result, a
    // later candidate (an open-world free pick) never replaces it.
    inline bool CcChooserKeepsResult(bool resultIsAssignedMark)
    {
        return resultIsAssignedMark;
    }

    // Blade Flurry reach: a running one is cancelled within the cancel
    // radius (the core's 5 yd proc radius plus drift while it runs), and
    // it is not started within the larger start radius. The gap is the
    // hysteresis: a sheep or mark sitting at the edge of the scrum, with
    // the rogue repositioning behind its target, would otherwise cycle
    // cancel/recast every 4 s cooldown (25 energy and a GCD each time).
    constexpr float CcBladeFlurryCancelRadius = 8.0f;
    constexpr float CcBladeFlurryStartRadius = 11.0f;

    // The guards skip one unit the caller names. Blade Flurry names the
    // rogue's own victim: the core proc picks its extra target from the
    // enemies within 5 yd *other than* the victim
    // (SelectRandomUnfriendlyTarget(victim, ...) drops it), so the
    // rogue's own Gouge on its target must neither cancel nor block it
    // (it did, with nothing marked, once per Gouge). A ready CC mark still
    // counts even when it is the rogue's target (ReadyCcMarkNear).
    inline bool CcGuardCountsUnit(bool isSkippedUnit)
    {
        return !isSkippedUnit;
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
