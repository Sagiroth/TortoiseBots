#pragma once
#include <cstdint>

// Pure policy for the shaman reactive situational totems (SHM-2): tremor vs
// fear/charm, grounding vs casts aimed at the party, poison/disease
// cleansing vs party debuffs, earthbind vs runners. All are reactive swaps
// of the normal spec totem that must never fight an explicit player order
// (`totem <slot> <which>` manual strategy) and must not flap: they only
// fire when their slot currently holds no totem of that element.
//
// NOTE: this is new automation, not a donor port — mod-playerbots picks
// situational totems manually per fight (per-slot strategies) and carries
// `TODO: tremor totem` in its dungeon code. The donor refs in PROVENANCE
// are the slot-maintenance triggers and isUseful fallbacks reused here.

namespace ai
{
    enum class SituationalTotemSlot : std::uint8_t
    {
        EARTH = 0,
        AIR = 1,
        WATER = 2
    };

    // Manual-order guard shared by all swaps: an explicit player order for
    // this slot always wins, so automation stands down.
    inline bool SituationalTotemManualOverride(bool manualOrderForSlot)
    {
        return manualOrderForSlot;
    }

    // Tremor: anyone in our party (self included, group or solo) is feared
    // or charmed. Fear + charm only: tremor dispels charm/fear/sleep
    // mechanics (effect 8146), never confuse/polymorph.
    inline bool TremorShouldDrop(bool partyFearedOrCharmed, bool earthSlotEmpty, bool manualEarthOrder)
    {
        if (manualEarthOrder || !earthSlotEmpty)
            return false;
        return partyFearedOrCharmed;
    }

    // Grounding: the current target is casting at us or a party member.
    // Grounding cannot redirect AoE, self-buffs or casts aimed elsewhere,
    // so only a cast aimed at the party justifies stealing the air slot.
    inline bool GroundingShouldDrop(bool castAimedAtParty, bool airSlotEmpty, bool manualAirOrder)
    {
        if (manualAirOrder || !airSlotEmpty)
            return false;
        return castAimedAtParty;
    }

    // Poison cleansing: a party member (or self) carries a dispellable
    // poison. 1.12 has no "Cleansing Totem" spell — only the poison /
    // disease pair, each with its own action.
    inline bool PoisonCleansingShouldDrop(bool partyPoisoned, bool waterSlotEmpty, bool manualWaterOrder)
    {
        if (manualWaterOrder || !waterSlotEmpty)
            return false;
        return partyPoisoned;
    }

    // Disease cleansing: same shape as the poison twin.
    inline bool DiseaseCleansingShouldDrop(bool partyDiseased, bool waterSlotEmpty, bool manualWaterOrder)
    {
        if (manualWaterOrder || !waterSlotEmpty)
            return false;
        return partyDiseased;
    }

    // Earthbind: the current target is fleeing (running mob, fear break) and
    // still alive, or a party member is snared and needs the slow.
    inline bool EarthbindShouldDrop(bool targetFleeing, bool earthSlotEmpty, bool manualEarthOrder)
    {
        if (manualEarthOrder || !earthSlotEmpty)
            return false;
        return targetFleeing;
    }
}
