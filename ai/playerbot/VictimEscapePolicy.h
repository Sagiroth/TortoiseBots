#pragma once

// The escape of a taunt-immune boss's non-tank victim (AiPlayerbot.DungeonRunImmuneBossVictimEscape). Tested in
// tools/test_victim_escape_policy.cpp; callers pass plain numbers, so this header stays free of core includes.
//
// A taunt-immune boss that turns on a damage dealer or a healer (Hakkar after Cause Insanity) cannot be taken back by
// a tank, so the victim has to leave the boss's pick itself. The core's victim pick (ThreatManager.cpp
// SelectHostileTarget, the low-priority skip) passes over a target immune to the attacker's melee school, so Ice Block,
// Divine Shield and Blessing (on this core: Hand) of Protection make the boss retarget; Feign Death and Vanish drop the
// victim's threat. Fade is left out on purpose: on this core both its effects are SPELL_AURA_DUMMY with no handler
// (spell_template 586-10942, SpellAuras.cpp HandleAuraDummy's empty priest case), so it moves no threat at all.
//
// CLASS_* ids are kept literal so the header needs no core include: 2 paladin, 3 hunter, 4 rogue, 5 priest, 8 mage.

#include <cstdint>

namespace ai
{
    // One escape per victim in this window: a second one would spend a cooldown on a boss that just left.
    constexpr std::uint32_t kVictimEscapeGapMs = 6000;
    // The paladin's Blessing of Protection reach: the spell's own range (rangeIndex 4).
    constexpr float kVictimEscapeBopYd = 30.0f;

    enum class VictimEscape
    {
        None = 0,
        FeignDeath,
        Vanish,
        IceBlock,
        DivineShield,
        AskProtection, // no own escape ready: a paladin in reach may protect the victim
    };

    // Which of the victim's own escapes are ready (known, off cooldown, castable).
    struct VictimEscapeReady
    {
        bool feignDeath = false;
        bool vanish = false;
        bool iceBlock = false;
        bool divineShield = false;
    };

    // The restrictions every escape (own or a paladin's protection) answers to.
    struct VictimEscapeGuards
    {
        bool escapedRecently = false;   // the victim had an escape inside kVictimEscapeGapMs (VictimEscapeRecent)
        bool otherNonTankTarget = true; // another living non-tank stands on the boss's threat list
        bool holdsDuty = false;         // the victim holds a boss-tactic duty (the walk-out puller, a held Son)
    };

    // Whether the victim's last escape is still inside the window. `hasLast` false: none yet.
    inline bool VictimEscapeRecent(bool hasLast, std::uint32_t sinceLastMs)
    {
        return hasLast && sinceLastMs < kVictimEscapeGapMs;
    }

    // The gate shared by the victim's own escape and the paladin's protection: the key is on, the victim is no tank,
    // the boss is taunt-immune and is on the victim, no escape inside the window, another non-tank target is left for
    // the boss to turn to, and no duty would be dropped.
    inline bool VictimEscapeAllowed(bool key, bool victimIsTank, bool onVictim, bool bossTauntImmune,
                                    const VictimEscapeGuards& guards)
    {
        if (!key || victimIsTank || !onVictim || !bossTauntImmune)
            return false;
        if (guards.escapedRecently || !guards.otherNonTankTarget || guards.holdsDuty)
            return false;
        return true;
    }

    // The victim's own escape, by its class: hunter Feign Death, rogue Vanish, mage Ice Block, paladin Divine Shield.
    // Any other class, or one whose escape is not ready, asks for protection.
    inline VictimEscape VictimEscapeFor(bool key, bool victimIsTank, bool onVictim, bool bossTauntImmune,
                                        std::uint8_t classId, const VictimEscapeReady& ready,
                                        const VictimEscapeGuards& guards = VictimEscapeGuards())
    {
        if (!VictimEscapeAllowed(key, victimIsTank, onVictim, bossTauntImmune, guards))
            return VictimEscape::None;
        switch (classId)
        {
            case 3: if (ready.feignDeath) return VictimEscape::FeignDeath; break;
            case 4: if (ready.vanish) return VictimEscape::Vanish; break;
            case 8: if (ready.iceBlock) return VictimEscape::IceBlock; break;
            case 2: if (ready.divineShield) return VictimEscape::DivineShield; break;
            default: break;
        }
        return VictimEscape::AskProtection;
    }

    // Whether this paladin casts Blessing of Protection on the victim: the victim has no own escape ready
    // (VictimEscapeFor gives AskProtection), the paladin's protection is ready (and the victim carries no
    // Forbearance), and the victim stands within kVictimEscapeBopYd. A paladin never protects itself this way: its
    // own Divine Shield is the escape.
    inline bool ProtectorShouldBop(bool selfIsPaladin, bool bopReady, bool targetIsSelf, VictimEscape victimEscape,
                                   float distYd)
    {
        return selfIsPaladin && bopReady && !targetIsSelf && victimEscape == VictimEscape::AskProtection &&
            distYd <= kVictimEscapeBopYd;
    }

    // The log row's spell word for an escape.
    inline const char* VictimEscapeWord(VictimEscape e)
    {
        switch (e)
        {
            case VictimEscape::FeignDeath: return "feign death";
            case VictimEscape::Vanish: return "vanish";
            case VictimEscape::IceBlock: return "ice block";
            case VictimEscape::DivineShield: return "divine shield";
            case VictimEscape::AskProtection: return "hand of protection";
            default: return "";
        }
    }
}
