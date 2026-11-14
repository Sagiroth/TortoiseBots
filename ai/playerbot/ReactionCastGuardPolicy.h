#pragma once

// Pure rule behind the reaction engine's cast break (PlayerbotAI::UpdateAIReaction) and
// AiPlayerbot.ReactionHealCastGuardPct.
//
// UpdateAIReaction breaks the bot's cast as soon as FindReaction PICKS a reaction whose
// action says it may (ShouldReactionInterruptCast), before that action's Execute can decide
// it has nothing to do. Every MovementAction says it may. Over walk 102's 150 healer-fights
// that cost each healer 10.5 casts a fight (the cut lands at a median 4 % of the cast, 79 %
// of the cuts under 10 %): `boss tactics seek` answers 74.5 % of the cuts the diagnostics
// could label, `raid spread` most of the rest, and their typical pass ends without a step
// (the seek's at-spot path returns false, a held slot does not walk).
//
// The guard stands only those two actions down (ReactionYieldsToHealCast), and only for a
// healer's heal that has not got far: the key's percent is the opening of the cast a
// yielding reaction may still cut, so the walk value 1 keeps a heal from its first percent
// on and the median 4 % cut is kept. Key off (0) is today's rule, and every other reaction
// still cuts: freeze, flee, bomb runout, avoid aoe, hazard, potions, victim escape, BoP and
// the dispels. A seek or spread that really walks still breaks the cast itself: MoveTo
// interrupts once it has a path.
//
// No core includes: the caller passes the generic slot's own read (Spell::getState,
// Spell::GetCastTime, Spell::GetCastedTime and, for a channel, the channel's duration) as a
// ReactionCastRead. This core counts a cast bar down in PREPARING and runs the channel phase
// in CASTING; it has no separate channeling state, the compat shim maps
// SPELL_STATE_CHANNELING onto CASTING.

#include <cstdint>

namespace ai
{
    // Spell states from game/Spells/Spell.h, numeric to stay core-free.
    enum ReactionCastState : std::uint32_t
    {
        REACTION_CAST_PREPARING = 1,
        REACTION_CAST_CASTING = 2,
    };

    // The bot's generic cast slot as the caller reads it. A slot with no spell is the caller's null pointer.
    struct ReactionCastRead
    {
        std::uint32_t state = 0;       // Spell::getState()
        std::int32_t totalMs = 0;      // the countdown's total: the cast time on a bar, the channel's duration in CASTING
        std::uint32_t remainingMs = 0; // Spell::GetCastedTime() (m_timer), which starts at the total and counts to 0
        bool isHeal = false;           // the spell heals outright or over time, by its effects
    };

    // Is a cast under way to protect? A cast bar in PREPARING or a channel in CASTING, still with time left on
    // its countdown. NULL, FINISHED, IDLE and DELAYED (an instant whose missile is in flight) sit on no bar.
    inline bool ReactionCastRunning(ReactionCastRead const& cast)
    {
        return (cast.state == REACTION_CAST_PREPARING || cast.state == REACTION_CAST_CASTING) && cast.remainingMs > 0;
    }

    // Progress of the running cast in percent: 0 at the start, 100 at the end; -1 when no cast is running or its
    // total is unknown (an instant, nothing on a bar to lose). A remaining above the total reads as a fresh cast.
    // Same number the CastCancel diagnostic rows record, so a walk's cast_pct column reads as the guard sees it.
    inline int ReactionCastPct(ReactionCastRead const& cast)
    {
        if (!ReactionCastRunning(cast) || cast.totalMs <= 0)
            return -1;
        const std::int32_t remaining = cast.remainingMs > static_cast<std::uint32_t>(cast.totalMs)
            ? cast.totalMs : static_cast<std::int32_t>(cast.remainingMs);
        const int pct = static_cast<int>(100 * (cast.totalMs - remaining) / cast.totalMs);
        return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    }

    // Does a picked reaction break the bot's cast?
    // - not a cast-breaking reaction: no;
    // - key off, or a reaction that does not yield, or the bot is no healer: break, as before;
    // - no cast slot, a spell that does not heal, or no progress to read: break, as before;
    // - otherwise break only while the cast is under `guardPct` % done: past that the heal is too far along to drop
    //   for a positioning pass that usually ends in a no-op.
    inline bool ReactionCastBreakDue(bool shouldInterrupt, bool yieldsToHeal, int guardPct,
                                     bool botIsHealer, ReactionCastRead const* cast)
    {
        if (!shouldInterrupt) return false;
        if (guardPct <= 0 || !yieldsToHeal || !botIsHealer) return true;
        if (!cast || !cast->isHeal) return true;
        const int pct = ReactionCastPct(*cast);
        return pct < 0 || pct < guardPct;
    }
} // namespace ai
