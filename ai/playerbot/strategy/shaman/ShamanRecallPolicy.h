#pragma once

// Pure policy for out-of-combat Totemic Recall (mod-playerbots parity
// SHM-5): recall standing totems for mana when nothing is fighting, but
// never destroy the mana-tide cooldown totem for pennies. Recall needs the
// spell trained and at least one totem down.

namespace ai
{
    inline bool TotemicRecallShouldFire(bool hasSpell, bool anyTotemDown, bool manaTideDown,
        bool botInCombat, bool anyMemberInCombat)
    {
        if (!hasSpell || !anyTotemDown)
            return false;
        if (manaTideDown)
            return false;
        if (botInCombat || anyMemberInCombat)
            return false;
        return true;
    }
}
