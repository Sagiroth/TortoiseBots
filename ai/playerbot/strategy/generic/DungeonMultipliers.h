#pragma once

#include "playerbot/strategy/Multiplier.h"

class Action;

namespace ai
{
    // Garr AoE-off (mod-playerbots parity): while Garr lives, DPS-bot
    // AoE actions are vetoed so Firesworn adds are not splashed.
    class GarrAoeOffMultiplier : public Multiplier
    {
    public:
        GarrAoeOffMultiplier(PlayerbotAI* ai) : Multiplier(ai, "garr aoe off") {}

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
