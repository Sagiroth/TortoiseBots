#pragma once

namespace ai
{
    // A bot in the heal role never spends a GCD on a damage item: grenades,
    // sapper charges and Oil of Immolation pull threat onto the healer and
    // delay the next heal. Damage dealers and tanks keep them.
    inline bool DamageItemAllowed(bool isHealer) { return !isHealer; }
}
