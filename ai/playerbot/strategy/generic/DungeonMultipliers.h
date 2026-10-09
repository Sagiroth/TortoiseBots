#pragma once

#include "playerbot/strategy/Multiplier.h"

class Action;

namespace ai
{
    class PreventMoveAwayFromCreatureOnReachToCastMultiplier : public Multiplier
    {
    public:
        PreventMoveAwayFromCreatureOnReachToCastMultiplier(PlayerbotAI* ai) : Multiplier(ai, "cast spell after reach") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    // Baron Geddon Inferno (mod-playerbots parity): while Geddon carries
    // the Inferno aura (or the bot carries Living Bomb), only the two
    // survival runouts may move the bot.
    class GeddonInfernoMultiplier : public Multiplier
    {
    public:
        GeddonInfernoMultiplier(PlayerbotAI* ai) : Multiplier(ai, "geddon inferno") {}

    public:
        virtual float GetValue(Action* action) override;
    };
