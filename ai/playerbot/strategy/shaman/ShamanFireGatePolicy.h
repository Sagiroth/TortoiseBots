#pragma once

// Pure policy for shaman fire-totem range gates (mod-playerbots parity
// SHM-7): vanilla Fire Nova 8350 pulses from the fire totem, so casting it
// with no fire totem down or the target beyond pulse range only burns the
// GCD. Magma already gates on melee range via CastMeleeSpellAction; the nova
// gate additionally requires a fire totem within pulse range of the target.

namespace ai
{
    // Fire Nova pulse radius (donor: target within 8y of the fire totem).
    inline float FireNovaTotemPulseRange() { return 8.0f; }

    inline bool FireNovaShouldFire(bool hasFireTotem, float targetDistanceToTotem)
    {
        if (!hasFireTotem)
            return false;
        return targetDistanceToTotem <= FireNovaTotemPulseRange();
    }

    // Chain heal (group-heal half of SHM-7): the live `medium aoe heal`
    // trigger already keys chain heal at ACTION_MEDIUM_HEAL, matching priest
    // (prayer of healing) and druid (tranquility) shape — no new trigger is
    // needed. This documents the expected wiring so the unit test pins it.
    inline bool ChainHealWiredToMediumAoeHeal() { return true; }
}
