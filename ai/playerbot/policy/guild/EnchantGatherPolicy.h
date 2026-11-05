#pragma once

// Enchant-order material gathering (guild enchant service).
//
// Pure decision rules behind one question the lead's lifecycle action asks:
// given the working enchant order's rows, are all its reagents in the bags.
// The spell's reagent list arrives as plain data (the AI side reads it off
// the enchant spell), so the standalone test never links the server. The
// caller feeds bag counts; the header never touches Player, mail, or bank.
//
// Shape of the answer:
//   complete     = every reagent held covers its need (one cast: want=1)
//   stillMissing = per-reagent shortfall (bags only), for the need list
//   readyToMeet  = complete and no reagent ticked bring= (the giver's share
//                  arrives at Meet, so the order waits for it instead)
//
// Bring-ticked reagents count from bags like any other, but their shortfall
// never reaches a bank or auction leg: the ladder routes them to the need
// list through the same BringTickedBlocksLeg gate craft orders use, and the
// order only turns ready once the giver's share is in the bags too.

#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

struct EnchantMaterial
{
    uint32_t itemId = 0;
    uint32_t need = 0; // per cast (want is always 1 for an enchant order)
};

struct EnchantMaterialState
{
    uint32_t itemId = 0;
    uint32_t need = 0;
    uint32_t inBags = 0;
    bool bringTicked = false; // giver brings it: never banked, bought, or farmed
};

// One reagent's bags shortfall, after the ticked share is counted.
inline uint32_t EnchantMaterialShortfall(EnchantMaterialState const& material)
{
    return material.inBags >= material.need ? 0 : material.need - material.inBags;
}

// Every reagent in the bags covers one cast: the order may turn ready.
inline bool EnchantMaterialsComplete(EnchantMaterialState const* materials, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (EnchantMaterialShortfall(materials[i]) != 0)
            return false;
    return true;
}

// Shortfall rows for the need list, in spell order: unticked reagents first
// (the ladder still sources those), ticked ones last (the giver's share).
// Zero-count rows never appear: a reagent in the bags is simply absent.
inline std::vector<EnchantMaterial> EnchantMissingMaterials(EnchantMaterialState const* materials,
    size_t count)
{
    std::vector<EnchantMaterial> out;
    for (size_t pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < count; ++i)
        {
            bool ticked = materials[i].bringTicked;
            if ((pass == 1) != ticked)
                continue;
            if (uint32_t shortfall = EnchantMaterialShortfall(materials[i]))
                out.push_back(EnchantMaterial{ materials[i].itemId, shortfall });
        }
    return out;
}

// Whether a missing reagent may still be sourced by the ladder: a ticked
// reagent never is (the need list owns it), everything else runs the usual
// bags -> bank -> ah -> need legs.
inline bool EnchantReagentSourcable(EnchantMaterialState const& material)
{
    return !material.bringTicked;
}

} // namespace GuildOrders
} // namespace TortoiseBots
