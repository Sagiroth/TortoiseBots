#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/DungeonFollowStallPolicy.h"

namespace ai
{
	class FollowAction : public MovementAction {
	public:
		FollowAction(PlayerbotAI* ai, std::string name = "follow") : MovementAction(ai, name) {}
		virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool CanDeadFollow(Unit* target);

    private:
        // Dungeon "hold before a pack" follow (AiPlayerbot.DungeonFollowHoldBeforePacks).
        bool UseHoldBeforePacks(Unit* followTarget);
        bool FollowHoldingBeforePacks(Unit* followTarget, bool& moved);

        // Dungeon rescue of a core follow that stands still (DungeonFollowStallPolicy.h).
        bool ObserveDungeonFollowStall(Unit* followTarget);
        bool RescueStalledDungeonFollow(Unit* followTarget, bool& moved);
        DungeonFollowStallWatch followStallWatch;

        bool holdGateOpen = false;      // gate state, for the 30/25 yd hysteresis
        bool holdFollowActive = false;  // Execute ran the walk (reset the wait on leaving)
        bool holdStanding = false;      // standing at a hold point, nothing left to walk
        ObjectGuid holdToldPack;
        time_t holdToldAt = 0;
	};

	class StopFollowAction : public MovementAction {
	public:
		StopFollowAction(PlayerbotAI* ai, std::string name = "stop follow") : MovementAction(ai, name) {}
		virtual bool Execute(Event& event) override { ai->StopMoving(); return true; }
        virtual bool isUseful() override;
	};

    class FleeToMasterAction : public FollowAction {
    public:
        FleeToMasterAction(PlayerbotAI* ai) : FollowAction(ai, "flee to master") {}

        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

}
