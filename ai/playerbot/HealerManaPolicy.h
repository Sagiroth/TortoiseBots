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

    // Mana a grouped healer keeps back before it spends any on off-spec
    // damage (`healer should attack`). `balance` is BalancePercentValue:
    // party levels * 100 / weighted enemy levels, capped at 200, so a LOW
    // balance is a HARD fight and a HIGH balance an EASY one.
    // Donor mod-playerbots 79bd428 (src/Ai/Base/Trigger/GenericTriggers.cpp:361-368):
    // 85 (balance <= 50), HighMana 65 (<= 100), MediumMana 40 (above).
    // Deliberate deviation, owner decision 2026-10-12: the easy tier keeps
    // 70 instead of 40. A full 5-man against one or two elites lands in that
    // tier, and a vanilla healer (5-second rule, no Replenishment) that nukes
    // down to 40 % runs dry on the first real damage (Uldaman / build-10:
    // "shoot | no mana" ~23 s into a fight, tank at 44 %). The even tier is
    // raised from 65 to 70 as well (controller ruling 2026-10-12) so the
    // ladder stays monotonic: a harder fight never allows a lower reserve
    // than an easier one. The hard tier stays on the donor's 85.
    constexpr std::uint8_t HEALER_DPS_MANA_FLOOR_HARD_FIGHT = 85;
    constexpr std::uint8_t HEALER_DPS_MANA_FLOOR_EVEN_FIGHT = 70;
    constexpr std::uint8_t HEALER_DPS_MANA_FLOOR_EASY_FIGHT = 70;

    inline std::uint8_t HealerDpsManaFloor(std::uint8_t balance)
    {
        if (balance <= 50)
            return HEALER_DPS_MANA_FLOOR_HARD_FIGHT;
        if (balance <= 100)
            return HEALER_DPS_MANA_FLOOR_EVEN_FIGHT;
        return HEALER_DPS_MANA_FLOOR_EASY_FIGHT;
    }

    // True when the healer's own mana allows off-spec damage. A bot without
    // a mana bar is never held back here; the wand row (`healer should
    // wand`) does not ask this at all.
    inline bool HealerDpsManaAllows(bool hasMana, std::uint8_t mana, std::uint8_t balance)
    {
        return !hasMana || mana >= HealerDpsManaFloor(balance);
    }

    // Whether a grouped healer's off-spec damage (`healer should attack`,
    // `healer should wand`) may go on its current target: only when that mob
    // already fights another party member. A healer that opens on a fresh mob
    // (the tank still on its own pull, the commander holding the party back
    // for the opener) pulls it onto itself. Without a target the rows decide
    // as before (their actions need one anyway); a bot outside a group is
    // never held back.
    inline bool HealerDamageTargetAllowed(bool grouped, bool hasTarget, bool targetFightsOtherMember)
    {
        if (!grouped || !hasTarget)
            return true;
        return targetFightsOtherMember;
    }
}
