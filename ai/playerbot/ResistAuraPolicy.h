#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for paladin resist-aura auto-swap per boss
// (mod-playerbots parity, raid1 batch item 8).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Base/Actions per-boss resist
// actions (add the paladin resist strategy + cast the aura now) +
// MCStrategy.cpp/BWLStrategy.cpp resist trigger lists.
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_resist_aura_policy.cpp.

namespace ai
{
    // Fire-aura bosses (paladin Fire Resistance Aura): Lucifron is shadow
    // (see below); everything else breathing fire or blasting fire damage.
    // Entries all 1.18.1 verified at port time — see provenance.
    inline bool IsFireAuraBoss(std::uint32_t entry)
    {
        switch (entry)
        {
            case 11982: // Magmadar
            case 12057: // Garr
            case 12056: // Baron Geddon
            case 12098: // Sulfuron Harbinger
            case 11988: // Golemagg the Incinerator
            case 11502: // Ragnaros
            case 12435: // Razorgore the Untamed
            case 13020: // Vaelastrasz the Corrupt
            case 12017: // Broodlord Lashlayer
            case 11983: // Firemaw
            case 11981: // Flamegor
                return true;
            default:
                return false;
        }
    }

    // Shadow-aura bosses (paladin Shadow Resistance Aura).
    inline bool IsShadowAuraBoss(std::uint32_t entry)
    {
        switch (entry)
        {
            case 12118: // Lucifron
            case 12259: // Gehennas
            case 12018: // Majordomo Executus
                return true;
            default:
                return false;
        }
    }

    // Swap only when the wanted aura is not already up; the trigger that
    // queues the swap is paladin-gated upstream (cheap class check first,
    // mirroring the donor), so this predicate covers the aura-state half.
    inline bool ShouldSwapResistAura(bool wantFire, bool wantShadow, bool hasFireAura, bool hasShadowAura)
    {
        if (wantFire && !hasFireAura)
            return true;
        if (wantShadow && !hasShadowAura)
            return true;
        return false;
    }

    // Kept in sync with the swap actions (DungeonActions.cpp): fire wins on
    // conflict (a boss is never both). Used by the actions so trigger and
    // action route through one rule instead of duplicating the spell names.
    inline std::string WantedResistAuraAction(bool wantFire, bool wantShadow)
    {
        if (wantFire)
            return "fire resistance aura";
        if (wantShadow)
            return "shadow resistance aura";
        return "";
    }
}
