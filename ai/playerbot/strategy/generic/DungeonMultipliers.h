#pragma once

#include "playerbot/strategy/Multiplier.h"

class Action;

namespace ai
{
    // Razorgore off-tank hold (mod-playerbots parity): while eggs live,
    // the designated off-tank keeps the boss and never follows tank-assist
    // retargets; after the eggs die, non-victim tanks don't face into Cleave.
    class RazorgoreOffTankMultiplier : public Multiplier
    {
    public:
        RazorgoreOffTankMultiplier(PlayerbotAI* ai) : Multiplier(ai, "razorgore off tank") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    class PreventMoveAwayFromCreatureOnReachToCastMultiplier : public Multiplier
    {
    public:
        PreventMoveAwayFromCreatureOnReachToCastMultiplier(PlayerbotAI* ai) : Multiplier(ai, "cast spell after reach") {}

    public:
        virtual float GetValue(Action* action) override;
    };
}
