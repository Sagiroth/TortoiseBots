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

        // Stuck-hand-in fallback, shared with the re-pick observation in
        // ChooseTravelTargetAction::setNewTarget: records one no-progress
        // episode for this quest's hand-in trip (a failed move toward the
        // taker, or the same taker picked again). Execute settles the trip off
        // these counters when the taker turns out not to be walkable to.
        static void CountHandInNoProgress(PlayerbotAI* ai, uint32 questId, int32 takerEntry);

    private:
        // A hand-in trip whose taker the bot cannot walk to is settled without
        // walking the last steps: one navmesh probe per bot, shared process-wide
        // (TakerReachabilityCache), or, when the probe says the taker is
        // walkable, the no-progress episode counters. Returns true when the
        // quest was rewarded.
        bool TrySettleUnreachableHandIn(TravelTarget* target, std::string const& purpose);

        // Pay the finished quest out without a talk and park that quest's
        // hand-in travel. `reason` ("nopath" when a navmesh probe settled it,
        // "unreachable" when the episode counters did) lands in QuestAutoHandIn.
        static bool SettleUnreachableTakerHandIn(PlayerbotAI* ai, uint32 questId, int32 takerEntry, std::string const& reason);
    };

}
