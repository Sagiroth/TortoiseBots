#pragma once
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/actions/ViscidusActions.h"

namespace ai
{
    class ViscidusCombatTrigger : public Trigger
    {
    public:
        explicit ViscidusCombatTrigger(PlayerbotAI* ai) : Trigger(ai, "viscidus combat needed", 1) {}
        bool IsActive() override;
    };
    class ViscidusToxinTrigger : public Trigger
    {
    public:
        explicit ViscidusToxinTrigger(PlayerbotAI* ai) : Trigger(ai, "viscidus toxin nearby", 1) {}
        bool IsActive() override { return InsideViscidusToxin(ai); }
    };
    class ViscidusDistanceTrigger : public Trigger
    {
    public:
        explicit ViscidusDistanceTrigger(PlayerbotAI* ai) : Trigger(ai, "viscidus melee danger", 1) {}
        bool IsActive() override { return NeedsViscidusDistance(ai); }
    };
    class ViscidusGlobStrategy : public Strategy
    {
    public:
        ViscidusGlobStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "viscidus glob focus"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
