#pragma once

#include <cstdint>

// Pure decision rules for Onyxia Deep Breath safe-zone dodging
// (mod-playerbots parity, raid1 batch item 7).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/Ony/OnyTriggers.cpp
// (OnyxiaDeepBreathTrigger: boss casting one of 8 Breath ids) +
// src/Ai/Raid/Ony/OnyActions.h:52-93 (MoveToSafeZone: nearest of 2 safe
// zones per breath direction; already-safe bots hold).
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_onyxia_breath_policy.cpp.

namespace ai
{
    // Onyxia entry + the 8 directional Deep Breath spell ids (all 1.18.1
    // verified at port time — see provenance).
    constexpr std::uint32_t kOnyxiaEntry = 10184;

    inline bool IsOnyxiaBreathSpell(std::uint32_t spellId)
    {
        switch (spellId)
        {
            case 17086: // N to S
            case 18351: // S to N
            case 18576: // E to W
            case 18609: // W to E
            case 18564: // SE to NW
            case 18584: // NW to SE
            case 18596: // SW to NE
            case 18617: // NE to SW
                return true;
            default:
                return false;
        }
    }

    // Facing-to-axis: the core faces the breath destination, so boss
    // orientation selects the lane pair. 8 compass eighths fold into 4
    // axes (opposite directions share safe zones, as in the donor table).
    inline int BreathAxisFromFacing(float facing)
    {
        // 8 compass eighths → the donor's 4 lane pairs. Cardinal breaths
        // take the cardinal axis; diagonal breaths take their diagonal.
        const float kTwoPi = 6.2831853072f;
        const float kEighth = 0.7853981634f; // PI/4
        while (facing < 0.0f)
            facing += kTwoPi;
        while (facing >= kTwoPi)
            facing -= kTwoPi;
        int eighth = (int)(facing / kEighth + 0.5f) % 8;
        switch (eighth)
        {
            case 0: // N (+X)
            case 4: // S (-X)
                return 0;
            case 2: // W (+Y)
            case 6: // E (-Y)
                return 1;
            case 3: // SW
            case 7: // NE
                return 3;
            case 5: // SE
            case 1: // NW
            default:
                return 2;
        }
    }
    // Safe-zone pair per breath direction (donor coords, Onyxia's lair map).
    // Pair index: 0 = N-S axis, 1 = E-W axis, 2 = SE-NW axis, 3 = SW-NE axis.
    // Kept for documentation/tests; production maps facing via above.
    inline int BreathAxisIndex(std::uint32_t spellId)
    {
        switch (spellId)
        {
            case 17086:
            case 18351:
                return 0;
            case 18576:
            case 18609:
                return 1;
            case 18564:
            case 18584:
                return 2;
            case 18596:
            case 18617:
                return 3;
            default:
                return -1;
        }
    }

    // Safe-zone radius: already inside means hold (donor early-out).
    constexpr float kBreathSafeZoneRadius = 5.0f;

    inline bool ShouldMoveToBreathSafeZone(bool breathCasting, bool alreadySafe)
    {
        return breathCasting && !alreadySafe;
    }
}
