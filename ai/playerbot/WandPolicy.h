#pragma once

#include <cstdint>

// Pure decision rule for "a caster with mana casts instead of wanding"
// (mage, priest, warlock).
//
// Turtle core rule (tortoise-wow `Unit::_UpdateAutoRepeatSpell`): every wand
// shot (spell category 351, SPELLCATEGORY_RANGED_WEAPON) puts the global
// cooldown (category 133) on the shooter for the wand's whole swing time,
// and the next shot renews it. Wand Shoot (5019) itself has no GCD category,
// so it stays castable while every caster spell with a GCD (Frostbolt,
// Fireball, Smite, Mind Flay, Shadow Bolt, Flash Heal, Lesser Heal...)
// answers SPELL_FAILED_NOT_READY. A bot whose nuke fell through to its
// "shoot" alternative once (a GCD tick, a cast refused while moving), or
// whose "no mana" / "healer should wand" row started the wand, then never
// saw its nuke or heal possible again: it wanded at full mana, or let the
// party drop, until movement broke the wand or the fight ended.
//
// The donor (mod-playerbots class default lists: the nuke ranks above
// shoot; heal rows far above both) wands only when nothing better can be
// cast. This rule keeps that intent under the Turtle GCD: stop the wand
// while a wanted spell is blocked by nothing but the GCD (or is castable
// outright), keep it while the spell is blocked by anything else (no mana,
// not known, silenced, school locked out, wrong form, no line of sight...).
//
// No core includes: the class triggers translate game state into these
// plain inputs, so the rule stays testable in tools/test_wand_policy.cpp.

namespace ai
{
    // Spell category of wand Shoot (5019) in Turtle 1.18.1 spell data.
    constexpr std::uint32_t WAND_SHOOT_CATEGORY = 351;

    // How a spell probe (PlayerbotAI::CanCastSpell with its result) came out.
    enum class WandSpellProbe
    {
        Castable,           // the cast would go out now
        GlobalCooldownOnly, // refused with SPELL_FAILED_NOT_READY: the GCD
        Blocked             // anything else: mana, unknown spell, not wanted...
    };

    // What the trigger can read on the caster and target without CheckCast.
    // The core answers SPELL_FAILED_NOT_READY for the GCD before it looks at
    // any of these (Spell::CheckCast: GCD, then form, line of sight, and
    // CheckCasterAuras for silence), and a player's school lockout is a
    // per-spell cooldown that the bot-side probe also reports as NOT_READY.
    // While the wand renews the GCD, NOT_READY alone cannot tell a free
    // spell from a silenced, locked-out, wrong-form or out-of-sight one.
    struct WandCastBlockers
    {
        bool silenced = false;      // silence (or pacify) flag for the spell's prevention type
        bool lockedOut = false;     // the spell's school is locked out, or the spell has a cooldown of its own
        bool wrongForm = false;     // the caster's shapeshift form forbids the spell
        bool noLineOfSight = false; // the target is out of line of sight
    };

    // Turn a probe (castable now / refused with NOT_READY / refused for
    // anything else) plus the blockers above into a WandSpellProbe. Any
    // blocker makes it Blocked: stopping the wand for a spell that still
    // cannot go out would only restart the wand one swing later. A probe
    // that is "castable" is checked too, because the probe ignores range
    // and so passes a line-of-sight failure.
    inline WandSpellProbe ClassifyWandSpellProbe(bool castable, bool notReady, WandCastBlockers const& blockers)
    {
        if (!castable && !notReady)
            return WandSpellProbe::Blocked;
        if (blockers.silenced || blockers.lockedOut || blockers.wrongForm || blockers.noLineOfSight)
            return WandSpellProbe::Blocked;
        return castable ? WandSpellProbe::Castable : WandSpellProbe::GlobalCooldownOnly;
    }

    struct WandState
    {
        bool wandAutoRepeat;      // current auto-repeat spell is a wand shot
        WandSpellProbe bestNuke;  // best probe over the class's nukes
        bool keepWandForThreat;   // raid row "has aggro -> shoot" owns this
        bool noManaRowActive;     // "no mana" (< 5%) -> shoot row owns this
        // Best probe over the heals a heal row wants now (priest only;
        // Blocked when nobody needs one). Heal rows sit far above the
        // "no mana" wand row, so a wanted heal stops the wand even there.
        WandSpellProbe bestHeal = WandSpellProbe::Blocked;
        // The "threat" multiplier zeroes the nukes ("high threat" active):
        // the next tick would fall back to the wand anyway, so stopping it
        // only ping-pongs (stop, wait one swing, "shoot" again).
        bool nukeThreatVetoed = false;
        // A grouped healer whose wand HealerMayWand no longer allows (a
        // party member needs a heal, or the wand's mob is not fighting the
        // party): the wand goes, whatever the spell probes say. The same
        // rule keeps "shoot" from starting it again, so this never
        // ping-pongs.
        bool healerMustStop = false;
        // The mana-short row ("<filler> unaffordable" -> shoot, mage) owns
        // this wand: the filler nuke costs more than the mana left. The
        // stop probes Frostbolt and Fireball, which can be cheaper than an
        // arcane or fire mage's filler; stopping then only ping-pongs.
        bool manaShortRowActive = false;
    };

    inline bool ShouldStopWandToCast(WandState const& state)
    {
        if (!state.wandAutoRepeat)
            return false;
        if (state.healerMustStop)
            return true;
        if (state.keepWandForThreat)
            return false;
        if (state.bestHeal != WandSpellProbe::Blocked)
            return true;
        if (state.noManaRowActive || state.manaShortRowActive || state.nukeThreatVetoed)
            return false;
        return state.bestNuke != WandSpellProbe::Blocked;
    }

    // When a grouped healer may wand at all (heal spec in a group; a bot
    // outside a group or without a heal spec is never held back here).
    //
    // mod-playerbots puts every healer damage row, the wand included, under
    // HealerShouldAttackTrigger, which is off while any party member is under
    // the almost-full line: a healer never wands while somebody needs a heal.
    // Here the "healer should wand" row asks the same, but a queued "shoot"
    // outlives the trigger by up to ExpireActionTime (5 s), and a running
    // wand renews the GCD on every shot, so every heal row answers "not
    // ready" and the engine falls through to the wand again (Uldaman
    // 2026-10-14: the priest wanded at 4000+ mana while the tank went from
    // 81 % to 46 %). The shoot action and the "wand blocks cast" stop both
    // ask this rule, so the wand neither starts nor keeps running then.
    //
    // The wand's mob must also be fighting the party already (a member or
    // the healer itself): a healer that opens on a fresh mob pulls it onto
    // itself (same run: the priest wanded a mob the tank had not touched
    // yet, took it, and spent the next seconds on Fade instead of heals).
    //
    // Out of mana (the "no mana" row, under 5 %) the wand is all the healer
    // has left, so it may wand an engaged mob whoever is hurt.
    //
    // Never held back (mod-playerbots HealerShouldAttackTrigger escape,
    // GenericTriggers.cpp:348): no other group member near (within
    // SightDistance) - a player who ran off, a group spread over the map:
    // the healer fights like a solo bot. Nor on the target of an explicit
    // attack order to this bot (player control first).
    struct HealerWandGate
    {
        bool groupedHealer = false;   // heal spec, in a group
        bool memberNeedsHeal = false; // a party member in heal reach is under the almost-full line, or the healer under the low line
        bool noMana = false;          // under the "no mana" line
        bool targetEngaged = false;   // the wand's mob is attacking the healer or another party member
        bool aloneNear = false;       // no other live group member within SightDistance
        bool explicitOrder = false;   // the wand's mob is this bot's explicit attack target
        bool healOnly = false;        // the run leader keeps this healer to heals (PlayerbotAI::RunHealOnly)
    };

    inline bool HealerMayWand(HealerWandGate const& gate)
    {
        if (!gate.groupedHealer || gate.aloneNear || gate.explicitOrder)
            return true;
        // The run leader's healer heals: no wand at all (Uldaman run 4: the
        // priest drew the camp's aggro with Holy Fire, Mind Blast and the wand).
        if (gate.healOnly)
            return false;
        if (!gate.targetEngaged)
            return false;
        if (gate.noMana)
            return true;
        return !gate.memberNeedsHeal;
    }

    // Combine two spell probes: the better outcome wins.
    inline WandSpellProbe BetterWandSpellProbe(WandSpellProbe a, WandSpellProbe b)
    {
        if (a == WandSpellProbe::Castable || b == WandSpellProbe::Castable)
            return WandSpellProbe::Castable;
        if (a == WandSpellProbe::GlobalCooldownOnly || b == WandSpellProbe::GlobalCooldownOnly)
            return WandSpellProbe::GlobalCooldownOnly;
        return WandSpellProbe::Blocked;
    }

    // The mana-short wand row ("<nuke> unaffordable" -> shoot, mage): the
    // filler nuke has no "shoot" fallback any more (a GCD or threat-veto
    // refusal must not start the wand), so without this row a mage with
    // too little mana for its nuke but above the 5% "no mana" line stood
    // still. Wand exactly when the mana is short: never for the GCD, the
    // threat veto or any other refusal. The nuke is the spec's filler (the
    // default action: Frostbolt, Fireball, Arcane Missiles): a cheaper
    // spell no row casts as filler would not keep the mage busy either.
    // When the filler is not known (cost 0) the "no mana" row is all there is.
    inline bool NukeUnaffordable(bool hasManaPool, std::uint32_t mana, std::uint32_t nukeCost)
    {
        return hasManaPool && nukeCost && mana < nukeCost;
    }

    // The raid row "has aggro -> shoot" (mage and warlock raid strategies)
    // and the wand stops that leave it alone ask the same question: a real
    // raid group, and the bot holds its target's aggro. The "raid" class
    // strategies are on in every dungeon (PlayerbotAI::IsInRaid counts any
    // instance map), so the row must check the group itself; a 5-man party
    // in Zul'Farrak is not a raid group.
    inline bool RaidAggroWandRow(bool inRaidGroup, bool hasAggro)
    {
        return inRaidGroup && hasAggro;
    }

    // What is left of a cooldown of durationMs that started at startMs, at
    // nowMs (WorldTimer::getMSTime() values; uint32 wrap-safe).
    inline std::uint32_t CooldownRemainingMs(std::uint32_t startMs, std::uint32_t durationMs, std::uint32_t nowMs)
    {
        std::uint32_t const elapsed = nowMs - startMs;
        return elapsed < durationMs ? durationMs - elapsed : 0;
    }

    // Longest sleep "stop wand" ever asks for, whatever the inputs say: a
    // slowed caster's ranged timer stays well under this.
    constexpr std::uint32_t WAND_STOP_MAX_WAIT_MS = 6000;

    // After stopping the wand, sleep until the wanted spell can go out, so
    // the next combat tick does not find it "not ready", fall through to
    // "shoot" and restart the wand. Two things hold it back:
    //  - the last wand shot's GCD, which the core sets to the ranged timer
    //    at the shot (Unit::_UpdateAutoRepeatSpell), so it ends when the
    //    ranged timer does, slowed attack speed included;
    //  - the GCD of a spell the bot cast after that shot (an instant cast
    //    while the wand ran), which can outlast the ranged timer.
    // Wait for the later of the two, plus the react delay. Whichever set
    // the GCD last, the later end is never too early; at worst (a spell
    // cast before the last shot) it waits out a GCD the shot replaced.
    // The old rule waited the ranged timer alone, capped at the unmodified
    // swing: about 70% of frost-mage stops in Zul'Farrak saw Frostbolt
    // still "not ready" and Shoot again within 1 s.
    inline std::uint32_t WandStopWaitMs(std::uint32_t rangedTimerMs, std::uint32_t spellGcdRemainingMs, std::uint32_t reactDelayMs)
    {
        std::uint32_t wait = rangedTimerMs > spellGcdRemainingMs ? rangedTimerMs : spellGcdRemainingMs;
        if (wait > WAND_STOP_MAX_WAIT_MS)
            wait = WAND_STOP_MAX_WAIT_MS;
        return wait + reactDelayMs;
    }
}
