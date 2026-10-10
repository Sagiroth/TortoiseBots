#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class AnubrekhanStartFightTrigger : public StartBossFightTrigger
    {
    public:
        AnubrekhanStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start anub'rekhan fight", "anub'rekhan", 15956) {}
    };

    class AnubrekhanEndFightTrigger : public EndBossFightTrigger
    {
    public:
        AnubrekhanEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end anub'rekhan fight", "anub'rekhan", 15956) {}
    };

    // A live Crypt Guard (16573) is engaged: non-tanks switch off boss.
    class AnubrekhanAddsTrigger : public Trigger
    {
    public:
        AnubrekhanAddsTrigger(PlayerbotAI* ai, std::string name = "anub'rekhan adds", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Locust Swarm (28785 self-buff on the boss): collapse to center.
    class AnubrekhanSwarmTrigger : public Trigger
    {
    public:
        AnubrekhanSwarmTrigger(PlayerbotAI* ai, std::string name = "anub'rekhan swarm", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };
}
