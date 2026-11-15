#pragma once

// Shared interrupt-target rule (fury Pummel / combat Kick / shield bash /
// war stomp parity row, task G).
//
// A target counts as worth interrupting only while it has a spell the core
// itself would let an interrupt land on: a generic spell still on its cast
// bar (PREPARING with a real cast time — the state Spell::Update counts
// down) or already channeling (CASTING), or any channeled spell in flight,
// and that spell is silence-prevented. This mirrors the core's own
// Spell::EffectInterruptCast gate (PREPARING-with-cast-time or CASTING +
// PreventionType SILENCE + the damage/channel interrupt flag), minus the
// travel-time nuance a trigger cannot see.
//
// Why not IsNonMeleeSpellCasted(true): that helper also returns true for a
// DELAYED spell (instant cast with a missile in flight — Hateful Strike,
// Slimebolt — nothing on a bar to interrupt) and for the autorepeat slot,
// so every melee boss read as "casting" and Pummel/Kick fired on cooldown
// into thin air (Patchwerk 2026-10-14: 64 Pummels / 19 Kicks, 10 rage +
// GCD each, zero casts to interrupt). mod-playerbots asks the same
// question the same way (PlayerbotAI::IsInterruptableSpellCasting,
// 79bd4281): cast-in-progress first, interrupt effect second.
//
// A stun counts as an interrupt too, behind AiPlayerbot.InterruptAllowStun
// (default 0): Bash, Hammer of Justice, War Stomp and Repentance on this
// core are an APPLY_AURA MOD_STUN effect only (no interrupt-cast effect, no
// combat-interrupt flag), so the donor's three-way check never accepted them
// and the tank interrupt rows were dead (Tortoise Trials tank-bear-bash-caster
// 0/75, tank-paladin-hoj-caster 3/32). The core stops any non-melee cast a
// stun lands on, so a stun also skips the victim's silence-prevention and
// interrupt-flag gate. Intercept's stun is one level away (effect 64 triggers
// 20253, a MOD_STUN aura): the caller resolves the trigger into
// effectTriggeredStun.
//
// Only a target that can actually be stunned is worth the cooldown, and
// IsImmuneToSpellEffect is not enough to say so: it reads the effect's
// mechanic, while Bash/Hammer of Justice carry STUN as the SPELL's Mechanic
// with an empty effect mechanic (Creature::IsImmuneToSpellEffect in the core
// reads EffectMechanic only), so a stun-immune boss passed. The caller
// therefore fills InterruptStunImmunity from the core (spell-level immunity,
// the creature's raw mechanic_immune_mask STUN bit, boss status, an existing
// stun aura and stun diminishing returns); any one of them blocks the stun,
// and a boss counts as unstunnable without the bit. The pure rule cannot
// read the creature, but it does own the verdict.
//
// Pure rule on core spell state, no engine types: the trigger passes the
// target's generic/channeled Spell pointers (or nulls) plus the
// already-resolved interrupt spell's flags, and gets a verdict.

#include <cstdint>

class Spell;

namespace ai
{
    // Spell states from game/Spells/Spell.h (numeric to stay core-free).
    enum InterruptSpellState : uint32_t
    {
        INTERRUPT_SPELL_PREPARING = 1,
        INTERRUPT_SPELL_CASTING = 2,
    };

    // Effect ids from game/Spells/SpellDefines.h, aura ids from
    // game/Spells/SpellAuraDefines.h (verified 1.18.1: INTERRUPT_CAST =
    // 68, APPLY_AURA = 6, MOD_SILENCE = 27, MOD_STUN = 12), mechanic
    // MECHANIC_STUN = 12 from SpellDefines.h.
    enum InterruptEffect : uint32_t
    {
        INTERRUPT_EFFECT_INTERRUPT_CAST = 68,
        INTERRUPT_EFFECT_APPLY_AURA = 6,
        INTERRUPT_AURA_MOD_SILENCE = 27,
        INTERRUPT_AURA_MOD_STUN = 12,
        INTERRUPT_MECHANIC_STUN = 12,
    };

    // The creature's mechanic_immune_mask, bit for MECHANIC_STUN. Read
    // directly because Bash/HoJ carry the stun as the spell Mechanic only,
    // which Creature::IsImmuneToSpellEffect does not look at.
    inline bool StunMechanicImmune(uint32_t mechanicImmuneMask)
    {
        return (mechanicImmuneMask & (1u << (INTERRUPT_MECHANIC_STUN - 1))) != 0;
    }

    // Why the target cannot be stunned. The pure rule has no access to the
    // creature or the aura list, so the caller fills this in; any one true
    // means a stun is never picked.
    struct InterruptStunImmunity
    {
        // Unit::IsImmuneToSpell of the stun spell (the spell's Mechanic
        // against mechanic_immune_mask, school immunity, and so on).
        bool spellImmune = false;
        // The creature's mechanic_immune_mask has the STUN bit.
        bool stunMechanicImmune = false;
        // A boss: world boss, the run's plan boss, or boss rank. Bosses are
        // treated as unstunnable even without the immunity bit.
        bool boss = false;
        // A MOD_STUN aura is already on the target.
        bool alreadyStunned = false;
        // The target's stun diminishing return is at immune.
        bool stunDiminished = false;

        bool BlocksStun() const
        {
            return spellImmune || stunMechanicImmune || boss || alreadyStunned || stunDiminished;
        }
    };

    struct InterruptTargetSpell
    {
        // The spell object on the target, if any (generic slot first,
        // channeled slot second). Null = slot empty.
        Spell const* spell = nullptr;
        // True when this is the channeled slot (CASTING = channel phase).
        bool channeledSlot = false;
        // Spell::getState() of the object.
        uint32_t state = 0;
        // Spell::GetCastTime() — 0 for instants.
        int32_t castTime = 0;
        // SpellEntry of the spell being cast.
        uint32_t preventionType = 0; // SPELL_PREVENTION_TYPE_SILENCE = 1
        uint32_t interruptFlags = 0; // SPELL_INTERRUPT_FLAG_DAMAGE = 0x02
        uint32_t channelInterruptFlags = 0; // CHANNEL_FLAG_INTERRUPT = 0x04
    };

    struct InterruptSpellInfo
    {
        uint32_t interruptFlags = 0; // of the interrupt spell (pummel/kick/...)
        uint32_t preventionType = 0;
        // Effect triples of the interrupt spell (3 max, EFFECT_INDEX_0..2).
        static const int MAX_EFFECTS = 3;
        uint32_t effect[MAX_EFFECTS] = {0, 0, 0};
        uint32_t effectAura[MAX_EFFECTS] = {0, 0, 0};
        // Whether the target is immune to effect i (IsImmuneToSpellEffect).
        bool effectImmune[MAX_EFFECTS] = {false, false, false};
        // Whether effect i is SPELL_EFFECT_TRIGGER_SPELL and the spell it
        // triggers applies MOD_STUN to a target that is not immune to it
        // (Intercept 20252 effect 2 -> 20253). Resolved by the caller, which
        // is the only place that can look the triggered entry up.
        bool effectTriggeredStun[MAX_EFFECTS] = {false, false, false};
        // Why the target cannot be stunned at all (any one blocks the stun).
        // The caller fills it from the core; effect immunity for a stun
        // effect is effectImmune above.
        InterruptStunImmunity stunImmunity;
    };

    // True when the target has an interruptible cast in progress AND the
    // interrupt spell can land on it. Either slot may be null. allowStun is
    // AiPlayerbot.InterruptAllowStun: false keeps the donor's three-way check
    // exactly (a stun never counts).
    inline bool ShouldInterruptTargetCasting(InterruptTargetSpell const* genericSpell,
        InterruptTargetSpell const* channeledSpell, InterruptSpellInfo const& interrupt, bool allowStun)
    {
        // The victim's cast must be real: a generic spell still on its bar
        // or channeling, or a channeled spell in flight. DELAYED instants
        // (missile in flight) and finished/idle slots are not on a bar.
        bool casting = false;
        InterruptTargetSpell const* victim = nullptr;
        if (genericSpell && genericSpell->spell &&
            ((genericSpell->state == INTERRUPT_SPELL_PREPARING && genericSpell->castTime > 0) ||
                genericSpell->state == INTERRUPT_SPELL_CASTING))
        {
            casting = true;
            victim = genericSpell;
        }
        else if (channeledSpell && channeledSpell->spell &&
            (channeledSpell->state == INTERRUPT_SPELL_PREPARING ||
                channeledSpell->state == INTERRUPT_SPELL_CASTING))
        {
            casting = true;
            victim = channeledSpell;
        }
        if (!casting || !victim)
            return false;

        // Does the interrupt spell itself put a stun on the target? Only
        // behind AiPlayerbot.InterruptAllowStun: a tank then spends Bash /
        // Hammer of Justice / War Stomp on the first trash cast instead of
        // saving the 60 s cooldown. A target that cannot be stunned (immune,
        // boss, already stunned, stun-diminished) never counts.
        bool stun = false;
        if (allowStun && !interrupt.stunImmunity.BlocksStun())
        {
            for (int i = 0; i < InterruptSpellInfo::MAX_EFFECTS; ++i)
            {
                if (interrupt.effectImmune[i])
                    continue;
                if ((interrupt.effect[i] == INTERRUPT_EFFECT_APPLY_AURA &&
                        interrupt.effectAura[i] == INTERRUPT_AURA_MOD_STUN) ||
                    interrupt.effectTriggeredStun[i])
                {
                    stun = true;
                    break;
                }
            }
        }

        // The victim's spell must be silence-prevented with the matching
        // interrupt flag (core EffectInterruptCast gate): generic spells
        // carry INTERRUPT_FLAG_DAMAGE, channeled ones CHANNEL_FLAG_INTERRUPT.
        // A stun stops any non-melee cast the core has under way, so it needs
        // neither: that is the point of accepting it here.
        static const uint32_t SILENCE = 1;
        static const uint32_t INTERRUPT_FLAG_DAMAGE = 0x02;
        static const uint32_t CHANNEL_FLAG_INTERRUPT = 0x04;
        if (!stun)
        {
            if (victim->preventionType != SILENCE)
                return false;
            if (victim->channeledSlot)
            {
                if (!(victim->channelInterruptFlags & CHANNEL_FLAG_INTERRUPT))
                    return false;
            }
            else if (!(victim->interruptFlags & INTERRUPT_FLAG_DAMAGE))
                return false;
        }

        // The interrupt spell itself must be able to interrupt (donor's
        // three-way check, unchanged): combat-interrupt + silence, an
        // interrupt-cast effect the target is not immune to, or a silence
        // aura effect; a stun (above) is the fourth way.
        static const uint32_t INTERRUPT_FLAG_COMBAT = 0x10;
        for (int i = 0; i < InterruptSpellInfo::MAX_EFFECTS; ++i)
        {
            if ((interrupt.interruptFlags & INTERRUPT_FLAG_COMBAT) && interrupt.preventionType == SILENCE)
                return true;
            if (interrupt.effect[i] == INTERRUPT_EFFECT_INTERRUPT_CAST && !interrupt.effectImmune[i])
                return true;
            if (interrupt.effect[i] == INTERRUPT_EFFECT_APPLY_AURA &&
                interrupt.effectAura[i] == INTERRUPT_AURA_MOD_SILENCE)
                return true;
        }
        return stun;
    }
}
