#pragma once

// Pure decision rules for the hunter's area shots, no core includes: the
// triggers in strategy/hunter/HunterTriggers.h translate game state into
// these plain inputs, so the rules stay testable in
// tools/test_hunter_aoe_policy.cpp without the server.
#include <cstdint>

namespace ai
{
    // Multi-Shot pays for itself only when the splash hits a second live
    // enemy; a lone target gets the single-target kit.
    inline constexpr uint8_t kHunterMultiShotMinTargets = 2;

    // Splash reach of every 1.12 Multi-Shot rank: the core chains extra
    // targets with EffectChainTarget=3 each within CHAIN_SPELL_JUMP_RADIUS
    // of the previous one (SharedDefines.h), starting at the shot's target.
    inline constexpr float kHunterMultiShotSplashYd = 10.0f;

    // The caller counts fighting, reachable enemies inside the splash radius
    // of the shot's target; damage-immune ones (banish, bubbles) never count.
    // Any breakable CC inside the splash vetoes the shot entirely - the
    // splash would shatter it. Unbreakable control (stun, fear, roots) takes
    // the splash harmlessly, so those mobs still count.
    inline bool ShouldHunterMultiShot(uint8_t splashTargets, bool breakableCcInSplash)
    {
        return !breakableCcInSplash && splashTargets >= kHunterMultiShotMinTargets;
    }

    // Carve (Turtle 51575: Survival talent, melee cone, 10 s cooldown shared
    // with Multi-Shot) never walks: it is cast only on a target already in
    // melee reach (edge to edge, the same ATTACK_DISTANCE the melee action
    // checks). Walking onto a tank-held boss for it put Survival hunters in
    // the shot's dead zone, where the dead-zone step-out carried them back
    // out, every 10 s (ZG walk 54 Hakkar: 0.10-0.18 casts per alive second,
    // 28-41 % of the ranged median). Flag off keeps the old walk.
    inline constexpr float kHunterCarveReachYd = 5.0f;
    inline constexpr bool HunterCarveAllowed(bool inReachOnly, float edgeDistYd)
    {
        return !inReachOnly || edgeDistYd <= kHunterCarveReachYd;
    }
}
