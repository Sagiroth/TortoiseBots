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

    // Adds up (phase 1, or phase-2 guardians): any of the four add
    // entries on the cached target lists.
    class KelthuzadAddsTrigger : public Trigger
    {
    public:
        KelthuzadAddsTrigger(PlayerbotAI* ai, std::string name = "kel'thuzad adds", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Phase one: KT present but unattackable (NOT_SELECTABLE set
    // until phase 2).
    class KelthuzadPhaseOneTrigger : public Trigger
    {
    public:
        KelthuzadPhaseOneTrigger(PlayerbotAI* ai, std::string name = "kel'thuzad phase one", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Phase two: KT attackable (NOT_SELECTABLE cleared at phase-2 start).
    class KelthuzadPhaseTwoTrigger : public Trigger
    {
    public:
        KelthuzadPhaseTwoTrigger(PlayerbotAI* ai, std::string name = "kel'thuzad phase two", int checkInterval = 3)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Shadow Fissure (16129) within 10yd of the bot (~5s lifetime:
    // 3s fuse + despawn, so a short hazard memory).
    class KelthuzadFissureTrigger : public CloseToCreatureHazardTrigger
    {
    public:
        KelthuzadFissureTrigger(PlayerbotAI* ai) : CloseToCreatureHazardTrigger(ai, "kel'thuzad fissure", 16129, 10.0f, 6) {}
    };
}
