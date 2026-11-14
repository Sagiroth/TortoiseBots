
#include "playerbot/playerbot.h"
#include "WarlockActions.h"
#include "WarlockTriggers.h"
#include "../generic/ClassChoicePolicy.h"
#include "playerbot/strategy/actions/EquipAction.h"
#include "../../../runtime/policy/class/WarlockStonePolicy.h"

using namespace ai;

bool CastChoicePetAction::LoadOrder()
{
    std::string const word = OrderedPetWord(ai);
    if (word.empty())
        return false;
    SetSpellName(ChoiceSpell(CHOICE_PET, word));
    return true;
}

bool CastChoicePetAction::isUseful()
{
    if (!LoadOrder() || sServerFacade.IsInCombat(bot))
        return false;
    Unit* pet = AI_VALUE(Unit*, "pet target");
    return !pet || pet->GetEntry() != ChoicePetEntry(OrderedPetWord(ai));
}

bool CastChoicePetAction::isPossible()
{
    return LoadOrder() && CastSpellAction::isPossible();
}

bool CastChoicePetAction::Execute(Event& event)
{
    return LoadOrder() && CastSpellAction::Execute(event);
}

bool EquipFirestoneAction::Execute(Event& /*event*/)
{
    // Trigger already gates empty off-hand + one-handed main-hand; re-check
    // the slot here so a race between ticks can't displace worn gear.
    if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        return false;

    std::list<Item*> stones = AI_VALUE2(std::list<Item*>, "inventory items", "firestone");
    for (Item* stone : stones)
    {
        // Only a holdable stone: Turtle's bag-consumable Spellstone and
        // Firestone share the names but cannot be equipped.
        if (stone && !stone->IsEquipped() &&
            TortoiseBots::IsEquippableWarlockStone(stone->GetProto()->InventoryType))
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
        // Only a holdable stone: Turtle's bag-consumable Spellstone and
        // Firestone share the names but cannot be equipped.
        if (stone && !stone->IsEquipped() &&
            TortoiseBots::IsEquippableWarlockStone(stone->GetProto()->InventoryType))
        {
            EquipAction::EquipItem(ai, nullptr, stone, true);
            return bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) == stone;
        }
    }

    return false;
}
