#pragma once

#include <cstdint>

// Pure decision rule for the druid out-of-combat resurrection port (DRU-1):
// a vanilla druid has no normal resurrection spell, only Rebirth (30 min
// cooldown, usable in combat). Burning it out of combat is only right when
// nobody else in the group can do the job: a living priest, paladin or
// shaman (the three vanilla classes with a normal out-of-combat resurrect)
// is always preferred over a druid's battle rez.
//
// No core includes: callers in strategy/ translate game state into these
// plain inputs, so the rule stays testable in
// tools/test_ooc_rebirth_policy.cpp without the server.
//
// Donor: mod-playerbots `GenericDruidNonCombatStrategy.cpp:116-118`
// (`party member dead` -> `revive`), adapted: the donor's `revive` spell
// does not exist as a druid-taught spell in 1.18.1 (the only `Revive` row,
// 24341, is a Zul'Gurub boss spell with no trainer path; druid trainers
// teach Rebirth 20484+), so the port casts Rebirth out of combat instead —
// and only when no living groupmate of a resurrecting class can do it.
// Combat rebirth is unchanged (existing `rebirth` rows fire regardless).

namespace ai
{
    struct OocRebirthState
    {
        bool partyMemberDead;        // someone in the group needs a rez
        bool botKnowsRebirth;        // the druid has Rebirth trained
        bool botAlive;               // the druid itself is alive
        bool botInCombat;            // out-of-combat row: must be false
        bool livingResurrector;      // a living priest/paladin/shaman that
                                     // could actually rez: same map and in
                                     // spell range of the corpse
    };

    // True when the druid should burn Rebirth out of combat: someone is
    // dead, we know the spell and are alive and out of combat, and nobody
    // better (a living resurrecting class) can do it instead.
    inline bool ShouldCastOocRebirth(OocRebirthState const& state)
    {
        if (!state.partyMemberDead)
            return false;
        if (!state.botKnowsRebirth || !state.botAlive || state.botInCombat)
            return false;
        return !state.livingResurrector;
    }
}
