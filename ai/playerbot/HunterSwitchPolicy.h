#pragma once

// Hunter dead-zone kit-switch hysteresis (pool 1v1 fix, Oct 2026).
// Pure decision rules, no core includes: the triggers in
// strategy/hunter/HunterTriggers.h translate game state into these plain
// inputs, so the rules stay testable in tools/test_hunter_switch_policy.cpp
// without the server.
//
// Donor comparison (mod-playerbots, read-only reference):
// SwitchToMeleeTrigger::IsActive is victim-on-bot AND distance<=8, and
// SwitchToRangedTrigger::IsActive is victim-off-bot AND distance>8
// (HunterTriggers.cpp:112-126 - both are ANDs, not ORs). Neither has a level
// gate. The melee rule below is that exact donor AND plus the local no-ammo
// branch; the ranged rule drops the distance half (see ShouldSwitchToRanged)
// and keeps the rooted branch, so a mob pacing the 8 yd line cannot flip the kit
// every tick (fresh pool: inter-switch p50 13 s, 41% within 10 s, each flip
// rebuilding the combat engine's trigger graph). The fresh-pool run is what
// exposed the old extra branches as flap sources:
//  - tooSlowToFollow answered 83% of melee->ranged pairs at dist < 5 yd: at
//    pool levels a walking mob is always "slower than half run speed", so the
//    OR fired right after every melee trade, before the bot moved a step.
//  - fastOrFinisher's pet-gone half let SwitchToMelee fire at 5-8 yd on slow
//    mobs (76% of ranged->melee pairs landed at 5-8 yd), re-trading for a mob
//    the step-back already handles. Both extras are gone; the signatures keep
//    the old parameters (marked void) so the triggers keep compiling.
//
// Fix round (Oct 2026): with the ranged kit back as soon as the mob is off
// the bot, a pet-held mob inside the dead zone sent the hunter into the
// generic flee every tick. For pool bots that flee is a MoveChase at flee
// range, which the core's chase generator turns into "stay put" while the
// mob stands still within 1.4x that range - so the flee re-fired without
// moving and the hunter neither shot nor meleed (flee 57-63 % of combat
// samples). The dead zone now gets a step-out per mob (below) and a minimum
// dwell on the kit so aggro trading inside 8 yd cannot rebuild strategies
// every tick. Ranged first (#1018): a failed step is retried farther, never
// answered with a melee trade for a mob that is not on the hunter.

#include <cstdint>

namespace ai
{
    // Glued inside the shot's dead zone: the donor's own melee edge, and the
    // same band the GenericHunterStrategy step-back ("enemy too close for
    // auto shot" -> disengage/flee) uses, so the switch and the step-back
    // agree on where the dead zone ends.
    inline constexpr float kHunterMeleeGlueYd = 8.0f;
    // Return margin past the dead zone for future hysteresis use; the rules
    // below switch on the donor's 8 yd.
    inline constexpr float kHunterRangedReturnYd = 10.0f;

    // Trades into melee once the mob is really on the bot (victim set) and
    // glued inside the dead zone: the donor's AND (victim AND dist<=8) plus
    // mobile (not rooted - a rooted mob at 8 yd is shot, not charged).
    // The old fast-or-finisher OR is gone: it let melee fire at 5-8 yd on
    // slow mobs (76% of ranged->melee pairs landed at 5-8 yd), re-trading
    // for a mob the step-back already handles. A slow mob glued on the bot
    // is what Wing Clip and the step-back are for. Out of ammo always
    // trades: there is nothing to shoot with.
    // Ranged first (issue #1018): a failed dead-zone step-out
    // (stuckInDeadZone) no longer trades into melee. Stuck only happens for a
    // mob that is NOT on the hunter (DecideHunterDeadZoneStep), and trading
    // there walked group hunters onto the tank's mob (Zul'Farrak, 3 hunters:
    // melee kit 13-20 % of fight time, mob on the tank in 47-73 % of those
    // frames). A failed step now retries farther instead (see below). Every
    // trade except out-of-ammo waits for the kit dwell (dwellElapsed).
    inline bool ShouldSwitchToMelee(bool hasRangedKit, bool victimOnBot, bool immobilized,
        bool fastOrFinisher, float distanceYd, bool hasAmmo,
        bool stuckInDeadZone = false, bool dwellElapsed = true)
    {
        if (!hasAmmo)
            return true;
        (void)fastOrFinisher;
        (void)stuckInDeadZone;
        if (!hasRangedKit)
            return false;
        return dwellElapsed && victimOnBot && !immobilized &&
            distanceYd <= kHunterMeleeGlueYd;
    }

    // Hands the ranged kit back as soon as the mob is off the bot (pet, tank
    // or anyone else holds it) or rooted (shot from where the bot stands), at
    // any distance. The old "and past 8 yd" half trapped the hunter: the close
    // kit's own "reach melee" keeps it inside 8 yd, so once the pet took the
    // mob back the hunter meleed it for the rest of the fight. The ranged
    // kit's dead-zone step-back then moves it out to shoot (mod-playerbots
    // 79bd428 has no melee kit at all; its hunter only steps out). Victim on
    // vs off the bot is the hysteresis: melee needs it on, ranged needs it
    // off, so distance pacing cannot flip the kit. The old slow-or-far OR
    // stays gone (it flipped 83% of melee->ranged pairs at dist < 5 yd
    // while the mob was still on the bot). Below level 10 this is what
    // restores ranged after a melee trade.
    //
    // Fix round: victim on/off alone is no hysteresis inside 8 yd (REVIEW
    // finding 4): the two rules are complements there, so pet Growl and the
    // hunter's own shots trading aggro flipped the kit every few seconds,
    // each flip rebuilding the combat engine. The kit now has to have been
    // held kHunterKitMinDwellMs first (dwellElapsed). stepOutBlocked used to
    // hold the melee kit after the stuck trade; that trade is gone (#1018),
    // so a mob off the hunter now always hands the ranged kit back after the
    // dwell and the ranged kit's step-out moves the hunter out.
    inline bool ShouldSwitchToRanged(bool hasCloseKit, bool targetOffBot, bool immobilized,
        bool tooSlowToFollow, float distanceYd, bool hasAmmo,
        bool stepOutBlocked = false, bool dwellElapsed = true)
    {
        if (!(hasCloseKit && hasAmmo))
            return false;
        if (!dwellElapsed)
            return false;
        if (immobilized)
            return true;
        (void)tooSlowToFollow;
        (void)distanceYd;
        (void)stepOutBlocked;
        return targetOffBot;
    }

    // Minimum time a kit is held before the next victim- or root-driven
    // switch (REVIEW finding 4 asked for 3-5 s).
    inline constexpr uint32_t kHunterKitMinDwellMs = 4000;
    // One dead-zone step-out: the move gets this long to carry the hunter
    // out before it counts as failed.
    inline constexpr uint32_t kHunterStepOutWindowMs = 3000;
    // A step-out episode is per mob and lasts this long. Inside it a step
    // that did not get the hunter out is retried, farther each time, up to
    // kHunterStepOutMaxAttempts; after that the hunter holds the ranged kit
    // (it never trades into melee for a mob that is not on it) until the
    // episode runs out and a fresh one starts.
    inline constexpr uint32_t kHunterStepOutRetryMs = 10000;
    inline constexpr uint8_t kHunterStepOutMaxAttempts = 3;
    // The step aims this far from the target (edge to edge): well clear of
    // the 8 yd dead zone, so a small move of the mob or the tank does not put
    // the hunter straight back inside (#1018; it used to aim edge + 3 yd).
    // Each retry aims kHunterStepOutRetryExtraYd farther.
    inline constexpr float kHunterStepOutTargetYd = 15.0f;
    inline constexpr float kHunterStepOutRetryExtraYd = 5.0f;
    // Floor for the aim: never less than this past the dead zone edge.
    inline constexpr float kHunterStepOutMarginYd = 3.0f;
    // Step length bounds: never a shuffle, never a run across the map.
    inline constexpr float kHunterStepOutMinYd = 4.0f;
    inline constexpr float kHunterStepOutMaxYd = 20.0f;

    // Per-bot bookkeeping for the rules above (the "hunter kit state" value).
    // Times are WorldTimer ms, compared with wrap-safe uint32 subtraction;
    // 0 means "never".
    struct HunterKitState
    {
        uint32_t lastKitSwitchMs = 0;
        uint64_t stepOutTarget = 0;
        uint32_t stepOutStartMs = 0;
        // Current attempt inside the episode (1-based) and when it began.
        uint8_t stepOutAttempt = 0;
        uint32_t stepOutAttemptMs = 0;
    };

    inline bool HunterKitDwellElapsed(HunterKitState const& state, uint32_t nowMs)
    {
        return state.lastKitSwitchMs == 0 || nowMs - state.lastKitSwitchMs >= kHunterKitMinDwellMs;
    }

    inline void NoteHunterKitSwitch(HunterKitState& state, uint32_t nowMs)
    {
        state.lastKitSwitchMs = nowMs ? nowMs : 1;
    }

    inline bool HunterStepOutEpisodeOpen(HunterKitState const& state, uint64_t target, uint32_t nowMs)
    {
        return state.stepOutStartMs != 0 && state.stepOutTarget == target &&
            nowMs - state.stepOutStartMs < kHunterStepOutRetryMs;
    }

    // Starts a step: a fresh episode for a new mob or after the episode ran
    // out, otherwise the next (farther) attempt inside the open episode.
    inline void BeginHunterStepOut(HunterKitState& state, uint64_t target, uint32_t nowMs)
    {
        uint32_t const stamp = nowMs ? nowMs : 1;
        if (HunterStepOutEpisodeOpen(state, target, nowMs))
        {
            if (state.stepOutAttempt < 255)
                ++state.stepOutAttempt;
        }
        else
        {
            state.stepOutTarget = target;
            state.stepOutStartMs = stamp;
            state.stepOutAttempt = 1;
        }
        state.stepOutAttemptMs = stamp;
    }

    // Ranged kit, mob inside the shot's dead zone:
    //  None    - not in the dead zone, or the mob is on the hunter (the
    //            melee switch / kite flee own that case).
    //  StepOut - no episode for this mob yet, or the last step did not get
    //            the hunter out and attempts are left: step out (farther).
    //  Hold    - a step is under way (moving, inside the window): let it
    //            finish, do not re-dispatch.
    //  Stuck   - every attempt of this episode failed: hold the ranged kit
    //            until the episode runs out. Never a melee trade (#1018).
    enum class HunterDeadZoneStep { None, StepOut, Hold, Stuck };

    inline HunterDeadZoneStep DecideHunterDeadZoneStep(HunterKitState const& state, uint64_t target,
        uint32_t nowMs, bool inDeadZone, bool victimOnBot, bool moving)
    {
        if (!inDeadZone || victimOnBot)
            return HunterDeadZoneStep::None;
        if (!HunterStepOutEpisodeOpen(state, target, nowMs))
            return HunterDeadZoneStep::StepOut;
        if (moving && nowMs - state.stepOutAttemptMs < kHunterStepOutWindowMs)
            return HunterDeadZoneStep::Hold;
        if (state.stepOutAttempt < kHunterStepOutMaxAttempts)
            return HunterDeadZoneStep::StepOut;
        return HunterDeadZoneStep::Stuck;
    }

    // Straight-back step from edgeDistYd that lands kHunterStepOutTargetYd
    // from the target (plus kHunterStepOutRetryExtraYd per retry; attempt is
    // 1-based), clamped to [kHunterStepOutMinYd, kHunterStepOutMaxYd]. The
    // landing never aims inside the dead zone plus kHunterStepOutMarginYd,
    // whatever the shot's minimum range is.
    inline float HunterStepOutLength(float edgeDistYd, float minRangeYd, uint8_t attempt = 1)
    {
        float aim = kHunterStepOutTargetYd + kHunterStepOutRetryExtraYd * float(attempt > 1 ? attempt - 1 : 0);
        if (aim < minRangeYd + kHunterStepOutMarginYd)
            aim = minRangeYd + kHunterStepOutMarginYd;
        float const need = aim - edgeDistYd;
        if (need < kHunterStepOutMinYd)
            return kHunterStepOutMinYd;
        if (need > kHunterStepOutMaxYd)
            return kHunterStepOutMaxYd;
        return need;
    }

    // Explosive / frost / immolation trap on the current target: the hunter
    // lays it at its own feet after Feign Death and no longer walks to the
    // target first (#1018: 22-29 "reach melee" walks per Zul'Farrak run, 0
    // traps cast). So the trap is only laid when the target is already in
    // melee reach of the hunter (edge to edge) - the solo case, a mob on the
    // hunter. A mob the tank holds 12 yd away gets no trap and no walk.
    inline constexpr float kHunterTrapReachYd = 5.0f;
    inline constexpr bool HunterTrapTargetInReach(float edgeDistYd)
    {
        return edgeDistYd <= kHunterTrapReachYd;
    }

    // Raptor Strike / Mongoose Bite / Wing Clip against a mob that is already
    // on the hunter: the party threat back-off (ThreatMultiplier) has nothing
    // to protect there, and applying it vetoed all three while the mob held
    // the hunter (live test Oct 2026: ~98 s in the melee kit, 16 hits taken,
    // white swings only). A mob someone else holds keeps the back-off.
    inline constexpr bool HunterMeleeRowIgnoresThreatBackoff(bool targetOnHunter)
    {
        return targetOnHunter;
    }

    // Evaluates whether a candidate shooting spot is clear of an unpulled creature's aggro radius.
    inline bool IsShootingSpotSafeFromUnpulled(float distToCreature, float aggroDistance)
    {
        return distToCreature >= aggroDistance;
    }

    // Evaluates whether a candidate shooting spot is outside an active enemy's dead zone
    // and does not step closer to an approaching enemy targeting the bot.
    inline bool IsShootingSpotSafeFromEnemy(float distToEnemy, float minRange,
        float currentDistToEnemy, bool victimOnBot)
    {
        if (distToEnemy < minRange + 1.0f)
            return false;
        if (victimOnBot && distToEnemy < currentDistToEnemy)
            return false;
        return true;
    }

    // Scores a candidate shooting spot: prefers larger clearance from enemies,
    // with tie-break favoring smaller deflection angle.
    inline float ScoreShootingSpot(float minEnemyDist, float angleOffset)
    {
        return minEnemyDist - (angleOffset < 0.0f ? -angleOffset : angleOffset) * 0.5f;
    }
}
