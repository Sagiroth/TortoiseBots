#pragma once

// Turn scheduling for the random-pool AI pass (BotManager::UpdateBots).
//
// One rule instead of special passes. After each AI turn a bot is due again
// when its own AI asked to think next (reaction delay, GCD, cast time, move
// duration), but never later than its situation allows: in combat 400 ms,
// dead or in a battleground 1 s, anything else 3 s. A change of situation
// (pulled into combat, died, started a teleport) makes the bot due at once,
// and so does arriving at the travel target (see TravelArrivalMakesDueAtOnce:
// only the walk-to-work edge counts, never a failed move or a re-pick).
//
// Every tick the due bots run most overdue first relative to tolerance. When
// PoolNearPlayerYd is set (off by default), those within that distance of a
// real player on the same map and instance run first as a group; the rest
// keep the same order and run after, never dropped. A bot left over keeps getting later and so climbs the order.
// Under load every bot is equally late relative to its tolerance: a fight
// never waits behind a questing walk, and no bot is skipped for good.
//
// Pure decision logic, no AI or core types: tested on its own in
// tools/test_bot_turn_scheduler.cpp. World-thread only, like BotManager.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{

// Most urgent last, so a larger value never waits longer.
enum class TurnSituation : uint8_t
{
    Normal,
    Battleground,
    Dead,
    Combat,
    Teleport,
};

// What the scan reads about a pool bot. Combat is the core's flag OR the AI's
// own combat engine (mod-playerbots GetReactDelay counts both): a bot closing
// on or pulling a target is fighting before the core says so, and the AI may
// keep fighting a moment after the core drops the flag.
struct TurnFacts
{
    bool teleporting = false;
    bool alive = true;
    bool coreCombat = false;
    bool aiCombat = false;
    bool battleground = false;
};

// Most urgent fact wins: teleport, then dead, then combat, then battleground.
inline TurnSituation TurnSituationFrom(TurnFacts const& facts)
{
    if (facts.teleporting)
        return TurnSituation::Teleport;
    if (!facts.alive)
        return TurnSituation::Dead;
    if (facts.coreCombat || facts.aiCombat)
        return TurnSituation::Combat;
    if (facts.battleground)
        return TurnSituation::Battleground;
    return TurnSituation::Normal;
}

// The longest a bot in this situation may go without a turn.
inline uint32_t TurnMaxWaitMs(TurnSituation situation)
{
    switch (situation)
    {
        case TurnSituation::Teleport:     return 100;  // ack the transfer promptly
        case TurnSituation::Combat:       return 400;  // reactions run between GCDs and casts
        case TurnSituation::Dead:         return 1000; // release, corpse run, revive steps
        case TurnSituation::Battleground: return 1000; // objectives and flags move fast
        case TurnSituation::Normal:       break;
    }
    return 3000;
}

// Wrap-safe: the world clock wraps after ~49 days of uptime.
inline bool TurnIsDue(uint32_t nowMs, uint32_t dueMs)
{
    return static_cast<int32_t>(nowMs - dueMs) >= 0;
}

// When a bot is due after a turn: the delay its AI asked for, capped by the
// situation. An arrival (TravelArrivalMakesDueAtOnce) passes 0 here, so the
// bot is due on the next tick.
inline uint32_t TurnNextDueMs(uint32_t nowMs, uint32_t aiDelayMs, TurnSituation situation)
{
    return nowMs + std::min(aiDelayMs, TurnMaxWaitMs(situation));
}

// A bot that just finished its walk is due at once, like a situation change,
// so a bot next to a player acts on arrival instead of waiting out its
// normal cap. wasEnRoute is whether the travel target was still on the way
// (READY or TRAVEL) before the turn, nowWorking whether it is being worked
// (WORK) after it. Only that edge counts: a failed move, drop, expiry,
// cooldown or re-pick never lands on WORK, so an unreachable target cannot
// re-pick itself in a tight loop.
inline bool TravelArrivalMakesDueAtOnce(bool wasEnRoute, bool nowWorking)
{
    return wasEnRoute && nowWorking;
}

// A bot that just finished its task is due at once too: the quest errand is
// done (its target expired) so the next tick picks the next one instead of
// waiting out the normal cap. wasWorking is whether the travel target was
// being worked (WORK) before the turn, nowExpired whether it expired after
// it. EXPIRED only: a drop nulls to NONE with the purpose parked, a cooldown
// waits, a MoveTo failure leaves the trip TRAVEL-side, an Unstuck reset or
// re-pick never expires - so none of them wakes the bot early.
inline bool TravelCompletionMakesDueAtOnce(bool wasWorking, bool nowExpired)
{
    return wasWorking && nowExpired;
}

struct DueTurn
{
    uint32_t guidLow;
    uint32_t dueMs;
    TurnSituation situation;
    // Within sight of a real player this pass (BotManager's per-pass cache):
    // served ahead of the rest when the budget cuts the pass.
    bool nearPlayer = false;
};

// Order due turns with player-visible bots first, then most overdue relative
// to tolerance: (now - due) / maxWait, compared by cross-multiplication so no
// floats. Ties go to the more urgent situation, then the lower guid
// (deterministic). Distant bots keep today's order inside their group.
inline void OrderDueTurns(std::vector<DueTurn>& due, uint32_t nowMs)
{
    std::sort(due.begin(), due.end(), [nowMs](DueTurn const& a, DueTurn const& b)
    {
        if (a.nearPlayer != b.nearPlayer)
            return a.nearPlayer > b.nearPlayer;
        uint64_t const lateA = static_cast<uint32_t>(nowMs - a.dueMs);
        uint64_t const lateB = static_cast<uint32_t>(nowMs - b.dueMs);
        uint64_t const scoreA = lateA * TurnMaxWaitMs(b.situation);
        uint64_t const scoreB = lateB * TurnMaxWaitMs(a.situation);
        if (scoreA != scoreB)
            return scoreA > scoreB;
        if (a.situation != b.situation)
            return a.situation > b.situation;
        return a.guidLow < b.guidLow;
    });
}

} // namespace TortoiseBots
