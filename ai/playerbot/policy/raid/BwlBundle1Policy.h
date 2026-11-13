#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the cheap BWL per-boss bundle (mod-playerbots
// parity, raid1 batch item 4): Broodlord 18y ranged step-out, drake
// off-tank flank, Nefarian mage Ice Block on Wild Magic, Vael 13020 in the
// dragon-flank list. Nefarian class calls (priest, shaman, warlock) are
// native rules with no donor, see below.
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/BWL/BWLTriggers.cpp:59-71
// (BwlBroodlordRangedTooCloseTrigger: ranged non-victim within
// BROODLORD_SAFE_DISTANCE 18y), BWLActions.cpp:269-278 (step out to 18y),
// BWLTriggers.cpp:102-106 (BwlNefarianWildMagicTrigger: mage + Wild Magic
// 23410), BWLStrategy.cpp:38-47/54-55 (drake rear flank + ice block wiring),
// BWLHelpers.h:55 (18y constant).
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_bwl_bundle1_policy.cpp.

namespace ai
{
    // Broodlord Lashlayer 12017, Vaelastrasz the Corrupt 13020,
    // Nefarian 11583, Wild Magic 23410 (all 1.18.1 verified).
    constexpr std::uint32_t kBroodlordEntry = 12017;
    constexpr std::uint32_t kVaelEntry = 13020;
    constexpr std::uint32_t kNefarianEntry = 11583;
    constexpr std::uint32_t kWildMagicSpellId = 23410;

    // Donor BROODLORD_SAFE_DISTANCE: ranged holds 18y (Blast Wave).
    constexpr float kBroodlordRangeDistance = 18.0f;

    // Broodlord: ranged non-victims inside 18y step out. Victims (aggro)
    // must not kite the boss through the room.
    inline bool ShouldLeaveBroodlordRange(bool botIsRanged, bool botIsVictim, bool insideRange)
    {
        return botIsRanged && !botIsVictim && insideRange;
    }

    // Drake flank for off-tanks: a tank that is NOT the boss's victim
    // flanks like everyone else instead of holding the head.
    inline bool ShouldOffTankFlank(bool botIsTank, bool botIsVictim)
    {
        return botIsTank && !botIsVictim;
    }

    // Nefarian Wild Magic: mage carriers Ice Block (existing action).
    inline bool ShouldNefarianIceBlock(bool botIsMage, bool selfHasWildMagic)
    {
        return botIsMage && selfHasWildMagic;
    }

    // Nefarian class calls beyond the mage one. Native gap: the donor
    // (mod-playerbots 79bd428 src/Ai/Raid/BWL) only handles Wild Magic.
    // Ids from the core script (boss_nefarian.cpp HandleClassCall and
    // npc_corrupted_totemAI) and tw_world_spell_template /
    // tw_world_creature_template, all 1.18.1.
    // Priest call: Corrupted Healing 23401 on the priest; every spell with
    // a direct heal effect it casts makes its target take damage.
    constexpr std::uint32_t kCorruptedHealingSpellId = 23401;
    // Warlock call: two Corrupted Infernals 14668 spawn on the warlock.
    constexpr std::uint32_t kCorruptedInfernalEntry = 14668;
    // Shaman call: the totems that stay up and buff or heal Nefarian and
    // his drakonids within 40y. Corrupted Fire Nova Totem 14662 is left
    // out: the core despawns it on its first update.
    constexpr std::uint32_t kCorruptedStoneskinTotemEntry = 14663;
    constexpr std::uint32_t kCorruptedHealingStreamTotemEntry = 14664;
    constexpr std::uint32_t kCorruptedWindfuryTotemEntry = 14666;
    // DPS only switch to a class-call add this close (the totems stand at
    // the shaman's feet, the infernals on the warlock: inside the raid).
    constexpr float kNefarianClassCallAddRange = 30.0f;

    // Kill order for class-call adds: totems first (Healing Stream heals
    // the boss), then infernals. 0 = not a class-call add.
    inline int NefarianClassCallAddRank(std::uint32_t entry)
    {
        if (entry == kCorruptedStoneskinTotemEntry ||
            entry == kCorruptedHealingStreamTotemEntry ||
            entry == kCorruptedWindfuryTotemEntry)
            return 2;
        if (entry == kCorruptedInfernalEntry)
            return 1;
        return 0;
    }

    // Who switches: damage dealers only (tanks hold the boss, healers
    // heal) and never over an explicit player attack order.
    inline bool ShouldNefarianKillClassCallAdd(bool botIsTank, bool botIsHealer, bool hasExplicitTarget)
    {
        return !botIsTank && !botIsHealer && !hasExplicitTarget;
    }

    // Switch only to a higher-ranked add than the current target, so a
    // bot already on a totem is not bounced between totems.
    inline bool ShouldSwitchToClassCallAdd(int bestAddRank, int currentTargetRank)
    {
        return bestAddRank > 0 && bestAddRank > currentTargetRank;
    }

    // Priest carrying Corrupted Healing: no spell with a direct heal effect
    // (Renew and Power Word: Shield have none, so they stay available).
    inline bool ShouldBlockCorruptedHeal(bool selfHasCorruptedHealing, bool spellHasDirectHeal)
    {
        return selfHasCorruptedHealing && spellHasDirectHeal;
    }
}
