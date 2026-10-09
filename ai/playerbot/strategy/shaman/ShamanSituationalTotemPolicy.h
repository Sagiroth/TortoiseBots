#pragma once
#include <cstdint>

// Pure policy for the shaman reactive situational totems (mod-playerbots
// parity SHM-2): tremor vs fear, grounding vs casters, poison/disease
// cleansing vs party debuffs, earthbind vs runners. All four are reactive
// swaps of the normal spec totem that must never fight an explicit player
// order (`totem <slot> <which>` manual strategy) and must not flap: they
// only fire when their slot currently holds no totem of that element.

namespace ai
{
    enum class SituationalTotemSlot : std::uint8_t
    {
        EARTH = 0,
        AIR = 1,
        WATER = 2
    };

    // Manual-order guard shared by all four swaps: an explicit player order
    // for this slot always wins, so automation stands down.
    inline bool SituationalTotemManualOverride(bool manualOrderForSlot)
    {
        return manualOrderForSlot;
    }

    // Tremor: anyone in our party (self included, group or solo) is feared,
    // charmed or asleep. Enemy fear-casting is covered too: if the current
    // target is casting and we have no tremor down, pre-dropping is cheap.
    inline bool TremorShouldDrop(bool partyFeared, bool targetFearCasting, bool earthSlotEmpty, bool manualEarthOrder)
    {
        if (manualEarthOrder || !earthSlotEmpty)
            return false;
        return partyFeared || targetFearCasting;
    }

    // Grounding: the current target is casting a spell right now. Grounded
    // absorbs one harmful cast, so only a live cast justifies stealing the
    // air slot from windfury/grace.
    inline bool GroundingShouldDrop(bool targetCasting, bool airSlotEmpty, bool manualAirOrder)
    {
        if (manualAirOrder || !airSlotEmpty)
            return false;
        return targetCasting;
    }

    // Cleansing: a party member (or self) carries a dispellable poison or
    // disease. The totem pulses the cleanse, so one afflicted member is
    // enough; the direct cure spells handle the single-target top-up.
    inline bool CleansingShouldDrop(bool partyPoisoned, bool partyDiseased, bool waterSlotEmpty, bool manualWaterOrder)
    {
        if (manualWaterOrder || !waterSlotEmpty)
            return false;
        return partyPoisoned || partyDiseased;
    }

    // Earthbind: the current target is fleeing (running away at low health
    // or feared movement) and still alive. The slow keeps it in kill range;
    // a feared runner is covered by tremor first, earthbind only if tremor
    // is already down or not the pick.
    inline bool EarthbindShouldDrop(bool targetFleeing, bool earthSlotEmpty, bool manualEarthOrder)
    {
        if (manualEarthOrder || !earthSlotEmpty)
            return false;
        return targetFleeing;
    }

    // Earth-slot arbitration between the two reactive earth picks: tremor
    // (protects the party) outranks earthbind (holds one runner).
    inline bool TremorOutranksEarthbind() { return true; }
}
