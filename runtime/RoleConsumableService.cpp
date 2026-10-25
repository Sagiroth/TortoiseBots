#include "RoleConsumableService.h"
#include "HireLifecycle.h"
#include "../ai/playerbot/playerbot.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "Bag.h"
#include "ObjectMgr.h"

namespace TortoiseBots
{
bool RoleConsumablesEnabled()
{
    return sPlayerbotAIConfig.roleConsumablesEnabled || sPlayerbotAIConfig.roleConsumablesAutoProvision;
}
bool RoleConsumablesManaged(PlayerbotAI* ai)
{
    if (!RoleConsumablesEnabled() || !ai || !ai->HasRealPlayerMaster()) return false;
    Player* bot = ai->GetBot();
    Player* master = ai->GetMaster();
    return bot && master && master != bot && !bot->InBattleGround() && bot->GetGroup() && bot->GetGroup() == master->GetGroup();
}
bool RoleConsumableUnlocked(PlayerbotAI* ai, ConsumableCategory category)
{
    if (!RoleConsumablesManaged(ai)) return false;
    bool const provision = sPlayerbotAIConfig.roleConsumablesAutoProvision &&
        HireLifecycle::Instance().IsHired(ai->GetBot()->GetObjectGuid());
    Player* master = ai->GetMaster();
    auto rewarded = [master](uint32 id) { return id && master->GetQuestRewardStatus(id); };
    return ConsumableUnlocked(category, provision,
        rewarded(sPlayerbotAIConfig.roleConsumablesPotionQuest),
        rewarded(sPlayerbotAIConfig.roleConsumablesElixirQuest),
        rewarded(sPlayerbotAIConfig.roleConsumablesFlaskQuest));
}
unsigned RoleConsumableRole(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (ai->IsTank(bot)) return Tank;
    if (ai->IsHeal(bot)) return Healer;
    // Hunters are ranged physical; feral druids and enhancement shamans are melee.
    return bot->GetClass() != CLASS_HUNTER && ai->IsRanged(bot) ? Caster : Physical;
}
Item* FindRoleConsumable(Player* bot, uint32 entry)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (item->GetEntry() == entry && !item->IsInTrade()) return item;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* items = dynamic_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < items->GetBagSize(); ++slot)
                if (Item* item = items->GetItemByPos(slot))
                    if (item->GetEntry() == entry && !item->IsInTrade()) return item;
    return nullptr;
}
bool HasConsumableBuff(Player* player, ConsumableCategory category)
{
    // Fixed catalogue lookups, never a world/item-template scan. Observe the
    // master's successful buff rather than an attempted or failed item click.
    for (auto const& entry : RoleConsumables)
    {
        if (entry.category != category) continue;
        auto const* proto = sObjectMgr.GetItemPrototype(entry.item);
        if (!proto) continue;
        for (auto const& use : proto->Spells)
        {
            auto const* spell = sSpellTemplate.LookupEntry<SpellEntry>(use.SpellId);
            if (!spell) continue;
            if (player->HasAura(spell->Id)) return true;
            for (uint32 trigger : spell->EffectTriggerSpell)
                if (trigger && player->HasAura(trigger)) return true;
        }
    }
    return false;
}
void TopUpRoleConsumables(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.roleConsumablesAutoProvision || !RoleConsumablesManaged(ai)) return;
    Player* bot = ai->GetBot();
    if (!HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) || !bot->IsAlive() ||
        !bot->IsInWorld() || bot->IsInCombat() || bot->IsBeingTeleported() ||
        bot->IsTaxiFlying() || bot->IsNonMeleeSpellCasted(false) || bot->GetTradeData()) return;
    unsigned filled = 0;
    unsigned const role = RoleConsumableRole(ai);
    for (auto const& entry : RoleConsumables)
    {
        // Food follows the master from existing bags, but is not part of the free kit.
        if (entry.category == ConsumableCategory::Food) continue;
        unsigned const bit = 1u << unsigned(entry.category);
        if ((filled & bit) || !(entry.roles & role)) continue;
        if (entry.category == ConsumableCategory::Mana && !bot->GetMaxPower(POWER_MANA)) continue;
        auto const* proto = sObjectMgr.GetItemPrototype(entry.item);
        if (!proto || bot->CanUseItem(proto) != EQUIP_ERR_OK || !proto->Spells[0].SpellId ||
            !sSpellTemplate.LookupEntry<SpellEntry>(proto->Spells[0].SpellId)) continue;
        // A full bag must not cause lower-tier items to be generated instead.
        filled |= bit;
        uint32 const count = ConsumableTopUp(entry.category, bot->GetItemCount(entry.item, true), proto->GetMaxStackSize());
        if (!count) continue;
        ItemPosCountVec dest;
        if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry.item, count) == EQUIP_ERR_OK)
            bot->StoreNewItem(dest, entry.item, true);
    }
}
void UpdateRoleConsumableProvision(PlayerbotAI* ai, uint32 elapsed, ConsumableProvisionClock& clock)
{
    if (!sPlayerbotAIConfig.roleConsumablesAutoProvision) return;
    Player* bot = ai->GetBot();
    if (!clock.Tick(bot->IsAlive(), elapsed)) return;
    // Login, resurrection and first completed hire are retried at a bounded pace
    // until safe. The same idempotent top-up is used by companion restocking.
    clock.remaining = 5000;
    if (!RoleConsumablesManaged(ai) || !bot->IsInWorld() || bot->IsInCombat() ||
        bot->IsBeingTeleported() || bot->IsTaxiFlying() || bot->IsNonMeleeSpellCasted(false) || bot->GetTradeData()) return;
    TopUpRoleConsumables(ai);
    clock.remaining = 300000;
}
}
