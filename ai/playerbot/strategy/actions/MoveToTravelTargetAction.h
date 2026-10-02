#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/LastMovementValue.h"

namespace ai
{
    class TravelTarget;

    class MoveToTravelTargetAction : public MovementAction {
    public:
        MoveToTravelTargetAction(PlayerbotAI* ai) : MovementAction(ai, "move to travel target") {}

        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

    private:
        // Stuck-hand-in fallback: a completed quest whose hand-in trip to its taker
        // keeps failing to move, for a pool bot with no real master. Pays the quest
        // out server-side (TalkToQuestGiverAction::RewardFinishedQuest) and parks
        // that quest's hand-in travel. Returns true when the quest was rewarded.
        bool TryAutoHandInUnreachableTaker(TravelTarget* target, std::string const& purpose, float distance);
    };

}
