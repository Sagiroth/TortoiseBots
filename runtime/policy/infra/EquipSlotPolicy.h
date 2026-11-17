#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace EquipSlotConstants
{
    constexpr uint8_t kSlotFinger1 = 10;
    constexpr uint8_t kSlotFinger2 = 11;
    constexpr uint8_t kSlotTrinket1 = 12;
    constexpr uint8_t kSlotTrinket2 = 13;
    constexpr uint8_t kSlotMainHand = 15;
    constexpr uint8_t kSlotOffHand = 16;

    constexpr uint32_t kInvTypeFinger = 11;
    constexpr uint32_t kInvTypeTrinket = 12;
    constexpr uint32_t kInvTypeWeapon = 13;
    constexpr uint32_t kInvTypeShield = 14;
    constexpr uint32_t kInvType2HWeapon = 17;
    constexpr uint32_t kInvTypeWeaponMainHand = 21;
    constexpr uint32_t kInvTypeWeaponOffHand = 22;
    constexpr uint32_t kInvTypeHoldable = 23;
}

inline bool ParseEquipTargetSlot(std::string_view token, uint8_t& outSlot)
{
    std::string lower;
    lower.reserve(token.size());
    for (char c : token)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower == "mainhand")
    {
        outSlot = EquipSlotConstants::kSlotMainHand;
        return true;
    }
    if (lower == "offhand")
    {
        outSlot = EquipSlotConstants::kSlotOffHand;
        return true;
    }
    if (lower == "finger1")
    {
        outSlot = EquipSlotConstants::kSlotFinger1;
        return true;
    }
    if (lower == "finger2")
    {
        outSlot = EquipSlotConstants::kSlotFinger2;
        return true;
    }
    if (lower == "trinket1")
    {
        outSlot = EquipSlotConstants::kSlotTrinket1;
        return true;
    }
    if (lower == "trinket2")
    {
        outSlot = EquipSlotConstants::kSlotTrinket2;
        return true;
    }
    return false;
}

inline bool ValidateEquipTargetSlot(uint32_t invType, uint8_t targetSlot, bool canDualWield, std::string_view& outError)
{
    using namespace EquipSlotConstants;

    if (targetSlot == kSlotMainHand)
    {
        if (invType == kInvTypeWeapon || invType == kInvType2HWeapon || invType == kInvTypeWeaponMainHand)
            return true;
        if (invType == kInvTypeWeaponOffHand || invType == kInvTypeShield || invType == kInvTypeHoldable)
            outError = "That item can only be equipped in the off hand.";
        else
            outError = "That item cannot be equipped in the main hand.";
        return false;
    }

    if (targetSlot == kSlotOffHand)
    {
        if (invType == kInvTypeWeapon)
        {
            if (!canDualWield)
            {
                outError = "This bot cannot dual wield.";
                return false;
            }
            return true;
        }
        if (invType == kInvTypeWeaponOffHand || invType == kInvTypeShield || invType == kInvTypeHoldable)
            return true;
        if (invType == kInvType2HWeapon || invType == kInvTypeWeaponMainHand)
            outError = "That item can only be equipped in the main hand.";
        else
            outError = "That item cannot be equipped in the off hand.";
        return false;
    }

    if (targetSlot == kSlotFinger1 || targetSlot == kSlotFinger2)
    {
        if (invType == kInvTypeFinger)
            return true;
        outError = "That item is not a ring.";
        return false;
    }

    if (targetSlot == kSlotTrinket1 || targetSlot == kSlotTrinket2)
    {
        if (invType == kInvTypeTrinket)
            return true;
        outError = "That item is not a trinket.";
        return false;
    }

    outError = "Invalid target slot.";
    return false;
}
}
