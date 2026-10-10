#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class HeiganStartFightTrigger : public StartBossFightTrigger
    {
    public:
        HeiganStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start heigan fight", "heigan", 15936) {}
    };

    class HeiganEndFightTrigger : public EndBossFightTrigger
    {
    public:
        HeiganEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end heigan fight", "heigan", 15936) {}
    };

    // Dance phase: Heigan carries Plague Cloud (29350, self-cast at dance
    // start). Per-bot observable — no shared clock needed.
    class HeiganDanceTrigger : public Trigger
    {
    public:
        HeiganDanceTrigger(PlayerbotAI* ai, std::string name = "heigan dance", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Fight phase for ranged: dance off and bot is ranged/healer.
    class HeiganPlatformHoldTrigger : public Trigger
    {
    public:
        HeiganPlatformHoldTrigger(PlayerbotAI* ai, std::string name = "heigan platform hold", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };
}
