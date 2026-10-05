#pragma once
#include "playerbot/ServiceTripPolicy.h"
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

        // A service trip standing at its NPC is arrived even when the walk
        // point is not: banker/battlemaster halls from the room, counter
        // NPCs (auctioneer, vendor, trainer, mailbox) from twice
        // interaction range. Game-object mailboxes resolve by entry.
        // Returns true and flips the target to WORK.
        bool CheckServiceArrival(TravelTarget* target, std::string const& purpose);

        // Rescue for a service trip at the failure threshold: unwatched,
        // same-map, random masterless pool bot, once per 30 min. Teleports
        // to the exact destination point and logs ServiceTripTeleport.
        // Returns true when the rescue fired (the drop below is skipped).
        bool TryRescueServiceTrip(TravelTarget* target, std::string const& purpose);

        // Counts targets that fail NOPATH from one spot (TravelRepickPolicy.h)
        // and, at the threshold, runs the long-stuck rescue instead of
        // cycling targets from a spot nothing is reachable from. Returns true
        // when the rescue ran (the caller stops this tick).
        bool TryRescueNoPathTrap(bool noPath, std::string const& purpose);
    };

}
