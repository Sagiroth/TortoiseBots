#pragma once

#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "runtime/policy/provision/CraftOrderPolicy.h"

class Player;
struct SpellEntry;

namespace ai
{

// Shared craft-order spell helpers: one spell scan and one cast count for
// the values and the actions, so shortfalls never disagree on the yield.
inline uint32 FindCraftSpellFor(Player* bot, uint32 itemId)
{
    for (auto& [spellId, spellState] : bot->GetSpellMap())
    {
        if (spellState.state == PLAYERSPELL_REMOVED || spellState.disabled || IsPassiveSpell(spellId))
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
            continue;

        for (int i = 0; i < 3; ++i)
        {
            if (pSpellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && pSpellInfo->EffectItemType[i] == itemId)
                return spellId;
        }
    }

    return 0;
}

inline uint32 FindCraftEffectFor(uint32 spellId, uint32 itemId)
{
    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!pSpellInfo)
        return 0;

    for (int i = 0; i < 3; ++i)
    {
        if (pSpellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && pSpellInfo->EffectItemType[i] == itemId)
            return i;
    }

    return 0;
}

// Casts needed for `remaining` more items at the spell's guaranteed yield.
inline uint32 CraftCastsForRemaining(uint32 spellId, uint32 itemId, uint32 remaining)
{
    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!pSpellInfo)
        return remaining;

    uint32 effect = FindCraftEffectFor(spellId, itemId);
    uint32 yield = TortoiseBots::CraftOrderPolicy::MinYieldPerCast(
        pSpellInfo->EffectBasePoints[effect], pSpellInfo->EffectBaseDice[effect],
        pSpellInfo->EffectDieSides[effect]);
    return TortoiseBots::CraftOrderPolicy::CastsForRemaining(remaining, yield);
}

} // namespace ai
