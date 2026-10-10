#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class LoathebStartFightTrigger : public StartBossFightTrigger
    {
    public:
        LoathebStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start loatheb fight", "loatheb", 16011) {}
    };

    class LoathebEndFightTrigger : public EndBossFightTrigger
    {
    public:
        LoathebEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end loatheb fight", "loatheb", 16011) {}
    };

    // Live spore (16286) within 1yd of the bot: take it for the bloom.
    class LoathebSporeTrigger : public Trigger
    {
    public:
        LoathebSporeTrigger(PlayerbotAI* ai, std::string name = "loatheb spore", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Out of position: tanks off the anchor or ranged off the line.
    class LoathebPositionTrigger : public Trigger
    {
    public:
        LoathebPositionTrigger(PlayerbotAI* ai, std::string name = "loatheb position", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };
}
