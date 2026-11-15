#pragma once

#include <cstdint>

namespace ai
{
    enum class AmmoBagKind : uint8_t
    {
        None = 0,
        Quiver,
        Pouch,
    };

    struct AmmoBagSpec
    {
        AmmoBagKind kind = AmmoBagKind::None;
        uint32_t slots = 0;
        uint32_t itemLevel = 0;
        uint32_t quality = 0;
    };

    // Hunters keep a single ammo bag matching their ranged weapon, preferring
    // more container slots first and higher item level / quality on ties.
    inline bool ShouldEquipAmmoBag(
        bool isHunter,
        AmmoBagKind wantedKind,
        AmmoBagSpec const& candidate,
        bool hasEquippedBag,
        AmmoBagSpec const& equipped)
    {
        if (!isHunter || wantedKind == AmmoBagKind::None || candidate.kind != wantedKind)
            return false;

        if (!hasEquippedBag || equipped.kind != wantedKind)
            return true;

        if (candidate.slots != equipped.slots)
            return candidate.slots > equipped.slots;
        if (candidate.itemLevel != equipped.itemLevel)
            return candidate.itemLevel > equipped.itemLevel;
        return candidate.quality > equipped.quality;
    }
}
