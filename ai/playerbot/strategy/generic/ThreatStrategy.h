#pragma once
#include "playerbot/strategy/Multiplier.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // Whether AiPlayerbot.DungeonRunThreatResetWindow holds this damage dealer's damage on the target now: the boss's
    // row carries a reset window, the window is open and the bot's threat is at its stop line (advances the bot's
    // ThreatResetLatch). Read by ThreatMultiplier and the `threat reset white stop` trigger.
    bool ThreatResetHolds(PlayerbotAI* ai, Player* bot, Unit* target);

    class ThreatMultiplier : public Multiplier
    {
    public:
        ThreatMultiplier(PlayerbotAI* ai) : Multiplier(ai, "threat") {}

    public:
        float GetValue(Action* action) override;
    };

    class ThreatStrategy : public Strategy
    {
    public:
        ThreatStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "threat"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*> &triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*> &multipliers) override;
    };
}
