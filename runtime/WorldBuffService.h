// Issue #492: world-buff purchase service. Charge-first/refund (same pattern
// as the hire provisioner), then apply the original aura directly with
// Unit::AddAura on the buyer plus live in-world group/raid members within
// 40 yd of the recruiter NPC. The NPC is the aura caster, so a purchased
// buff never counts toward an aura unlock (PR5 filters recruiter casters).
//
// Pure-callable core: all targeting/price math runs on plain data and is
// unit-tested standalone (tools/test_world_buff_policy.cpp covers the math;
// the caster/quest wiring below is exercised headless in PR3+).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{

// One purchase application. spellIds holds 1 aura normally, 3 for the DM
// tribute pack, 1 chosen variant for Sayge (after stripping the other 7).
struct WorldBuffPurchase
{
    uint8_t buyIndex = 0;
    uint32_t totalCopper = 0;
    std::vector<uint32_t> spellIds;
    std::vector<uint32_t> stripSpellIds;
};

// Resolve the price pair + spell list for a buy index from live config.
// Returns false for unknown indices (caller re-shows the menu).
struct WorldBuffPricePair
{
    uint32_t baseCopper = 0;
    uint32_t perPersonCopper = 0;
};

inline bool WorldBuffPrices(uint8_t buyIndex, WorldBuffPricePair& pair,
    uint32_t cfgBase, uint32_t cfgPer, uint32_t cfgSaygeBase, uint32_t cfgSaygePer,
    uint32_t cfgSongBase, uint32_t cfgSongPer, uint32_t cfgSilBase, uint32_t cfgSilPer)
{
    switch (buyIndex)
    {
        case 1: // Rally
        case 2: // Warchief
        case 3: // Zandalar
        case 4: // DM pack
            pair.baseCopper = cfgBase;
            pair.perPersonCopper = cfgPer;
            return true;
        case 5: // Sayge (variant chosen in the submenu)
            pair.baseCopper = cfgSaygeBase;
            pair.perPersonCopper = cfgSaygePer;
            return true;
        case 6: // Songflower
            pair.baseCopper = cfgSongBase;
            pair.perPersonCopper = cfgSongPer;
            return true;
        case 7: // Silithyst
            pair.baseCopper = cfgSilBase;
            pair.perPersonCopper = cfgSilPer;
            return true;
        default:
            return false;
    }
}

// Spell lists per purchase. Sayge needs the chosen variant id from the
// submenu; strip holds the other 7 (removed before applying the choice so
// only one fortune is ever active).
inline bool WorldBuffSpells(uint8_t buyIndex, uint32_t saygeChoice,
    std::vector<uint32_t>& apply, std::vector<uint32_t>& strip)
{
    apply.clear();
    strip.clear();
    switch (buyIndex)
    {
        case 1: apply.push_back(22888); return true;
        case 2: apply.push_back(16609); return true;
        case 3: apply.push_back(24425); return true;
        case 4:
            apply.push_back(22817);
            apply.push_back(22818);
            apply.push_back(22820);
            return true;
        case 5:
        {
            static uint32_t const variants[] = { 23735, 23736, 23737, 23738, 23766, 23767, 23768, 23769 };
            bool known = false;
            for (uint32_t v : variants)
                if (v == saygeChoice)
                    known = true;
            if (!known)
                return false;
            apply.push_back(saygeChoice);
            for (uint32_t v : variants)
                if (v != saygeChoice)
                    strip.push_back(v);
            return true;
        }
        case 6: apply.push_back(15366); return true;
        case 7: apply.push_back(29534); return true;
        default: return false;
    }
}

// Sayge variant names for the submenu, in spell order.
struct WorldBuffSaygePick
{
    uint32_t spellId;
    char const* label;
};

inline WorldBuffSaygePick const* WorldBuffSaygePicks(uint32_t& count)
{
    static WorldBuffSaygePick const picks[] = {
        { 23735, "Strength" },
        { 23736, "Agility" },
        { 23737, "Stamina" },
        { 23738, "Spirit" },
        { 23766, "Intelligence" },
        { 23767, "Armor" },
        { 23768, "Damage" },
        { 23769, "Resistance" },
    };
    count = 8;
    return picks;
}

inline bool IsKnownSaygeChoice(uint32_t spellId)
{
    uint32_t count = 0;
    for (WorldBuffSaygePick const* p = WorldBuffSaygePicks(count); count > 0; --count, ++p)
        if (p->spellId == spellId)
            return true;
    return false;
}

} // namespace TortoiseBots
