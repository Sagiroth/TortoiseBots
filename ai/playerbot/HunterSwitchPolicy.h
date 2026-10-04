#pragma once

// Hunter dead-zone kit-switch hysteresis (pool 1v1 fix, Oct 2026).
// Pure decision rules, no core includes: the triggers in
// strategy/hunter/HunterTriggers.h translate game state into these plain
// inputs, so the rules stay testable in tools/test_hunter_switch_policy.cpp
// without the server.
//
// Donor comparison (mod-playerbots, read-only reference):
// SwitchToMeleeTrigger::IsActive is victim-on-bot AND distance<=8, and
// SwitchToRangedTrigger::IsActive is off-bot OR immobilized OR slow OR
// distance>8 (HunterTriggers.cpp:112-128; the 8 yd edge is also the
// ranged-slot minimum range the donor's auto shot uses). Both shapes are
// kept here unchanged - including no level gate on either side - and only
// the shared distance edge becomes a hysteresis band: melee at 5 yd and
// below, ranged back at 10 yd and above. Between the two the bot holds
// whatever kit it has, so a mob pacing the old single 8 yd line cannot flip
// the kit every tick (live pool: inter-switch p50 15 s, 37% within 10 s,
// each flip rebuilding the combat engine's trigger graph).

namespace ai
{
    // Glued inside the dead zone: the shot cannot fire here.
    inline constexpr float kHunterMeleeGlueYd = 5.0f;
    // Past the dead zone with margin: the shot fires again here.
    inline constexpr float kHunterRangedReturnYd = 10.0f;

    // Trades into melee once the mob is glued inside the dead zone. The rest
    // mirrors the donor's AND: victim on the bot, not immobilized, plus the
    // local fast-or-finisher shape (a fast mob stays glued; a slow one is
    // only worth trading for when the pet is gone and the mob is nearly
    // dead). Out of ammo always trades: there is nothing to shoot with.
    inline bool ShouldSwitchToMelee(bool hasRangedKit, bool victimOnBot, bool immobilized,
        bool fastOrFinisher, float distanceYd, bool hasAmmo)
    {
        if (!hasAmmo)
            return true;
        return hasRangedKit && victimOnBot && !immobilized && fastOrFinisher &&
            distanceYd <= kHunterMeleeGlueYd;
    }

    // Hands the ranged kit back once the bot can actually shoot again: the
    // target is off the bot, immobilized, too slow to follow, or 10+ yd out.
    // Below level 10 this is what restores ranged after a melee trade.
    inline bool ShouldSwitchToRanged(bool hasCloseKit, bool targetOffBot, bool immobilized,
        bool tooSlowToFollow, float distanceYd, bool hasAmmo)
    {
        return hasCloseKit && hasAmmo &&
            (targetOffBot || immobilized || tooSlowToFollow ||
                distanceYd >= kHunterRangedReturnYd);
    }
}
