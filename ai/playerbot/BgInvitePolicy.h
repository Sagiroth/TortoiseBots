#pragma once

#include <cstdint>

namespace ai
{
    // Battleground-invite polling fallback (bginvite).
    //
    // The live invite path is the SMSG_BATTLEFIELD_STATUS packet: the core
    // sets the per-queue-slot invited flag and the module's "bg status"
    // packet action ports the bot in. This trigger is the fallback for
    // invites whose status packet never reaches the AI tick (missed packet,
    // strategy churn): when any queue slot reports invited, the "default"
    // strategy queues a cheap "bg status check" (re-requests the status,
    // which re-emits the invite), so the normal port path still runs.
    //
    // Donor mod-playerbots reads the queue's GroupQueueInfo for the same
    // invite state (BgInviteActiveTrigger); here the caller passes the
    // already-known per-slot invited flags (Player::IsInvitedForBattleGroundQueueType),
    // so the rule stays a pure function with no core includes.
    //
    // Pure decision rule, no core includes: the caller (BgInviteActiveTrigger,
    // which already holds the queued/not-inside gate) passes the per-slot
    // invited flags, and the rule only says whether any slot is invited.

    // Invite verdict: true when at least one queued slot reports invited.
    // No slot invited (queued, waiting) stays false so the status re-request
    // only fires on a real invite, not every tick of every queued bot.
    inline bool BgInvitePending(bool invitedSlot0, bool invitedSlot1 = false,
        bool invitedSlot2 = false)
    {
        return invitedSlot0 || invitedSlot1 || invitedSlot2;
    }

    // Queue-slot count the rule covers (PLAYER_MAX_BATTLEGROUND_QUEUES = 3).
    inline std::int32_t BgInviteSlotCount() { return 3; }
}
