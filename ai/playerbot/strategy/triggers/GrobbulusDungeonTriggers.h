#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class GrobbulusStartFightTrigger : public StartBossFightTrigger
    {
    public:
        GrobbulusStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start grobbulus fight", "grobbulus", 15931) {}
    };

    class GrobbulusEndFightTrigger : public EndBossFightTrigger
    {
    public:
        GrobbulusEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end grobbulus fight", "grobbulus", 15931) {}
    };

    // Ranged injection carrier (28169 on a ranged bot): go behind boss.
    class GrobbulusInjectionRangedTrigger : public Trigger
    {
    public:
        GrobbulusInjectionRangedTrigger(PlayerbotAI* ai, std::string name = "grobbulus injection ranged", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Poison Cloud NPC (15933) within 10yd of a non-tank bot.
    class GrobbulusCloudTrigger : public CloseToCreatureHazardTrigger
    {
    public:
        GrobbulusCloudTrigger(PlayerbotAI* ai) : CloseToCreatureHazardTrigger(ai, "grobbulus cloud", 15933, 10.0f, 60) {}
    };
}
