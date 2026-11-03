#pragma once

// Pure rules for the "awaiting return" hold of a dungeon run's bots
// (AiPlayerbot.DungeonReviveHoldYd). A bot the run is bringing back (a
// summon or a teleport return under way, or one revived far from the run's
// lead bot) stands where it is: no follow, no move, no chase. Walking the
// navmesh back to the leader through packs the run never cleared is what
// killed it before. It still fights back with what does not move it.
//
// The run leader's 1 Hz tick stamps each held bot "held until now + mark";
// the bot's CanMove reads the stamp, so a stopped run, an unbind or a crash
// of the tick releases the bot by itself within the mark. No core includes:
// tools/test_teleport_return_policy.cpp tests these rules.

#include <cstdint>

namespace ai
{
    // One 1 Hz tick missed, plus margin.
    constexpr uint32_t RETURN_HOLD_MARK_MS = 2500;

    // A hold lasts at most this long without the bot arriving: then it is
    // released (and the run says so).
    constexpr uint32_t kReturnHoldMs = 60000;

    // Arrived: within this of the point it is brought to.
    constexpr float kReturnArriveYd = 10.0f;

    // A live mark lies at most one mark ahead of now (unsigned subtraction:
    // a timer wrap still reads inside the window, a stale value far ahead
    // cannot hold the bot).
    inline bool ReturnHoldOn(uint32_t untilMs, uint32_t nowMs)
    {
        uint32_t const d = untilMs - nowMs;
        return untilMs != 0 && d != 0 && d <= RETURN_HOLD_MARK_MS;
    }

    // A bot of a dungeon run alive again (any revive: the run's, a player's,
    // a GM's) is held when the key is on, it is inside a dungeon, and it is
    // farther than the key from the run's lead bot (1000 when not on the lead
    // bot's map). 0 turns the hold off.
    inline bool ShouldArmReviveHold(float holdYd, bool inDungeon, float leadYd)
    {
        return holdYd > 0.0f && inDungeon && leadYd > holdYd;
    }

    // The revive hold ends once the bot stands with the lead bot, or the run
    // took it over (its own return hold then decides).
    inline bool ReviveHoldReleased(float leadYd, bool runOwnsIt)
    {
        return leadYd <= kReturnArriveYd || runOwnsIt;
    }
} // namespace ai
