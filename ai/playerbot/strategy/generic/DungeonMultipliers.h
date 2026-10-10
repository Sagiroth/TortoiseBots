#pragma once

#include "playerbot/strategy/Multiplier.h"

class Action;

namespace ai
{
    // Golemagg fight discipline (mod-playerbots parity): single-tank
    // groups skip the role dance; assist tanks never follow tank-assist
    // retargets; DPS AoE stays off; ranged never melee-fallbacks onto the
    // boss; backed-off melee stay out at 20+ splash stacks. The burn phase
    // (<10%) releases the AoE veto, the ranged melee-ban and the back-off
    // lock; the single-tank skip and assist veto persist.
    class GolemaggFightMultiplier : public Multiplier
    {
    public:
        GolemaggFightMultiplier(PlayerbotAI* ai) : Multiplier(ai, "golemagg fight") {}

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
}
