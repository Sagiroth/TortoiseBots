#pragma once

#include <cmath>
#include <cstdint>
#include <string>

// Pure policy for city-service errands (AH, vendor, repair, mail, trainer,
// city): the trips that kept dying en route instead of finishing.
//
    // 1) Arrival: a service NPC inside interaction range counts as arrived, even
    //    when the randomized walk point sits inside geometry (live: Orgrimmar
    //    bots stalled 5-8 yd from the auctioneers, path "incomplete"). The
    //    arrival radius varies by NPC kind: a banker/battlemaster hall counts
    //    from the room (60 yd), a counter NPC from twice interaction range.
// 2) Rescue: when a service trip hits the real failure threshold (the same
//    IsMaxRetry that drops it) and nobody can see either end, the bot is
//    teleported to the exact destination point instead of dropped + parked.
//    Conservative by construction: service purposes only, same map only,
//    random masterless pool bots only, watched-from-either-end veto, one
//    rescue per bot per 30 min, logged as ServiceTripTeleport.
// 3) City gating: the "city" leisure trip needs real capital-service
//    business (an AH sale to post or an AH purchase the purse covers).
//    Every other service errand already has a need gate (vendor/repair/mail
//    in NeedTravelPurposeValue); city was the ungated one.
//
// The world-facing parts (NPC lookup, TeleportTo, HasPlayerNearby, logEvent,
// manual values) live in MoveToTravelTargetAction / TravelValues; the
// decisions below are pure so they can be tested on their own.

namespace ai
{
    // Service purposes, by TravelDestinationPurpose bit (TravelValues.h):
    // Trainer 1<<7, Repair 1<<8, Vendor 1<<9, AH 1<<10, Mail 1<<11.
    // Quest/grind/gather/explore/boss are deliberately not service.
    inline constexpr uint32_t SERVICE_TRIP_PURPOSE_BITS =
        (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11);

    // Hall NPCs are arrived at from the room, counter NPCs from twice
    // interaction range: the old per-tick jitter added up to radiusMin (5 yd)
    // onto the walk point, so a bot standing 5-8 yd from the spawn is as
    // close as jittered walking gets - the service actions close the last
    // yards themselves via MoveNear / nearby-service.
    inline constexpr float SERVICE_TRIP_COUNTER_ARRIVAL_YD = 10.0f;
    inline constexpr float SERVICE_TRIP_HALL_ARRIVAL_YD = 60.0f;

    // One rescue per bot per 30 min: a trip that needs rescuing twice in a
    // row is broken, not stuck, and must park like any other drop.
    inline constexpr int SERVICE_TRIP_RESCUE_COOLDOWN_SEC = 30 * 60;

    // Numeric purpose id ("1024") or named purpose ("city", "trainer class",
    // "trainer trade", "trainer mount", "trainer pet", "mount", "reagent
    // vendor"). The named trainer/vendor requests carry a real need of their
    // own (trainable spells, mount, reagents), so they count as service.
    // Anything else with a letter in it (pvp, guild meeting, guild order,
    // tabard, petition) walks a route the owner must see on foot: out of
    // scope for the rescue.
    inline bool ServiceTripIsServicePurpose(std::string const& purpose)
    {
        if (purpose == "city" || purpose.compare(0, 7, "trainer") == 0 ||
            purpose == "mount" || purpose == "reagent vendor")
            return true;
        if (purpose.empty())
            return false;
        for (char c : purpose)
            if (c < '0' || c > '9')
                return false;
        return (std::stoul(purpose) & SERVICE_TRIP_PURPOSE_BITS) != 0;
    }

    // Arrival radius for a service NPC by its npc_flags (UNIT_NPC_FLAG_*).
    // Banker/battlemaster halls are rooms; everything else (auctioneer,
    // vendor, trainer, mailbox) is a counter. Flags arrive as 0 for an
    // unknown entry or a game object, which correctly yields the counter.
    inline float ServiceTripArrivalRadius(uint32_t npcFlags)
    {
        constexpr uint32_t UNIT_NPC_FLAG_BANKER = 0x00000100;
        constexpr uint32_t UNIT_NPC_FLAG_BATTLEMASTER = 0x00000800;
        if (npcFlags & (UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_BATTLEMASTER))
            return SERVICE_TRIP_HALL_ARRIVAL_YD;
        return SERVICE_TRIP_COUNTER_ARRIVAL_YD;
    }

    // Stable per-(bot, destination) approach offset inside `radius`. The old
    // per-tick urand re-rolled the walk point every re-entry, so a bot whose
    // point landed inside the counter never converged; the donor
    // (mod-playerbots MoveToTravelTargetAction, stable angle/mod per pair)
    // keeps one approach per pair instead. Deterministic for the same
    // inputs, bounded by `radius`, never zero.
    inline void ServiceTripStableOffset(uint32_t botGuidLow, int32_t entry,
        float pointX, float pointY, float radius, float& dx, float& dy)
    {
        int32_t seed = (int32_t)botGuidLow ^
            (int32_t)(pointX * 64.0f) * 73856093 ^
            (int32_t)(pointY * 64.0f) * 19349663 ^
            entry * 83492791;
        uint32_t h = (uint32_t)seed;
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        float angle = 2.0f * 3.14159265f * (float)(h % 1000) / 1000.0f;
        float mod = 0.5f + (float)((h / 1000) % 1000) / 2000.0f;
        dx = std::cos(angle) * radius * mod;
        dy = std::sin(angle) * radius * mod;
    }

    // Rescue gate: every clause must hold. `busy` folds combat / taxi /
    // teleport-in-flight; `watched` folds players near either end.
    inline bool ServiceTripRescueEligible(bool atMaxRetry, bool randomMasterless,
        bool alive, bool busy, bool sameMap, bool watched, bool coolingDown, bool forced)
    {
        return atMaxRetry && randomMasterless && alive && !busy &&
            sameMap && !watched && !coolingDown && !forced;
    }
}
