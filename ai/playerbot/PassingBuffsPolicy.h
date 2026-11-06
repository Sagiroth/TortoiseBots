#pragma once

#include <cstdint>

// Meeting-driven buffs from pool bots to real players they pass (issue
// #1531): a friendly player running past casts a buff and keeps going.
//
// Pure decision rules, no engine types: callers translate game state into
// plain inputs, so the rules stay testable in
// tools/test_passing_buffs_policy.cpp. The glue reads live aura state
// (bot knows spell, can cast, player lacks buff and nothing stronger) and
// feeds the verdicts back as casts through the normal cast path.

namespace ai
{
    // Bot classes that hand out a party buff to strangers. Values match
    // CLASS_* (2=paladin 5=priest 8=mage 11=druid). Stances/shouts (warrior)
    // and totems (shaman) stay out: they are self-centered or party-wide
    // effects, not single-target stranger casts.
    enum class PassingBuffClass : std::uint8_t
    {
        None = 0,
        Paladin = 2,
        Priest = 5,
        Mage = 8,
        Druid = 11,
    };

    // Which single-target buff a bot hands to a stranger. The name is the
    // spell-line name the action layer resolves (highest allowed rank for
    // the player's level), matching the party upkeep lines: priests keep
    // Power Word: Fortitude, druids Mark of the Wild, mages Arcane Intellect
    // (mana users only). Paladins pick per target class below.
    inline char const* PassingBuffName(PassingBuffClass cls, bool targetHasMana)
    {
        switch (cls)
        {
            case PassingBuffClass::Priest: return "power word: fortitude";
            case PassingBuffClass::Druid: return "mark of the wild";
            case PassingBuffClass::Mage: return targetHasMana ? "arcane intellect" : nullptr;
            default: return nullptr;
        }
    }

    // Paladin blessing for a stranger, mirroring the PvE priority lists
    // (CastPveBlessingOnPartyAction): tanks want Kings first, healers and
    // hunters Wisdom first, casters Kings then Wisdom, melee Might first.
    // Paladin strangers keep Wisdom first: a paladin buffs its own mana.
    inline char const* PassingBlessingName(bool targetTank, bool targetHealer,
        bool targetRanged, std::uint8_t targetClass)
    {
        // CLASS_* ids: 1=warrior 2=paladin 3=hunter 5=priest 8=mage 9=warlock 11=druid.
        bool const melee = !targetRanged && !targetHealer;
        if (targetTank)
            return targetClass == 2 ? "blessing of wisdom" : "blessing of kings";
        if (targetHealer)
            return "blessing of wisdom";
        if (targetRanged)
            return (targetClass == 8 || targetClass == 9 || targetClass == 11)
                ? "blessing of kings" : "blessing of wisdom";
        if (melee)
            return targetClass == 2 ? "blessing of wisdom" : "blessing of might";
        return "blessing of kings";
    }

    // Instant heal a passing bot may land on a low-health stranger. Priest:
    // Renew; druid: Rejuvenation. Nothing else: direct heals all have a cast
    // time and would stop the bot.
    inline char const* PassingHealName(PassingBuffClass cls)
    {
        switch (cls)
        {
            case PassingBuffClass::Priest: return "renew";
            case PassingBuffClass::Druid: return "rejuvenation";
            default: return nullptr;
        }
    }

    // Chance gates, in 1-in-N form (brief: 1 in 5 for buffs, 1 in 2 heals).
    // The caller rolls urand(0, N-1) == 0; zero or one disables the gate.
    std::uint32_t const kPassingBuffChanceDivisor = 5;
    std::uint32_t const kPassingHealChanceDivisor = 2;

    inline bool PassingRollWon(std::uint32_t roll, std::uint32_t divisor)
    {
        if (divisor <= 1)
            return true;
        return roll % divisor == 0;
    }

    // Cooldowns per (bot, player) pair (brief: 10 min buff, 60 s heal).
    // Entries are pruned on write, so the maps stay bounded by the players
    // met in one window.
    std::uint32_t const kPassingBuffCooldownSec = 10 * 60;
    std::uint32_t const kPassingHealCooldownSec = 60;

    inline bool PassingCooldownReady(std::uint32_t lastCastUnixSec,
        std::uint32_t nowUnixSec, std::uint32_t cooldownSec)
    {
        if (!lastCastUnixSec)
            return true;
        return nowUnixSec >= lastCastUnixSec + cooldownSec;
    }

    // Health band for the passing heal (brief: below 35%).
    std::uint8_t const kPassingHealBelowPct = 35;

    inline bool PassingHealWanted(std::uint8_t targetHealthPct, bool targetHasHot)
    {
        return targetHealthPct < kPassingHealBelowPct && !targetHasHot;
    }

    // A "meeting" is a bot newly within range. Each side remembers the
    // GUIDs of the others it saw on the previous sweep; only pairs not
    // seen last time count, so standing next to the same player does not
    // re-roll every sweep.
    inline bool PassingIsNewMeeting(bool seenLastSweep)
    {
        return !seenLastSweep;
    }

    // Distance band for the meeting itself: inside buff range (~30 yd)
    // counts, anything past sight is not a meeting at all.
    float const kPassingBuffRangeYd = 30.0f;

    inline bool PassingInMeetingRange(float distance2dYd, float sightYd)
    {
        return distance2dYd >= 0.0f && distance2dYd <= kPassingBuffRangeYd &&
            distance2dYd <= sightYd;
    }
}
