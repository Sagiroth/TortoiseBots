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

    // True when this bot's ordered-pull hold has run its course: an anchor
    // copy ("pull hold") is set and the join window has expired. A hold whose
    // wait strategy was dropped early is already gone (PlayerbotAI::
    // ChangeStrategy releases it with the strategy), so it never gets here.
    class PullHoldExpiredTrigger : public Trigger
    {
    public:
        PullHoldExpiredTrigger(PlayerbotAI* ai, std::string name = "pull hold expired") : Trigger(ai, name) {}
        bool IsActive() override;
    };

    // True when the anchor hold the puller itself was parked on (a pullback
    // parks the tank at the anchor for the fight) is over: the pulled fight is
    // finished, so the tank resumes following the party. Held DPS bots carry
    // the same anchor marker but run their own wait window instead
    // ("pull hold expired"). PlayerbotAI::OnCombatEnded is the same release at
    // the engine switch, where this trigger no longer exists.
    class PullAnchorDoneTrigger : public Trigger
    {
    public:
        PullAnchorDoneTrigger(PlayerbotAI* ai) : Trigger(ai, "pull anchor done") {}
        bool IsActive() override;
    };
}
