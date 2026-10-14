#pragma once

// Ammo cache pick (RandomItemMgr::BuildAmmoCache): one white projectile (or
// thrown weapon) per ten-level bucket and subclass.
//
// The old query was `order by required_level desc` and took the first row, so
// two items at the same level tied in database order: "Depricated Razor Arrow"
// (3031, flag 0x10, required level 30) was picked over nothing better, and
// bots were stocked with an item no vendor or player can obtain. The pick now
// (1) drops deprecated and test rows, (2) takes the highest damage per second
// among what a bot of that level can use, (3) then the higher required level,
// (4) then the lowest entry, so the answer never depends on row order.
//
// Pure, core-free, so tools/test_ammo_cache_policy.cpp includes it directly.

#include <cstdint>
#include <string>
#include <vector>

namespace ai
{
    // ITEM_FLAG_DEPRECATED (0x10): RandomItemMgr.cpp already refuses it for gear.
    constexpr uint32_t kAmmoFlagDeprecated = 0x00000010;

    struct AmmoCandidate
    {
        uint32_t entry = 0;
        uint32_t requiredLevel = 0;
        uint32_t flags = 0;
        std::string name;
        double dmgMin = 0.0;
        double dmgMax = 0.0;
        uint32_t delayMs = 0;
    };

    // False for rows no player can obtain: the deprecated flag, or the
    // placeholder names the item table carries ("Depricated" is the table's own
    // misspelling; the rest follow the gear filter in RandomItemMgr.cpp).
    inline bool IsObtainableAmmo(AmmoCandidate const& c)
    {
        if (c.flags & kAmmoFlagDeprecated)
            return false;
        static char const* const kJunk[] =
            { "Deprecated", "Depricated", "Test", "TEST", "Monster ", "Unused", "[PH]", "(OLD)" };
        for (char const* junk : kJunk)
            if (c.name.find(junk) != std::string::npos)
                return false;
        return true;
    }

    // Damage per second of one shot (the table's ammo delay is the 3 s default,
    // but the formula does not rely on it).
    inline double AmmoDps(AmmoCandidate const& c)
    {
        uint32_t const delay = c.delayMs ? c.delayMs : 1000;
        return (c.dmgMin + c.dmgMax) * 0.5 * 1000.0 / delay;
    }

    // True when a is the better pick than b.
    inline bool AmmoBetter(AmmoCandidate const& a, AmmoCandidate const& b)
    {
        double const dpsA = AmmoDps(a);
        double const dpsB = AmmoDps(b);
        if (dpsA != dpsB)
            return dpsA > dpsB;
        if (a.requiredLevel != b.requiredLevel)
            return a.requiredLevel > b.requiredLevel;
        return a.entry < b.entry;
    }

    // The entry for a bot of `level`, or 0 when nothing usable remains.
    inline uint32_t PickAmmo(std::vector<AmmoCandidate> const& candidates, uint32_t level)
    {
        AmmoCandidate const* best = nullptr;
        for (AmmoCandidate const& c : candidates)
        {
            if (!c.entry || c.requiredLevel > level || !IsObtainableAmmo(c))
                continue;
            if (!best || AmmoBetter(c, *best))
                best = &c;
        }
        return best ? best->entry : 0;
    }
}
