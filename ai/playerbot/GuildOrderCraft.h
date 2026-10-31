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

// Casts affordable with what the bags hold: the minimum over reagents.
template <typename HasItem>
inline uint32 CraftAffordableCasts(SpellEntry const* pSpellInfo, uint32 casts, HasItem hasItem)
{
    if (!pSpellInfo)
        return 0;
    uint32 affordable = casts;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;
        affordable = std::min(affordable, TortoiseBots::CraftOrderPolicy::CastsAffordable(
            hasItem(pSpellInfo->Reagent[i]), pSpellInfo->ReagentCount[i]));
    }
    return affordable;
}

// First missing farmable reagent for a craft order: short of the casts
// needed, not sold unlimited by a vendor, and the planner can source it.
// Callbacks are injected so values, actions and order mode share the pick
// without duplicating the loop or dragging AI headers in here.
template <typename HasItem, typename StackSpace, typename VendorUnlimited, typename Sourcable>
inline uint32 FirstFarmableReagentFor(SpellEntry const* pSpellInfo, uint32 casts, uint32 affordable,
    HasItem hasItem, StackSpace stackSpace, VendorUnlimited vendorUnlimited, Sourcable sourcable)
{
    if (!pSpellInfo)
        return 0;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 reagentId = pSpellInfo->Reagent[i];
        if (vendorUnlimited(reagentId))
            continue;

        uint32 have = hasItem(reagentId);
        if (!TortoiseBots::CraftOrderPolicy::ReagentShortfall(pSpellInfo->ReagentCount[i], casts, have))
            continue;

        if (affordable >= 1 && stackSpace(reagentId) == 0)
            return 0;

        if (!sourcable(reagentId))
            continue;

        return reagentId;
    }
    return 0;
}

} // namespace ai
