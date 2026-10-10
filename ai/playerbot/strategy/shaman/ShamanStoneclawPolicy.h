#pragma once

// Pure policy for the shaman Stoneclaw panic totem (mod-playerbots parity
// SHM-3): the donor drops Stoneclaw at low health as a solo panic button
// (`low health -> stoneclaw totem`, solo-only isUseful guard). Ours has the
// action and the manual `totem earth stoneclaw` strategy but no live trigger
// queues it, so solo elemental/restoration bots never use it.

namespace ai
{
    // Solo-only Stoneclaw gate: in a group the earth slot belongs to the
    // spec totem (strength of earth), so the panic drop must not steal it.
    // Manual override (`totem earth stoneclaw` strategy) keeps precedence:
    // it means the player explicitly picked Stoneclaw for this fight. Any
    // OTHER explicit earth order (`totem earth strength/stoneskin/earthbind/
    // tremor`) always wins over the panic drop, even solo — otherwise the
    // panic would replace the ordered totem at low health.
    inline bool StoneclawPanicShouldDrop(bool lowHealth, bool inGroup, bool manualStoneclawStrategy, bool otherManualEarthOrder)
    {
        if (!lowHealth)
            return false;
        if (otherManualEarthOrder)
            return false;
        if (manualStoneclawStrategy)
            return true;
        return !inGroup;
    }
}
