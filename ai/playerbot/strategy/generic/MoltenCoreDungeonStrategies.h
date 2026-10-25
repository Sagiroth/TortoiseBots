#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class GehennasRainStrategy : public Strategy
    {
    public:
        GehennasRainStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "gehennas rain"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class MoltenCoreDungeonStrategy : public Strategy
    {
    public:
        MoltenCoreDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "molten core"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class GarrFightStrategy : public Strategy
    {
    public:
        GarrFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "garr"; }

    private:
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class ShazzrahFightStrategy : public Strategy
    {
    public:
        ShazzrahFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "shazzrah"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class GolemaggFightStrategy : public Strategy
    {
    public:
        GolemaggFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "golemagg"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class GarrBossTankStrategy : public Strategy
    {
    public:
        GarrBossTankStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "garr boss tank"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class GarrAddTankStrategy : public GarrBossTankStrategy
    {
    public:
        GarrAddTankStrategy(PlayerbotAI* ai) : GarrBossTankStrategy(ai) {}
        std::string getName() override { return "garr add tank"; }
    };

    class GolemaggBossTankStrategy : public Strategy
    {
    public:
        GolemaggBossTankStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "golemagg boss tank"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class GolemaggAddTankStrategy : public GolemaggBossTankStrategy
    {
    public:
        GolemaggAddTankStrategy(PlayerbotAI* ai) : GolemaggBossTankStrategy(ai) {}
        std::string getName() override { return "golemagg add tank"; }
    };

    // Opt-in DPS exclusions; tanks and explicit targets keep their assignments.
    class GolemaggDpsStrategy : public Strategy
    {
    public:
        GolemaggDpsStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "golemagg dps"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class GarrDpsStrategy : public GolemaggDpsStrategy
    {
    public:
        GarrDpsStrategy(PlayerbotAI* ai) : GolemaggDpsStrategy(ai) {}
        std::string getName() override { return "garr dps"; }
    private:
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class MajordomoDpsStrategy : public GolemaggDpsStrategy
    {
    public:
        MajordomoDpsStrategy(PlayerbotAI* ai) : GolemaggDpsStrategy(ai) {}
        std::string getName() override { return "majordomo dps"; }
    };

    // Explicit opt-in via "all +geddon inferno"; never added by map entry.
    class GeddonInfernoStrategy : public Strategy
    {
    public:
        GeddonInfernoStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "geddon inferno"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class MagmadarFightStrategy : public Strategy
    {
    public:
        MagmadarFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "magmadar"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class GeddonFightStrategy : public Strategy
    {
    public:
        GeddonFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "geddon"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    };
    class RagnarosSonsStrategy : public Strategy
    {
    public:
        RagnarosSonsStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "ragnaros sons"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };


    class SulfuronInterruptStrategy : public Strategy
    {
    public:
        SulfuronInterruptStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "sulfuron interrupts"; }
    };
}
