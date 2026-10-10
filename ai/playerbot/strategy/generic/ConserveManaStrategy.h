#pragma once
#include "playerbot/strategy/Multiplier.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class ConserveManaMultiplier : public Multiplier
    {
    public:
        ConserveManaMultiplier(PlayerbotAI* ai) : Multiplier(ai, "conserve mana") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    class SaveManaMultiplier : public Multiplier
    {
    public:
        SaveManaMultiplier(PlayerbotAI* ai) : Multiplier(ai, "save mana") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    class ConserveManaStrategy : public Strategy
    {
    public:
        ConserveManaStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "conserve mana"; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "conserve mana"; } //Must equal iternal name
        virtual std::string GetHelpDescription() {
            return "This strategy will make bots wait longer between casting the same spell twice.\n"
                   "the delay is based on [h:value|mana save level].";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return { }; }
#endif
    private:
        void InitCombatMultipliers(std::list<Multiplier*> &multipliers) override;
    };

    // Force-rebuff pass strategy (mod-playerbots parity, BUFF-1/BUFF-2):
    // while a rebuff window is pending out of combat, heals yield to buffs
    // (donor ForceRebuffBuffFirst multiplier) so the pass is not stretched
    // by interleaved heals. Registered in StrategyContext, on by default in
    // the non-combat set; disable with `.bot nc -force rebuff`.
    class ForceRebuffBuffFirstMultiplier : public Multiplier
    {
    public:
        ForceRebuffBuffFirstMultiplier(PlayerbotAI* ai) : Multiplier(ai, "force rebuff buff first") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    class ForceRebuffStrategy : public Strategy
    {
    public:
        ForceRebuffStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "force rebuff"; }

    private:
        void InitNonCombatMultipliers(std::list<Multiplier*> &multipliers) override;
    };
}
