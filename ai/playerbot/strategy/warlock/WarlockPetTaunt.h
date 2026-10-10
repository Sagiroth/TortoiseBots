#pragma once

// PET-8: warlock-local taunt-permission read for the Suffering gate.
// Self-contained by design: the generic PET-3 helper
// (`IsPetTauntAllowed` in actions/GenericActions) lives on an unmerged
// branch, and each parity PR must compile standalone against the
// integration branch. Same rule (solo or tankless group may taunt;
// grouped with a real tank stands down) using the same `IsTank` read the
// rest of the AI uses. Unify with the generic helper at merge time.

#include "playerbot/PlayerbotAI.h"
#include "playerbot/GroupMembers.h"

namespace ai
{

inline bool WarlockPetTauntAllowed(PlayerbotAI* ai, Player* bot)
{
    (void)ai;
    // Solo: the Voidwalker is the tank. Grouped: look for any member
    // filling the tank role — real players via talents/forced role, bots
    // via tank strategy.
    if (!bot || !bot->GetGroup())
        return true;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member || member == bot)
            continue;
        if (PlayerbotAI::IsTank(member))
            return false;
    }
    return true;
}

} // namespace ai
