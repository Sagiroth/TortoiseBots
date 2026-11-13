#pragma once

#include <cstdint>
#include <ctime>

// Zone migration for pool bots (leave-outgrown-zone grind errand).
//
// What was broken: the leave rule (`ShouldLeaveOutgrownZoneValue`) fires on
// the bot's *sub-area* level (`TryGetValidatedAreaLevel` of the bot's area
// id - e.g. Galwurth fired 3x at level 10.59 standing in a low Durotar
// sub-area while Durotar's zone average is 8), but the request floor
// (botLevel - 5, a zone-level test) still admitted the old zone - and
// `SetBestTarget` walks partitions near to far and takes the first active
// point, i.e. the nearest one, i.e. home. So every fire re-picked Durotar
// and the bot never reached the Barrens.
//
// The fix has two halves: the search floor (unchanged, still botLevel - 5)
// plus excluding the zone being left, by zone id. The trigger already
// vetted outgrownness (same +5 shape, or capital-idle), so the only pure
// decision left is *which* zone to exclude: the bot's current zone, unless
// the leave reason is "capital" (capitals hold trainers/AH/bank - same
// exemption as the outgrown service gate in `RpgTravelDestination`
// `::IsPossible`, which keeps capital vendors for outleveled bots).
// Unknown bot zones (id 0) exclude nothing - fail open, like today.
//
// Donor (mod-playerbots, gold standard) comparison: the donor never migrates
// random bots on foot - it teleports them (`RandomTeleportForLevel`,
// `RandomPlayerbotMgr.cpp:1783`) into level-bracketed zones
// (`zone2LevelBracket`: starters {5,12}, Darkshore/Barrens/Loch
// Modan/Westfall/Silverpine {10,20..25}). Ours migrates organically over the
// travel graph (no teleports), so the bracket membership the donor gets from
// the teleport table has to live in the destination gates instead: the
// trigger's +5 shape plays the bracket edge, and this exclusion plays the
// "don't re-pick the old bracket" half.

namespace ai
{
    // Zone id the leave-errand Grind search must not pick in, or 0 for no
    // exclusion. `botZoneId` is the bot's current zone id (sub-areas resolve
    // to their parent zone at the call site); `botInCapital` mirrors the
    // LeaveOutgrownZone log reason (`HasAreaFlag(AREA_FLAG_CAPITAL)`).
    inline std::uint32_t ZoneMigrationExcludeZone(std::uint32_t botZoneId, bool botInCapital)
    {
        if (botInCapital)
            return 0;

        return botZoneId;
    }

    // Outgrown-zone floor for ordinary Grind/Gather picks (task E): a pool
    // bot that outlevels a zone (validated zone level + 5 < bot level) must
    // not re-pick Grind/Gather points inside it. Donor mod-playerbots
    // `zone2LevelBracket` (src/Mgr/Travel/TravelMgr.cpp:4561: starters
    // {5,12}, next zones {10,20..25}) drops a level-12 bot out of the
    // starter bracket into the next one; here the validated zone level
    // plays the bracket edge and this refusal plays the "don't re-pick the
    // old bracket" half. Same +5 shape as the leave rule so the two cannot
    // drift, and the area ceiling above still bounds every pick, so an
    // undergeared bot is never pushed anywhere it cannot survive.
    // Pool bots level 11+ only: levels 1-10 never reach the gate below, so
    // starter behaviour is unchanged, and owned/hired bots keep player
    // control. Unknown point zones (id 0) and unvalidated levels (<= 0)
    // fail open, like today. Quest objectives are NOT gated here: the
    // fetch only offers quests already in the log (class quests cannot be
    // dropped), so starving them would brick the bot. Gather nodes stay
    // while the bot's skill cannot gather the next tier (Tin/Briarthorn
    // req 65) - enforced at the call site, not here.
    inline bool OutgrownZoneRefusesPoint(std::int32_t pointZoneLevel, std::uint32_t botLevel,
        bool masterlessRandom)
    {
        return masterlessRandom && botLevel > 10 && pointZoneLevel > 0 &&
            pointZoneLevel + 5 < (std::int32_t)botLevel;
    }

    // Lowest skill that gathers the next-zone tier (Tin Vein / Briarthorn:
    // req 65). A pool bot below this keeps its starter Copper/Earthroot
    // (req 1) even when outleveled, or it would have nowhere to skill up.
    inline constexpr std::uint32_t OUTGROWN_GATHER_NEXT_TIER_SKILL = 65;

    // Targetless leave. A pool bot with no travel target runs
    // `reset travel target` every few seconds while every request row is
    // parked, skip-rolled or yields to a need whose own trip never starts
    // (a hand-in or capital service the bot cannot reach). Live m910q
    // 2026-10-12: 72 runs of 3+ min on land, 353 bot-min in 28 min, 33 of
    // them in capitals. The leave rule waited for those needs, so nothing
    // moved the bot. Donor mod-playerbots `getNewTarget`
    // (ChooseTravelTargetAction.cpp:163-175) always ends in a grind target
    // before it idles. Here, two minutes of back-to-back resets turn the
    // leave rule on: the bot grinds in a zone that fits its level.
    //
    // A stretch is one run of resets less than a minute apart. A longer gap
    // means the bot did something else in between (a trip, a fight), so the
    // stretch starts over. A real pick ends it (caller clears `since`).
    constexpr std::time_t TARGETLESS_LEAVE_AFTER_SECONDS = 2 * 60;
    constexpr std::time_t TARGETLESS_RESET_GAP_SECONDS = 60;
    // Once due, the leave rule stays on until the bot's next real pick (the
    // resets in between would otherwise drop it). This only bounds that
    // wait, for a bot whose Grind search keeps coming back empty (same
    // 10 min as the grind-empty-streak clause). It never runs during a trip.
    constexpr std::time_t TARGETLESS_LEAVE_LATCH_SECONDS = 10 * 60;

    // Start of the targetless stretch after a reset at `now`. `since` and
    // `lastReset` are 0 when unset.
    inline std::time_t TargetlessSinceAfterReset(std::time_t since, std::time_t lastReset, std::time_t now)
    {
        if (since == 0 || lastReset == 0 || since > now || now - lastReset > TARGETLESS_RESET_GAP_SECONDS)
            return now;
        return since;
    }

    // Has the stretch lasted long enough to leave?
    inline bool TargetlessLeaveDue(std::time_t since, std::time_t now)
    {
        return since != 0 && now >= since && now - since >= TARGETLESS_LEAVE_AFTER_SECONDS;
    }

    // Does a pick start a targetless-leave trip? Only the Grind the leave
    // rule asked for while the leave was pending. The leave rule is that
    // trip's stored condition, so it stays on for exactly that trip: while
    // the bot walks to the spot and works it, however long the walk takes.
    // When the trip ends (expired, cooled down, reset, another pick) the
    // rule falls back to its ordinary tests, so a zone that fits the bot is
    // never left because of an old targetless stretch.
    inline bool TargetlessLeaveTripStarts(bool pickIsNull, bool pickIsGrind, bool leaveCondition, bool leavePending)
    {
        return !pickIsNull && pickIsGrind && leaveCondition && leavePending;
    }

    // Level-fitting teleport (login scatter, alt start, post-rez rescue):
    // the zone must sit within +-5 of the bot's level (clamped to 1..60) and
    // must not be a capital. Capitals carry a nominal zone level of 10, so
    // without the capital veto a level 10-15 bot lands among service NPCs
    // instead of in a grind zone.
    inline bool LevelFittingTeleportAllowsZone(std::int32_t zoneLevel, std::int32_t botLevel, bool zoneIsCapital)
    {
        if (zoneIsCapital)
            return false;
        std::int32_t const lower = botLevel - 5 < 1 ? 1 : botLevel - 5;
        std::int32_t const upper = botLevel + 5 > 60 ? 60 : botLevel + 5;
        return zoneLevel >= lower && zoneLevel <= upper;
    }

    // Zones a bot never picks to level in (teleport, grind, roaming, gathering),
    // whatever their level; quests, trainers and services there stay open.
    // Moonglade (493): its sub-areas carry level 15 in the client data, but it
    // has nothing to fight for a levelling bot.
    inline bool IsNoLevellingZone(std::uint32_t zoneId)
    {
        return zoneId == 493;
    }
}
