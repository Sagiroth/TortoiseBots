
#include "playerbot/playerbot.h"
#include "playerbot/TrinketUsePolicy.h"
#include "UseTrinketAction.h"
#include "Objects/Item.h"
#include "Objects/ItemPrototype.h"
#include "Objects/Player.h"

using namespace ai;

namespace
{
    bool IsManaRestoreEffect1_12(SpellEntry const* spellInfo, uint32 effect)
    {
        if (spellInfo->Effect[effect] == SPELL_EFFECT_ENERGIZE &&
            spellInfo->EffectMiscValue[effect] == POWER_MANA)
            return true;
        return spellInfo->Effect[effect] == SPELL_EFFECT_APPLY_AURA &&
            spellInfo->EffectApplyAuraName[effect] == SPELL_AURA_PERIODIC_ENERGIZE &&
            spellInfo->EffectMiscValue[effect] == POWER_MANA;
    }

    bool IsManaEfficiencyEffect1_12(SpellEntry const* spellInfo, uint32 effect)
    {
        if (spellInfo->Effect[effect] != SPELL_EFFECT_APPLY_AURA)
            return false;
        uint32 aura = spellInfo->EffectApplyAuraName[effect];
        if ((aura == SPELL_AURA_MOD_POWER_REGEN || aura == SPELL_AURA_MOD_POWER_REGEN_PERCENT) &&
            spellInfo->EffectMiscValue[effect] == POWER_MANA)
            return true;
        return aura == SPELL_AURA_MOD_POWER_COST_SCHOOL ||
            aura == SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT ||
            aura == SPELL_AURA_MOD_MANA_REGEN_INTERRUPT;
    }

    bool IsDefensiveTankEffect1_12(SpellEntry const* spellInfo, uint32 effect)
    {
        if (spellInfo->Effect[effect] != SPELL_EFFECT_APPLY_AURA)
            return false;
        switch (spellInfo->EffectApplyAuraName[effect])
        {
            case SPELL_AURA_MOD_RESISTANCE:
                // Matches the donor: armor counts, other schools do not.
                return (spellInfo->EffectMiscValue[effect] & SPELL_SCHOOL_MASK_NORMAL) != 0;
            case SPELL_AURA_MOD_INCREASE_HEALTH:
            case SPELL_AURA_MOD_PARRY_PERCENT:
            case SPELL_AURA_MOD_DODGE_PERCENT:
            case SPELL_AURA_MOD_BLOCK_PERCENT:
            case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
                return true;
            default:
                return false;
        }
    }
} // namespace

TrinketEffectClass UseTrinketAction::ClassifyTrinketSpell(SpellEntry const* spellInfo)
{
    bool aura = false, restore = false, efficiency = false, defensive = false;
    for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
    {
        if (!spellInfo->Effect[effect])
            continue;
        if (spellInfo->Effect[effect] == SPELL_EFFECT_APPLY_AURA)
            aura = true;
        restore = restore || IsManaRestoreEffect1_12(spellInfo, effect);
        efficiency = efficiency || IsManaEfficiencyEffect1_12(spellInfo, effect);
        defensive = defensive || IsDefensiveTankEffect1_12(spellInfo, effect);
    }
    // Priority mirrors the donor gates: restore first (mana gate below),
    // then defensive (health gate), then plain aura. Efficiency alone never
    // fires (donor aura-or-restore-only gate) and maps to None.
    if (restore)
        return TrinketEffectClass::ManaRestore;
    if (defensive)
        return TrinketEffectClass::Defensive;
    if (aura)
        return TrinketEffectClass::Aura;
    return TrinketEffectClass::None;
}

bool UseTrinketAction::UseTrinket(Player* requester, Item* item)
{
    const ItemPrototype* proto = item->GetProto();
    if (proto->InventoryType != INVTYPE_TRINKET || !item->IsEquipped())
        return false;
    if (bot->CanUseItem(item) != EQUIP_ERR_OK || item->IsInTrade())
        return false;

    uint32 spellId = 0;
    int32 itemCooldown = 0;
    uint32 itemCategory = 0;
    int32 itemCategoryCooldown = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE && proto->Spells[i].SpellId > 0)
        {
            spellId = proto->Spells[i].SpellId;
            itemCooldown = proto->Spells[i].SpellCooldown;
            itemCategory = proto->Spells[i].SpellCategory;
            itemCategoryCooldown = proto->Spells[i].SpellCategoryCooldown;
            break;
        }
    }
    if (!spellId)
        return false;

    // Donor per-item + per-category cooldown memory (DB values are seconds).
    uint32 nowMs = WorldTimer::getMSTime();
    std::pair<uint32, uint32> itemKey(item->GetEntry(), spellId);
    auto itemIt = itemCooldownExpiries.find(itemKey);
    bool itemCooling = itemIt != itemCooldownExpiries.end() &&
        (itemIt->second > nowMs ? true : (itemCooldownExpiries.erase(itemIt), false));
    auto catIt = categoryCooldownExpiries.find(itemCategory);
    bool catCooling = itemCategory && itemCategoryCooldown > 0 && catIt != categoryCooldownExpiries.end() &&
        (catIt->second > nowMs ? true : (categoryCooldownExpiries.erase(catIt), false));
    if (!TrinketCooldownAllowed(itemCooling, catCooling))
        return false;

    SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!spellInfo || !TrinketSpellAllowed(spellInfo->IsPositiveSpell()))
        return false;

    TrinketEffectClass effect = ClassifyTrinketSpell(spellInfo);
    if (!TrinketEffectAllowed(effect))
        return false;

    // Donor mana/health gates. Efficiency never reaches here (classified
    // None above); the efficiency branch of the policy covers future use.
    uint8 manaPct = AI_VALUE2(uint8, "mana", "self target");
    bool hasMana = AI_VALUE2(bool, "has mana", "self target");
    if (!TrinketManaAllowed(effect, hasMana, manaPct, sPlayerbotAIConfig.mediumMana))
        return false;
    uint8 healthPct = AI_VALUE2(uint8, "health", "self target");
    if (!TrinketHealthAllowed(effect, healthPct, sPlayerbotAIConfig.lowHealth))
        return false;

    if (!UseItem(requester, item->GetEntry()))
        return false;

    if (itemCooldown > 0)
        itemCooldownExpiries[itemKey] = nowMs + uint32(itemCooldown) * IN_MILLISECONDS;
    if (itemCategory && itemCategoryCooldown > 0)
        categoryCooldownExpiries[itemCategory] = nowMs + uint32(itemCategoryCooldown) * IN_MILLISECONDS;
    return true;
}

bool UseTrinketAction::Execute(Event& event)
{
	Player* requester = event.GetOwner();
	std::list<Item*> trinkets = AI_VALUE(std::list<Item*>, "trinkets on use");
	for (Item* item : trinkets)
	{
		if (UseTrinket(requester, item))
			return true;
	}

    return false;
}

bool UseTrinketAction::isPossible()
{
	return !AI_VALUE(std::list<Item*>, "trinkets on use").empty();
}
