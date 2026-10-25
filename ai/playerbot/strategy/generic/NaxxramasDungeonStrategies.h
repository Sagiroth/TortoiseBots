#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class MaexxnaRescueStrategy : public Strategy
    {
    public:
        MaexxnaRescueStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "maexxna rescue"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class NaxxramasDungeonStrategy : public Strategy
    {
    public:
        NaxxramasDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "naxxramas"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class FourHorsemanFightStrategy : public Strategy
    {
    public:
        FourHorsemanFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "four horseman"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    };
}