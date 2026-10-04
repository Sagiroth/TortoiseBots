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
// gate. The rules below are those exact donor ANDs plus the local no-ammo
// and rooted branches, so a mob pacing the 8 yd line cannot flip the kit
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
    inline bool ShouldSwitchToMelee(bool hasRangedKit, bool victimOnBot, bool immobilized,
        bool fastOrFinisher, float distanceYd, bool hasAmmo)
    {
        if (!hasAmmo)
            return true;
        (void)fastOrFinisher;
        return hasRangedKit && victimOnBot && !immobilized &&
            distanceYd <= kHunterMeleeGlueYd;
    }

    // Hands the ranged kit back once the bot has room (the donor's AND: off
    // the bot AND past the dead zone) or the mob is rooted (shot from where
    // the bot stands). The old slow-or-far OR is gone: at pool levels every
    // walking mob reads as "too slow", which is what flipped 83% of
    // melee->ranged pairs back at dist < 5 yd. Below level 10 this is what
    // restores ranged after a melee trade.
    inline bool ShouldSwitchToRanged(bool hasCloseKit, bool targetOffBot, bool immobilized,
        bool tooSlowToFollow, float distanceYd, bool hasAmmo)
    {
        if (!(hasCloseKit && hasAmmo))
            return false;
        if (immobilized)
            return true;
        (void)tooSlowToFollow;
        return targetOffBot && distanceYd > kHunterMeleeGlueYd;
    }
}
