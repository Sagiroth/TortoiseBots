#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
	class PullStartTrigger : public Trigger
	{
	public:
		PullStartTrigger(PlayerbotAI* ai, std::string name = "pull start") : Trigger(ai, name) {}
		bool IsActive() override;
	};

    // True when a tank in a dungeon group should start the next fight itself.
    class ShouldPullTrigger : public Trigger
    {
    public:
        ShouldPullTrigger(PlayerbotAI* ai) : Trigger(ai, "should pull", 5) {}

        bool IsActive() override;
    };

    class PullEndTrigger : public Trigger
    {
    public:
        PullEndTrigger(PlayerbotAI* ai, std::string name = "pull end") : Trigger(ai, name) {}
        bool IsActive() override;
    };
}
