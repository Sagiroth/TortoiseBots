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
// Scope: a taunt-immune boss with a `threat_successor` row (Hakkar and Jin'do today). Many taunt-immune bosses fixate
// or swap on purpose (Buru's fixate), so the escape is opt in by row data and names no boss; another boss opts in by
// a `threat_successor` row.
//
// CLASS_* ids are kept literal so the header needs no core include: 2 paladin, 3 hunter, 4 rogue, 5 priest, 8 mage.

#include <cstdint>

namespace ai
{
    // One escape per victim in this window: a second one would spend a cooldown on a boss that just left.
    constexpr std::uint32_t kVictimEscapeGapMs = 6000;
    // A self escape that has not landed (a resisted Feign Death: the boss is still on the victim, no feign-death state,
    // no immunity to its melee) holds the victim's slot this long, then the paladin may protect.
    constexpr std::uint32_t kVictimEscapeLandWaitMs = 1000;
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

    // Which of the victim's own escapes are ready: the full cast check passes (VictimEscapeCastReady). A real player
    // (no bot AI) has none: a paladin protects them.
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
        bool escapedRecently = false;   // the victim's last escape still holds its slot (VictimEscapeRecent)
        bool otherNonTankTarget = true; // another living non-tank stands on the boss's threat list
        bool holdsDuty = false;         // the victim holds a boss-tactic duty (VictimHoldsDuty)
    };

    // Whether the victim's last escape still holds its slot. `hasLast` false: none yet. A landed escape (the feign-death
    // state, the boss's victim changed, or the victim immune to its melee, read on a later tick) holds it for
    // kVictimEscapeGapMs; one that has not landed only for kVictimEscapeLandWaitMs.
    inline bool VictimEscapeRecent(bool hasLast, bool landed, std::uint32_t sinceLastMs)
    {
        if (!hasLast)
            return false;
        return sinceLastMs < (landed ? kVictimEscapeGapMs : kVictimEscapeLandWaitMs);
    }

    // Whether one escape is ready: the bot's full cast check (PlayerbotAI::CanCastSpell: silence, pacify, stun,
    // reagents such as Vanish's Flash Powder, category cooldowns, power) passes, or it refused only because the bot's
    // own cast is in progress and the core's Spell::CheckCast passes on its own (the escape breaks that cast).
    inline bool VictimEscapeCastReady(bool fullCheckOk, bool refusedOnlyByOwnCast, bool coreCheckOk)
    {
        return fullCheckOk || (refusedOnlyByOwnCast && coreCheckOk);
    }

    // Whether the core's cast check refused the escape only for the global cooldown of the bot's own cast in progress:
    // the check said not ready, the own cast is still preparing (interrupting it cancels its global cooldown,
    // Spell::cancel), and the escape itself is on neither its own nor its category cooldown. The caller then re-runs
    // the check without the global cooldown (CheckCast(false)) for the rest (silence, reagents, range).
    inline bool VictimEscapeGcdOnlyRefusal(bool notReady, bool ownCastPreparing, bool spellCooldown,
                                           bool categoryCooldown)
    {
        return notReady && ownCastPreparing && !spellCooldown && !categoryCooldown;
    }

    // Whether the boss is one the escape serves: immune to taunts and carrying a `threat_successor` row.
    inline bool VictimEscapeBossInScope(bool tauntImmune, bool threatSuccessorRow)
    {
        return tauntImmune && threatSuccessorRow;
    }

    // Whether an escape would drop the victim's duty: it is the walk-out puller, the open meet's Son puller, or the
    // current victim of another live, engaged hostile creature than the boss (a Son, an add): Feign Death or Vanish
    // would wipe that creature's threat too, and a protected member would leave it loose.
    // Whether one attacker of the victim counts for the duty rule: a live, engaged hostile creature on the victim that is
    // neither the boss nor another boss (a second boss on the victim is the escape's own case, not a duty).
    inline bool CountsAsDutyAttacker(bool isTheBoss, bool isCreature, bool alive, bool engaged, bool onVictim,
                                     bool isBoss)
    {
        return !isTheBoss && isCreature && alive && engaged && onVictim && !isBoss;
    }

    inline bool VictimHoldsDuty(bool walkoutPuller, bool meetPuller, int otherCreaturesOnVictim)
    {
        return walkoutPuller || meetPuller || otherCreaturesOnVictim > 0;
    }

    // The gate shared by the victim's own escape and the paladin's protection: the key is on, the victim is no tank,
    // the boss is in scope (VictimEscapeBossInScope) and is on the victim, no escape inside the window, another non-tank target is left for
    // the boss to turn to, and no duty would be dropped.
    inline bool VictimEscapeAllowed(bool key, bool victimIsTank, bool onVictim, bool bossInScope,
                                    const VictimEscapeGuards& guards)
    {
        if (!key || victimIsTank || !onVictim || !bossInScope)
            return false;
        if (guards.escapedRecently || !guards.otherNonTankTarget || guards.holdsDuty)
            return false;
        return true;
    }

    // The victim's own escape, by its class: hunter Feign Death, rogue Vanish, mage Ice Block, paladin Divine Shield.
    // Any other class, or one whose escape is not ready, asks for protection.
    inline VictimEscape VictimEscapeFor(bool key, bool victimIsTank, bool onVictim, bool bossInScope,
                                        std::uint8_t classId, const VictimEscapeReady& ready,
                                        const VictimEscapeGuards& guards = VictimEscapeGuards())
    {
        if (!VictimEscapeAllowed(key, victimIsTank, onVictim, bossInScope, guards))
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
