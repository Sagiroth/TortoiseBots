#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // "patchwerk main", "patchwerk soak" and "patchwerk melee safety": one
    // class, the role is its name.
    class PatchwerkAssignmentStrategy : public Strategy
    {
    public:
        PatchwerkAssignmentStrategy(PlayerbotAI* ai, std::string name) : Strategy(ai), name(name) {}
        std::string getName() override { return name; }
    private:
        std::string name;
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
