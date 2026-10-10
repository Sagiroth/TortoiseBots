#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class RazuviousStartFightTrigger : public StartBossFightTrigger
    {
    public:
        RazuviousStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start razuvious fight", "razuvious", 16061) {}
    };

    class RazuviousEndFightTrigger : public EndBossFightTrigger
    {
    public:
        RazuviousEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end razuvious fight", "razuvious", 16061) {}
    };

    // One of the first two living priests in the group (slot order), or a
    // priest already holding an Understudy.
    class RazuviousMindControlTrigger : public Trigger
    {
    public:
        RazuviousMindControlTrigger(PlayerbotAI* ai) : Trigger(ai, "razuvious mind control", 1) {}
        bool IsActive() override;
    };
}
