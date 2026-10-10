#pragma once
#include "playerbot/strategy/Multiplier.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // mod-playerbots FocusMultiplier (LD-3): single-target burn for marked /
    // CC'd packs — vetoes every AoE action (heals exempt) and debuffs aimed
    // at arbitrary attackers, so nothing splashes onto crowd control.
    class FocusMultiplier : public Multiplier
    {
    public:
        FocusMultiplier(PlayerbotAI* ai) : Multiplier(ai, "focus") {}

    public:
        float GetValue(Action* action) override;
    };

    class FocusStrategy : public Strategy
    {
    public:
        FocusStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "focus"; }

    private:
        void InitCombatMultipliers(std::list<Multiplier*> &multipliers) override;
    };
}
