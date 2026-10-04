#pragma once

// Pure policy: which graveyard teleport targets are safe (task F, night
// 2026-10-03/04 pool: ~5/h wrong/far repops - a horde bot stuck in Teldrassil
// woke up in Booty Bay, Durotar bots in Dun Morogh, Dun Morogh bots at an
// invalid map-0 point).
//
// Root cause: the core sObjectMgr.GetClosestGraveYard lookup is zone-linked,
// not proximity-bounded. When no graveyard linked to the bot's zone shares
// its map and faction (bot in enemy territory, e.g. horde in Teldrassil -
// zone 141 links are 3x alliance + 1x horde, and the faction filter drops
// the rest), it returns entryFar: an arbitrary linked graveyard on another
// map (last one iterated). Same-map picks can also be thousands of yards
// away (15-17k yd jumps in the log). Teleporting there strands the bot.
//
// mod-playerbots (donor) trusts the same core closest-graveyard lookup and
// only falls back to racial start zones, so there is no donor guard to port:
// this rule is new. Callers own the core lookup; only the numbers travel
// here, so no core includes (mirrors LongStuckRescuePolicy.h).

namespace ai
{
    // A same-map graveyard teleport longer than this is not a repop, it is a
    // relocation: measured legit same-map graveyard landings top out at
    // ~1400 yd (p99 ~1100 yd), every same-map jump >= 12k yd in the night log
    // stranded the bot. 5000 yd keeps margin over the biggest legit zones
    // while rejecting the 12-17k yd outliers.
    float const GRAVEYARD_TELEPORT_MAX_DIST_YD = 5000.0f;

    // A core graveyard pick is only worth teleporting to when it shares the
    // bot's map and is reasonably near. Anything else must degrade to the
    // caller's homebind/spawn path, never to a cross-map safe loc.
    inline bool IsUsableGraveyardTarget(bool haveGraveyard, bool sameMap, float distYd)
    {
        return haveGraveyard && sameMap && distYd <= GRAVEYARD_TELEPORT_MAX_DIST_YD;
    }
}
