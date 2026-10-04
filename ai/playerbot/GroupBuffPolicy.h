#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the group-buff port (issue #468): refresh a buff
// shortly before it expires, and upgrade a single-target buff to its group
// variant when enough party members lack both. No core includes: callers in
// strategy/ translate game state into these plain inputs, so the rules stay
// testable in tools/test_group_buff_policy.cpp without the server.
//
// Donor: mod-playerbots GenericBuffUtils (BuffBelowRefreshTarget /
// HasEnoughSameMapMissingPlayersForGroupVariant / GroupVariantFor) plus the
// beforeDuration plumbing in GenericTriggers/GenericSpellActions. Adapted:
// the donor passes beforeDuration = 0 at every buff registration, so stock
// donor also only rebuffs on fall-off outside its out-of-combat force-rebuff
// pass; the window here is a named 15 s so expiring buffs are topped up on
// the last out-of-combat tick instead of dropping mid-fight. Greater
// blessings stay out (donor GroupVariantFor excludes them too - coordinated
// by a separate system, here by the GreaterBlessing gates).

namespace ai
{
    // Ms before expiry at which a buff counts as needing a refresh. Donor
    // scale for explicit windows is 5-10 s (pet/prot short buffs); 15 s
    // covers the buff trigger re-evaluation interval with margin.
    std::uint32_t const kBuffRefreshWindowMs = 15000;

    // Donor quorum: prefer singles until at least three living, in-world
    // group members on the bot's map lack both the single-target buff and
    // its group variant (requiredCount = 3, presence check on both).
    std::uint32_t const kGroupBuffMinMissing = 3;

    // True when the buff should be (re)cast: missing outright, or expiring
    // inside the refresh window. Durations are ms remaining, matching the
    // Aura::GetAuraDuration() units the trigger/action gates read.
    // Permanent auras report <= 0 and never count (donor force-rebuff
    // branch treats them the same way).
    inline bool BuffNeedsRefresh(bool hasAura, std::int32_t remainingMs,
        std::uint32_t windowMs = kBuffRefreshWindowMs)
    {
        if (!hasAura)
            return true;
        if (remainingMs <= 0)
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
}
