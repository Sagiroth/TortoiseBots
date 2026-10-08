#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the group-buff port (issue #468): refresh a LONG
// buff shortly before it expires, and upgrade a single-target buff to its
// group variant when enough party members lack both. No core includes:
// callers in strategy/ translate game state into these plain inputs, so the
// rules stay testable in tools/test_group_buff_policy.cpp without the server.
//
// Donor: mod-playerbots GenericBuffUtils (BuffBelowRefreshTarget /
// HasEnoughSameMapMissingPlayersForGroupVariant / GroupVariantFor) plus the
// beforeDuration plumbing in GenericTriggers/GenericSpellActions. Adapted:
// the donor passes beforeDuration = 0 at every buff registration, so stock
// donor also only rebuffs on fall-off outside its out-of-combat force-rebuff
// pass; the window here is a named 15 s applied only to long buffs (max >=
// 5 min), so short combat buffs and combo finishers (Slice and Dice,
// Rupture) still run to expiry. Greater blessings stay out (donor
// GroupVariantFor excludes them too - coordinated by a separate system,
// here by the GreaterBlessing gates).

namespace ai
{
    // Ms before expiry at which a long buff counts as needing a refresh.
    // Donor scale for explicit windows is 5-10 s (pet/prot short buffs); 15 s
    // covers the buff trigger re-evaluation interval with margin.
    std::uint32_t const kBuffRefreshWindowMs = 15000;

    // Only long upkeep buffs (party buffs, armors, seals...) are refreshed
    // early. Short combat buffs and combo finishers (Slice and Dice 9-36 s,
    // Rupture 6-14 s, Holy Shield 10 s...) must run to expiry: a 15 s window
    // on a 12 s aura would keep its trigger permanently active and clip it.
    std::int32_t const kBuffRefreshMinMaxDurationMs = 5 * 60 * 1000;

    // Donor quorum: prefer singles until at least three living, in-world
    // group members on the bot's map lack both the single-target buff and
    // its group variant (requiredCount = 3, presence check on both).
    std::uint32_t const kGroupBuffMinMissing = 3;

    // True when the buff should be (re)cast: missing outright, or a LONG
    // buff expiring inside the refresh window. Durations are ms, matching
    // the Aura::GetAuraDuration()/GetAuraMaxDuration() units the gates read.
    // Permanent auras report <= 0 remaining and never count (donor
    // force-rebuff branch treats them the same way). Short auras (max
    // below the floor) only rebuff on fall-off, exactly as before #468.
    inline bool BuffNeedsRefresh(bool hasAura, std::int32_t remainingMs,
        std::int32_t maxDurationMs, std::uint32_t windowMs = kBuffRefreshWindowMs)
    {
        if (!hasAura)
            return true;
        if (remainingMs <= 0 || maxDurationMs < kBuffRefreshMinMaxDurationMs)
            return false;
        return std::uint32_t(remainingMs) < windowMs;
    }

    // Group variant of a single-target buff, or empty when there is none.
    // Greater blessings deliberately excluded (donor GroupVariantFor too):
    // vanilla 1.12 group buffs are Gift of the Wild, Arcane Brilliance,
    // Prayer of Fortitude / Spirit / Shadow Protection only.
    inline std::string GroupBuffVariantFor(std::string const& baseName)
    {
        if (baseName == "mark of the wild")
            return "gift of the wild";
        if (baseName == "arcane intellect")
            return "arcane brilliance";
        if (baseName == "power word: fortitude")
            return "prayer of fortitude";
        if (baseName == "divine spirit")
            return "prayer of spirit";
        if (baseName == "shadow protection")
            return "prayer of shadow protection";
        return std::string();
    }

    // True when (groupName, baseName) is a recognized upgrade pair: scopes
    // the quorum/reagent gates so unrecognized pairs (paladin greater
    // blessings, which the map above excludes) keep their existing
    // first-missing-member behavior untouched.
    inline bool IsGroupBuffUpgradePair(std::string const& groupName, std::string const& baseName)
    {
        return !baseName.empty() && GroupBuffVariantFor(baseName) == groupName;
    }

    // Donor UpgradeToGroupIfAppropriate, quorum half: upgrade only when the
    // group variant is trained and stocked (every 1.12 group buff spends a
    // reagent: Wild Thornroot / Arcane Powder / candles) and at least
    // minMissing members lack both auras. No master gate: buffs are
    // autonomous upkeep with no explicit-order path to conflict with (donor
    // has none either); the mana floor, retry cooldown and claim registry
    // already protect the master's mana and GCDs.
    inline bool ShouldUpgradeToGroupBuff(bool knowsGroupSpell, bool hasReagents,
        std::uint32_t missingBothCount, std::uint32_t minMissing = kGroupBuffMinMissing)
    {
        if (!knowsGroupSpell || !hasReagents)
            return false;
        return missingBothCount >= minMissing;
    }

    // Mana percent an upkeep buff waits for before it is (re)cast. Charge
    // buffs (Inner Fire, shields, Shadowguard: spent by being hit, re-cast
    // at full price) use the higher floor. A bot with a real player master
    // buffs on the master's schedule - the master watches the bar and pulls
    // around it - so the floor drops: the healer veto (HealerManaPolicy,
    // mediumMana) still guards the actual heal casts, which is the reserve
    // that matters. Pool bots keep the conservative floor.
    std::uint8_t const kBuffMinManaPercent = 40;
    std::uint8_t const kChargeBuffMinManaPercent = 70;
    std::uint8_t const kHiredBuffMinManaPercent = 20;
    std::uint8_t const kHiredChargeBuffMinManaPercent = 40;

    inline std::uint8_t BuffManaFloor(bool isChargeBuff, bool hasRealPlayerMaster)
    {
        if (hasRealPlayerMaster)
            return isChargeBuff ? kHiredChargeBuffMinManaPercent : kHiredBuffMinManaPercent;
        return isChargeBuff ? kChargeBuffMinManaPercent : kBuffMinManaPercent;
    }
}
