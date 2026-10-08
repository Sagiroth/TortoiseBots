#pragma once

#include <cstdint>

// Pure decision rules for the healer mana-conservation port (night2 gaps
// 1+2): refuse to START a single-target direct heal whose expected amount
// dwarfs the target's missing health, and refuse mana-hungry heals while the
// healer's own bar is low. No core includes: callers in strategy/ translate
// game state into these plain inputs, so the rules stay testable in
// tools/test_healer_mana_policy.cpp without the server.
//
// Donor: mod-playerbots HealerAutoSaveManaMultiplier
// (src/Ai/Base/Strategy/ConserveManaStrategy.cpp:92-129) with the
// estAmount/manaEfficiency table on each party heal action
// (src/Ai/Class/{Priest,PriestActions.h:70-75,Druid/Action/DruidActions.h:47-72,
// Paladin/Actions/PaladinActions.h:158-183,Shaman/ShamanActions.h:323-363}).
// Adapted: the mid-fight cancellation half already exists here
// (HealingCastPolicy::ShouldCancelWastefulHeal, which cancels an in-flight
// cast); this covers the pre-cast half the donor runs as a multiplier.
// Thresholds reuse the live bands (lowHealth/mediumHealth) instead of the
// donor's saveManaThreshold knob, so there is no new config key to document.

namespace ai
{
    // Donor HealingManaEfficiency (mod-playerbots PlayerbotAIConfig.h:33-41),
    // trimmed to the ranks this port assigns. Higher is cheaper per health.
    enum class HealManaEfficiency : std::uint8_t
    {
        LOW = 2,
        MEDIUM = 4,
        HIGH = 8,
        VERY_HIGH = 16
    };

    struct HealerManaState
    {
        std::uint8_t targetHealth;   // 0-100, target's current percent
        std::uint8_t healerMana;     // 0-100, healer's own mana percent
        std::uint8_t estAmount;      // expected heal, percent of max health
        HealManaEfficiency efficiency;
        bool targetIsTank;
        std::uint8_t lowHealth;      // danger line: never veto below this
        std::uint8_t mediumHealth;   // comfort line: waste veto above this
        std::uint8_t mediumMana;     // healer reserve line for cheap-heal veto
    };

    // True when the heal may start. Never vetoes a target in danger (at or
    // below lowHealth) - an emergency cast always fires. Otherwise:
    //  - above mediumHealth, veto when the expected heal overshoots the
    //    missing health (loss < estAmount) or the spell is at best MEDIUM
    //    efficiency (tanks: estAmount counts 2/3, they have bigger bars -
    //    donor estAmount /= 1.5);
    //  - at any non-danger health, veto LOW-or-worse spells while the
    //    healer's own mana is below the reserve line.
    inline bool ShouldStartHeal(HealerManaState const& heal)
    {
        if (heal.targetHealth <= heal.lowHealth)
            return true;
        std::uint8_t loss = heal.targetHealth >= 100 ? 0 : 100 - heal.targetHealth;
        std::uint8_t est = heal.estAmount;
        if (heal.targetIsTank)
            est = std::uint8_t((std::uint16_t(est) * 2) / 3);
        if (heal.targetHealth >= heal.mediumHealth &&
            (loss < est || heal.efficiency <= HealManaEfficiency::MEDIUM))
            return false;
        if (heal.efficiency <= HealManaEfficiency::LOW && heal.healerMana < heal.mediumMana)
            return false;
        return true;
    }
}
