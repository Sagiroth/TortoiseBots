#include "../runtime/policy/infra/EquipSlotPolicy.h"

#include <cassert>
#include <iostream>

using namespace TortoiseBots;
using namespace TortoiseBots::EquipSlotConstants;

static void TestParseEquipTargetSlot()
{
    uint8_t slot = 0;

    assert(ParseEquipTargetSlot("mainhand", slot) && slot == kSlotMainHand);
    assert(ParseEquipTargetSlot("MainHand", slot) && slot == kSlotMainHand);
    assert(ParseEquipTargetSlot("offhand", slot) && slot == kSlotOffHand);
    assert(ParseEquipTargetSlot("OFFHAND", slot) && slot == kSlotOffHand);
    assert(ParseEquipTargetSlot("finger1", slot) && slot == kSlotFinger1);
    assert(ParseEquipTargetSlot("finger2", slot) && slot == kSlotFinger2);
    assert(ParseEquipTargetSlot("trinket1", slot) && slot == kSlotTrinket1);
    assert(ParseEquipTargetSlot("trinket2", slot) && slot == kSlotTrinket2);

    assert(!ParseEquipTargetSlot("", slot));
    assert(!ParseEquipTargetSlot("head", slot));
    assert(!ParseEquipTargetSlot("finger3", slot));
    assert(!ParseEquipTargetSlot("trinket0", slot));
}

static void TestValidateEquipTargetSlot()
{
    std::string_view err;

    // 1H weapon: main hand always allowed; off hand only when dual wield is learned.
    assert(ValidateEquipTargetSlot(kInvTypeWeapon, kSlotMainHand, false, err));
    assert(ValidateEquipTargetSlot(kInvTypeWeapon, kSlotMainHand, true, err));
    assert(!ValidateEquipTargetSlot(kInvTypeWeapon, kSlotOffHand, false, err));
    assert(err == "This bot cannot dual wield.");
    assert(ValidateEquipTargetSlot(kInvTypeWeapon, kSlotOffHand, true, err));

    // 2H weapon and main-hand-only weapon: main hand only.
    assert(ValidateEquipTargetSlot(kInvType2HWeapon, kSlotMainHand, true, err));
    assert(!ValidateEquipTargetSlot(kInvType2HWeapon, kSlotOffHand, true, err));
    assert(err == "That item can only be equipped in the main hand.");
    assert(ValidateEquipTargetSlot(kInvTypeWeaponMainHand, kSlotMainHand, false, err));
    assert(!ValidateEquipTargetSlot(kInvTypeWeaponMainHand, kSlotOffHand, true, err));

    // Off-hand-only, shield, holdable: off hand only.
    assert(ValidateEquipTargetSlot(kInvTypeWeaponOffHand, kSlotOffHand, false, err));
    assert(ValidateEquipTargetSlot(kInvTypeShield, kSlotOffHand, false, err));
    assert(ValidateEquipTargetSlot(kInvTypeHoldable, kSlotOffHand, false, err));
    assert(!ValidateEquipTargetSlot(kInvTypeWeaponOffHand, kSlotMainHand, true, err));
    assert(err == "That item can only be equipped in the off hand.");
    assert(!ValidateEquipTargetSlot(kInvTypeShield, kSlotMainHand, true, err));
    assert(!ValidateEquipTargetSlot(kInvTypeHoldable, kSlotMainHand, true, err));

    // Rings and trinkets.
    assert(ValidateEquipTargetSlot(kInvTypeFinger, kSlotFinger1, false, err));
    assert(ValidateEquipTargetSlot(kInvTypeFinger, kSlotFinger2, false, err));
    assert(!ValidateEquipTargetSlot(kInvTypeFinger, kSlotTrinket1, false, err));
    assert(!ValidateEquipTargetSlot(kInvTypeTrinket, kSlotFinger1, false, err));
    assert(ValidateEquipTargetSlot(kInvTypeTrinket, kSlotTrinket1, false, err));
    assert(ValidateEquipTargetSlot(kInvTypeTrinket, kSlotTrinket2, false, err));
    assert(!ValidateEquipTargetSlot(kInvTypeWeapon, kSlotFinger1, true, err));
    assert(!ValidateEquipTargetSlot(kInvTypeWeapon, kSlotTrinket1, true, err));
}

int main()
{
    TestParseEquipTargetSlot();
    TestValidateEquipTargetSlot();
    std::cout << "test_equip_slot_policy: all tests passed.\n";
    return 0;
}
