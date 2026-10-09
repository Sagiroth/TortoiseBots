#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class KelthuzadStartFightTrigger : public StartBossFightTrigger
    {
    public:
        KelthuzadStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start kel'thuzad fight", "kel'thuzad", 15990) {}
    };

    class KelthuzadEndFightTrigger : public EndBossFightTrigger
    {
    public:
        KelthuzadEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end kel'thuzad fight", "kel'thuzad", 15990) {}
    };

    // Adds up (phase 1, or phase-2 guardians): any of the five entries
    // engaged within 40yd of the center.
    class KelthuzadAddsTrigger : public Trigger
    {
    public:
        KelthuzadAddsTrigger(PlayerbotAI* ai, std::string name = "kel'thuzad adds", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Phase two: KT attackable (NOT_SELECTABLE cleared at phase-2 start).
    class KelthuzadPhaseTwoTrigger : public Trigger
    {
    public:
        KelthuzadPhaseTwoTrigger(PlayerbotAI* ai, std::string name = "kel'thuzad phase two", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Shadow Fissure (16129) within 10yd of the bot.
    class KelthuzadFissureTrigger : public CloseToCreatureHazardTrigger
    {
    public:
        KelthuzadFissureTrigger(PlayerbotAI* ai) : CloseToCreatureHazardTrigger(ai, "kel'thuzad fissure", 16129, 10.0f, 20) {}
    };
}
