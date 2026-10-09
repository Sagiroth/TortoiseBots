#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class GluthStartFightTrigger : public StartBossFightTrigger
    {
    public:
        GluthStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start gluth fight", "gluth", 15932) {}
    };

    class GluthEndFightTrigger : public EndBossFightTrigger
    {
    public:
        GluthEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end gluth fight", "gluth", 15932) {}
    };

    // Swap trigger: this bot tanks Gluth's target slot (current target is
    // Gluth) while another tank holds him with 5+ Mortal Wound (25646)
    // stacks. Fires on the off-tank only.
    class GluthMortalWoundSwapTrigger : public Trigger
    {
    public:
        GluthMortalWoundSwapTrigger(PlayerbotAI* ai, std::string name = "gluth mortal wound swap", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "current target"; }
        bool IsActive() override;
    };

    // Chow triage trigger: live Zombie Chow (16360) within 30yd for a
    // non-tank whose target is not already the execute chow.
    class GluthChowUpTrigger : public Trigger
    {
    public:
        GluthChowUpTrigger(PlayerbotAI* ai, std::string name = "gluth chow up", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };
}
