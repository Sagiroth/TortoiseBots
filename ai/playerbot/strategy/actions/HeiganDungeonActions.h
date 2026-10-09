#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class HeiganEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    HeiganEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable heigan fight strategy", "+heigan") {}
};

class HeiganDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    HeiganDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable heigan fight strategy", "-heigan") {}
};

// Safety dance: move to the section that is safe for the next eruption.
// Sections and cadence from the vanilla core script (boss_heigan.cpp):
// first erupt 4s into the dance, then every 3s; safe area walks
// 0,1,2,3,2,1. Dance start = Plague Cloud (29350) remaining on Heigan,
// so teleports (core ports stragglers) cannot desync the clock.
class HeiganDanceMoveAction : public MovementAction
{
public:
    HeiganDanceMoveAction(PlayerbotAI* ai) : MovementAction(ai, "heigan dance move") {}
    bool Execute(Event& event) override;

private:
    int lastArea = -1;
    uint32 fallbackAnchorMs = 0;
};

// Dance suppression (donor HeiganDanceMultiplier, movement leg): while
// the dance is on, only the dance/platform moves reposition the bot —
// flee, spread, follow and any other movement would walk off the safe
// spot. Attacks, heals and dispels pass through untouched (no cast
// gating: our multiplier API cannot see remaining cast time, so timed
// casts may eat an eruption mid-dance — disclosed gap). Fail-open when
// the boss is off threat (no sweep).
class HeiganDanceSuppressionMultiplier : public Multiplier
{
public:
    HeiganDanceSuppressionMultiplier(PlayerbotAI* ai) : Multiplier(ai, "heigan dance suppression") {}
    float GetValue(Action* action) override;
};

// Fight phase: ranged/healers wait on Heigan's platform (floor sections
// erupt every 10s; the platform is not a floor section).
class HeiganHoldPlatformAction : public MovementAction
{
public:
    HeiganHoldPlatformAction(PlayerbotAI* ai) : MovementAction(ai, "heigan hold platform") {}
    bool Execute(Event& event) override;
};
}
