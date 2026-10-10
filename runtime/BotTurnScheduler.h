#pragma once

// Turn scheduling for the random-pool AI pass (BotManager::UpdateBots).
//
// One rule instead of special passes. After each AI turn a bot is due again
// when its own AI asked to think next (reaction delay, GCD, cast time, move
// duration), but never later than its situation allows: in combat 400 ms,
// dead or in a battleground 1 s, anything else 3 s. A change of situation
// (pulled into combat, died, started a teleport) makes the bot due at once.
//
// Every tick the due bots are served most-overdue first, where "overdue" is
// measured against what the bot's situation tolerates, until the tick budget
// runs out. A bot left over keeps getting later and so climbs the order. Under
// load every bot is equally late relative to its tolerance: a fight never
// waits behind a questing walk, and no bot is skipped for good.
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
// situation.
inline uint32_t TurnNextDueMs(uint32_t nowMs, uint32_t aiDelayMs, TurnSituation situation)
{
    return nowMs + std::min(aiDelayMs, TurnMaxWaitMs(situation));
}

struct DueTurn
{
    uint32_t guidLow;
    uint32_t dueMs;
    TurnSituation situation;
};

// Order due turns most overdue first, relative to tolerance:
// (now - due) / maxWait, compared by cross-multiplication so no floats.
// Ties go to the more urgent situation, then the lower guid (deterministic).
inline void OrderDueTurns(std::vector<DueTurn>& due, uint32_t nowMs)
{
    std::sort(due.begin(), due.end(), [nowMs](DueTurn const& a, DueTurn const& b)
    {
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
