#pragma once

#include <cstdint>

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
}
