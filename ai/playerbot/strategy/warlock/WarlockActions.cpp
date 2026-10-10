
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
            return bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) == stone;
        }
    }

    return false;
}

bool EquipSpellstoneAction::Execute(Event& /*event*/)
{
    // Same empty-off-hand re-check as firestone: never displace worn gear.
    if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        return false;

    std::list<Item*> stones = AI_VALUE2(std::list<Item*>, "inventory items", "spellstone");
    for (Item* stone : stones)
    {
        if (stone && !stone->IsEquipped())
        {
            EquipAction::EquipItem(ai, nullptr, stone, true);
            return bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) == stone;
        }
    }

    return false;
}
