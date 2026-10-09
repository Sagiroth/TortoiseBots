#include "CombatStrategy.h"
#include "playerbot/strategy/generic/CombatStrategy.h"
#pragma once

namespace ai
{
    class MeleeCombatStrategy : public CombatStrategy
    {
    public:
        MeleeCombatStrategy(PlayerbotAI* ai) : CombatStrategy(ai) {}

        virtual int GetType() override { return STRATEGY_TYPE_COMBAT | STRATEGY_TYPE_MELEE; }
        uint32 GetType() const override { return STRATEGY_TYPE_COMBAT | STRATEGY_TYPE_MELEE; }
        virtual std::string getName() override { return "close"; }

    protected:
        virtual void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class SetBehindCombatStrategy : public CombatStrategy
    {
    public:
        SetBehindCombatStrategy(PlayerbotAI* ai) : CombatStrategy(ai) {}
        std::string getName() override { return "behind"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*> &triggers) override;
    };

    // Tank-face as a real strategy (donor TankFaceStrategy shape): default
    // action "tank face away" at ACTION_MOVE, triggerless — the action
    // self-gates via TankFaceNeededTrigger's hysteresis math in isUseful.
    // Membership replaces the old real-player-master gate: any tank with
    // this strategy faces held mobs away from the party.
    class TankFaceStrategy : public CombatStrategy
    {
    public:
        TankFaceStrategy(PlayerbotAI* ai) : CombatStrategy(ai) {}
        std::string getName() override { return "tank face"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*> &triggers) override;
    };

    class ChaseJumpStrategy : public Strategy
    {
    public:
        ChaseJumpStrategy(PlayerbotAI* ai) : Strategy(ai) {};
        std::string getName() override { return "chase jump"; }
#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "chase jump"; } //Must equal iternal name
        virtual std::string GetHelpDescription() {
            return "This strategy makes bot jump when chasing enemies they can't reach.\n";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return { "melee" }; }
#endif
    private:
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    };
}

using ai::MeleeCombatStrategy;
