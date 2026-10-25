#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class VaelastraszTankHandoverStrategy : public Strategy
    {
    public:
        VaelastraszTankHandoverStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "vael tank handover"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    // Manually enabled for the orb/add-control phase; automatically yields
    // when the core disables the Orb of Domination at the phase transition.
    class RazorgorePhaseOneStrategy : public Strategy
    {
    public:
        RazorgorePhaseOneStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "razorgore phase1"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class EbonrocSwapStrategy : public Strategy
    {
    public:
        EbonrocSwapStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "ebonroc swap"; }
    private:
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class BlackwingLairDungeonStrategy : public Strategy
    {
    public:
        BlackwingLairDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "blackwing lair"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void OnStrategyAdded(BotState state) override;
    };

    class ChromaggusFightStrategy : public Strategy
    {
    public:
        ChromaggusFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "chromaggus"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class RazorgoreFightStrategy : public Strategy
    {
    public:
        RazorgoreFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "razorgore"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class BroodlordFightStrategy : public Strategy
    {
    public:
        BroodlordFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "broodlord"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class NefarianFightStrategy : public Strategy
    {
    public:
        NefarianFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "nefarian"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class VaelFightStrategy : public Strategy
    {
    public:
        VaelFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "vael"; }

    private:
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    };


    class SuppressionRoomStrategy : public Strategy
    {
    public:
        SuppressionRoomStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "suppression room"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void OnStrategyAdded(BotState state) override;
    };
}
