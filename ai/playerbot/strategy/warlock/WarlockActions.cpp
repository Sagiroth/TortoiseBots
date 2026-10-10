
#include "playerbot/playerbot.h"
#include "WarlockActions.h"
#include "playerbot/strategy/actions/EquipAction.h"

using namespace ai;

bool EquipFirestoneAction::Execute(Event& /*event*/)
{
    // Trigger already gates empty off-hand + one-handed main-hand; re-check
    // the slot here so a race between ticks can't displace worn gear.
    if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        return false;

    std::list<Item*> stones = AI_VALUE2(std::list<Item*>, "inventory items", "firestone");
    for (Item* stone : stones)
    {
        if (stone && !stone->IsEquipped())
        {
            EquipAction::EquipItem(ai, nullptr, stone, true);
            return true;
        }
    }

    return false;
}

bool ApplySpellstoneAction::isUseful()
{
    // Never burn a shard-made stone over an oiled weapon (mirrors the
    // trigger gate; the engine may evaluate either first).
    Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    return weapon && weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) == 0;
}

bool ApplySpellstoneAction::Execute(Event& event)
{
    Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!weapon || weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) != 0)
        return false;

    std::list<Item*> stones = AI_VALUE2(std::list<Item*>, "inventory items", "spellstone");
    for (Item* stone : stones)
    {
        if (stone && !stone->IsEquipped() && UseItem(event.GetOwner(), stone->GetProto()->ItemId, weapon))
            return true;
    }

    return false;
}
