#pragma once

#include "GuildOrderContract.h"

#include <cstdint>

// Whether a known recipe may be ordered as Craft: the bot can really make
// it and the player can really use it. One shared rule for the Professions
// view (the RECIPE order field), `.bot order <bot> craft` give and the
// craft search verdict, so chat, search and the panel never disagree.
// Unit-tested on its own (tools/test_craft_orderable_policy.cpp). All inputs
// are plain caller-resolved facts, so this header stays free of core types.

namespace TortoiseBots
{
namespace GuildOrders
{

// A recipe cooldown above the normal global cooldown: transmutes, Mooncloth
// and Salt-Shaker style recipes stall for hours or days, so the bot could
// never finish them on order. `GetRecoveryTime` (the max of RecoveryTime
// and CategoryRecoveryTime) is in milliseconds; a normal GCD tops out at
// 1500 ms (mirrors PlayerbotAI::NoteSpellGlobalCooldown).
inline constexpr uint32_t kNormalGcdMaxMs = 1500;

struct CraftOrderableInputs
{
    bool known = false;
    bool createsItem = false;
    // Item facts (caller reads them off the made item's prototype).
    bool itemOrderable = false;   // GuildOrders::IsOrderableItem (no BoP/quest/timed)
    bool itemNeedsProfession = false; // RequiredSkill != 0 (e.g. engineering goggles)
    uint32_t spellRecoveryMs = 0; // GetRecoveryTime()
};

inline OrderReason CraftOrderableVerdict(CraftOrderableInputs const& inputs)
{
    if (inputs.known && inputs.createsItem && inputs.itemOrderable &&
        !inputs.itemNeedsProfession && inputs.spellRecoveryMs <= kNormalGcdMaxMs)
        return OrderReason::None;
    if (!inputs.createsItem)
        return OrderReason::NoRecipe;
    if (!inputs.itemOrderable)
        return OrderReason::NoSource;
    if (inputs.itemNeedsProfession)
        return OrderReason::NoSkill;
    return OrderReason::Timeout;
}

// Wire token for the RECIPE line: "ok" when orderable, else the reason
// token. Old addons ignore the extra field; new ones enable the Craft
// order button only on "ok".
inline char const* CraftOrderToken(OrderReason verdict)
{
    return verdict == OrderReason::None ? "ok" : ReasonToken(verdict);
}

// Plain refusal lines for `.bot order <bot> craft` give, one per reason.
inline char const* CraftOrderRefusal(OrderReason verdict)
{
    switch (verdict)
    {
        case OrderReason::NoRecipe: return "Enchants cannot be ordered yet.";
        case OrderReason::NoSource: return "That item is bound when picked up, so the bot cannot hand it over.";
        case OrderReason::NoSkill:   return "That item needs a profession to use.";
        case OrderReason::Timeout:  return "That recipe has a long cooldown, so the bot cannot make it on order.";
        default:                    return "The bot does not know that recipe.";
    }
}

} // namespace GuildOrders
} // namespace TortoiseBots
