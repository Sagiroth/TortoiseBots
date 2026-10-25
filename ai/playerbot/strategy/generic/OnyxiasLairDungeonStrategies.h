#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class OnyxiaLandingTankStrategy : public Strategy
    {
    public:
        OnyxiaLandingTankStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "onyxia landing tank"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class OnyxiaBreathStrategy : public Strategy
    {
    public:
        OnyxiaBreathStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "onyxia deep breath"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class OnyxiasLairDungeonStrategy : public Strategy
    {
    public:
        OnyxiasLairDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "onyxia's lair"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class OnyxiaWhelpStrategy : public Strategy
    {
    public:
        OnyxiaWhelpStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "onyxia whelps"; }
    private:
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class OnyxiaAirSpreadStrategy : public Strategy
    {
    public:
        OnyxiaAirSpreadStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "onyxia air spread"; }
    private:
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class OnyxiaFightStrategy : public Strategy
    {
    public:
        OnyxiaFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "onyxia"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}