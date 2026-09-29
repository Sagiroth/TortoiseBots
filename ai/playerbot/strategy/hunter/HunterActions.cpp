
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/HunterRangedTelemetry.h"
#include "HunterActions.h"

using namespace ai;

bool ai::HunterHasLoadedRangedWeapon(PlayerbotAI* playerbotAi)
{
    if (!playerbotAi)
        return false;

    Player* bot = playerbotAi->GetBot();
    if (!bot)
        return false;

    Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!ranged)
        return false;

    if (ranged->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_THROWN)
        return true;

    return HunterHasRangedAmmo(playerbotAi);
}

bool CastSerpentStingAction::isUseful()
{
    return HunterHasRangedAmmo(ai) && CastRangedDebuffSpellAction::isUseful() && AI_VALUE2(uint8, "health", GetTargetName()) > 50 && !(AI_VALUE2(uint8, "mana", GetTargetName()) >= 10);
}

bool CastViperStingAction::isUseful()
{
    return HunterHasRangedAmmo(ai) && CastRangedDebuffSpellAction::isUseful() && AI_VALUE2(uint8, "mana", GetTargetName()) >= 10;
}

bool FeedPetAction::Execute(Event& event)
{
    Pet* pet = bot->GetPet();
    if (pet && pet->getPetType() == HUNTER_PET && pet->GetHappinessState() != HAPPY)
        pet->SetPower(POWER_HAPPINESS, HAPPINESS_LEVEL_SIZE * 2);

    return true;
}

bool CastAutoShotAction::isUseful()
{
    if (!ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT))
        return false;
    if (!HunterHasRangedAmmo(ai))
        return false;
    return AI_VALUE(uint32, "active spell") != AI_VALUE2(uint32, "spell id", getName());
}

bool HunterEquipAmmoAction::Execute(Event& event)
{
    // Get ranged weapon
    Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!ranged)
        return false;

    uint32 ammoClass = ITEM_CLASS_PROJECTILE;
    uint32 subClass = 0;

    switch (ranged->GetProto()->SubClass)
    {
    case ITEM_SUBCLASS_WEAPON_GUN:
        subClass = ITEM_SUBCLASS_BULLET;
        break;
    case ITEM_SUBCLASS_WEAPON_BOW:
    case ITEM_SUBCLASS_WEAPON_CROSSBOW:
        subClass = ITEM_SUBCLASS_ARROW;
        break;
    case ITEM_SUBCLASS_WEAPON_THROWN:
        ammoClass = ITEM_CLASS_WEAPON;
        subClass = ITEM_SUBCLASS_WEAPON_THROWN;
        break;
    }

    uint32 currentAmmoId = bot->GetUInt32Value(PLAYER_AMMO_ID);
    const ItemPrototype* bestAmmoProto = nullptr;
    auto checkAmmo = [&](Item* item)
    {
        if (!item)
            return;
        const ItemPrototype* proto = item->GetProto();
        if (!proto)
            return;
        if (proto->Class == ammoClass && proto->SubClass == subClass)
            if (!bestAmmoProto || proto->ItemLevel > bestAmmoProto->ItemLevel)
                bestAmmoProto = proto;
    };

    // Main 16-slot backpack (bag 0, slots 23..38): InitAmmo stores fresh
    // stacks here, so ignoring it left the action blind (Issue #219).
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        checkAmmo(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    // Equipped bags (slots 19..22)
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                checkAmmo(bag->GetItemByPos(j));
        }
    }

    // Equip best ammo if not already equipped
    if (bestAmmoProto && currentAmmoId != bestAmmoProto->ItemId)
    {
        bot->SetUInt32Value(PLAYER_AMMO_ID, bestAmmoProto->ItemId);
        bot->UpdateDamagePhysical(RANGED_ATTACK);
        return true;
    }

    return false;
}

bool CastAutoShotAction::Execute(Event& event)
{
    if (!bot->IsStopped())
    {
        ai->StopMoving();
    }

    bool const executed = CastSpellAction::Execute(event);

    // Hunter ranged telemetry: one row per (re)start of the ranged auto-attack
    // (the core keeps the shot repeating on its own); see HunterRangedTelemetry.h.
    LogHunterRangedEvent(ai, "AutoShot", std::string("cast=") + (executed ? "1" : "0"));

    return executed;
}
