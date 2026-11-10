#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class PetitionTurnInTrigger : public Trigger {
    public:
        PetitionTurnInTrigger(PlayerbotAI* ai) :
            Trigger(ai, "petition turn in trigger", 5) {}

        bool IsActive() override { return AI_VALUE(bool, "can hand in petition"); };
    };

    class BuyTabardTrigger : public Trigger {
    public:
        BuyTabardTrigger(PlayerbotAI* ai) :
            Trigger(ai, "buy tabard trigger", 5) {}

        bool IsActive() override { return AI_VALUE(bool, "can buy tabard"); };
    };

    class LeaveLargeGuildTrigger : public Trigger {
    public:
        LeaveLargeGuildTrigger(PlayerbotAI* ai) :
            Trigger(ai, "leave large guild trigger", 10) {}

        bool IsActive();
    };

    // Every tick while a guild craft order sits on its craft step: the bot
    // crafts the batch in one go instead of one cast per few seconds with
    // travel walking it off (and breaking the cast) in between.
    class GuildCraftStepTrigger : public Trigger {
    public:
        GuildCraftStepTrigger(PlayerbotAI* ai) :
            Trigger(ai, "guild craft step", 1) {}

        bool IsActive() override;
    };
}