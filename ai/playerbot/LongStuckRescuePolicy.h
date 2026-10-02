#pragma once

// Pure policy for the long-stuck last-resort teleport (issue #400): a bot that
// has not moved or earned for 15+ minutes must end up somewhere else. Hearth
// goes first when it leads somewhere useful, repop second - but repop runs
// through the engine gates, and its movement precondition (MovementAction::
// isPossible, PlayerbotAI::CanMove) fails on exactly the bots that need it
// most: rooted, stunned, feared or otherwise immobile bots never relocate, so
// the rescue fires every 5 s forever while the bot stands still (74 of the 90
// "move long stuck" bots in the 2026-10-02 pool logged no RepopAction and no
// UseHearthStoneAction). A teleport needs no movement precondition, so the
// last resort below moves those bots directly.
// mod-playerbots has no equivalent: its stuck triggers only reset, and only
// dead-engine "location stuck" repops - nothing relocates a live wedged bot.

namespace ai
{
    // Where the last-resort teleport goes. Graveyard first (always nearby and
    // level-neutral), homebind when the bot already stands next to its
    // graveyard (teleporting there would be a no-op the next trip re-fires on).
    enum class LongStuckFallbackTarget
    {
        None,
        Graveyard,
        Homebind
    };

    // A teleport shorter than this is not a rescue: it leaves the bot inside
    // its own 50-yd stuck-progress bound (MoveLongStuckTrigger) next to a spot
    // it cannot walk from, so the next trip fires on the same coordinate. 100
    // yd clears aggro/interaction range and the stuck bound with margin.
    float const LONG_STUCK_FALLBACK_MIN_RELOCATE_YD = 100.0f;

    inline LongStuckFallbackTarget PickLongStuckFallbackTarget(bool rescueAllowed, bool haveGraveyard,
        float graveyardDistYd, float homebindDistYd)
    {
        // Battleground bots and bots with an actively played master are their
        // player's business: never yank them (mirrors RepopAction::isUseful).
        if (!rescueAllowed)
            return LongStuckFallbackTarget::None;

        if (haveGraveyard && graveyardDistYd > LONG_STUCK_FALLBACK_MIN_RELOCATE_YD)
            return LongStuckFallbackTarget::Graveyard;

        if (homebindDistYd > LONG_STUCK_FALLBACK_MIN_RELOCATE_YD)
            return LongStuckFallbackTarget::Homebind;

        return LongStuckFallbackTarget::None;
    }
}
