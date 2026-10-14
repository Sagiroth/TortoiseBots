#pragma once

// Remote commander (issue #884): `cmd enter <area trigger id | near> [bot]`, the
// order that takes the bound party through a dungeon entrance. A headless bot never
// sends its own CMSG_AREATRIGGER, so a remote driver cannot walk it into a dungeon
// (Zul'Farrak has no inside game_tele); the order walks each bot to the trigger and then
// hands the core the same area trigger packet a client would send for it, so every
// requirement, bind and limit of the core's own entry path applies.
//
// Everything that decides is a pure function here, so tools/test_remote_commander_enter.cpp
// exercises it standalone: the argument parsing, the choice of the nearest entrance, the
// requirement verdict (the readable reason of a refusal; the core stays the judge), the
// per-bot walk/wait/arrive state machine with its budgets, and the tally that turns the
// bots' results into the order's single final answer. The glue is in
// commands/BotCommands.cpp.
//
// Plain data and the standard library only. World thread only, like the rest of the commander.

#include "RemoteCommanderTasks.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots {
namespace RemoteCommander {

// A bot is called in range of the trigger from inside its zone grown by this many
// yards. The core accepts a packet from 5 yd outside the zone, so a bot that is in range
// here is always accepted by the core's range check.
constexpr float kEnterTriggerMargin = 3.0f;
// After the packet: how long the transfer (the instance is created or loaded) may take
// until the bot stands in the dungeon.
constexpr uint32_t kEnterArriveMs = 30000;

// Result codes beyond the shared set of RemoteCommanderTasks.h.
// The entrance asks for something the bot lacks (level, key, quest, condition, raid group, content phase).
constexpr char const* kCodeRequirement = "requirement";
// The bot's account has entered too many instances this hour (the core's limit).
constexpr char const* kCodeInstanceLimit = "instance-limit";

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

struct EnterOrder
{
    bool nearest = false;   // `near`: the nearest instance entrance to the lead bot
    uint32_t triggerId = 0; // the area trigger id (0 for `near`)
    std::string bot;        // the one bot named, else every bot of the party
};

// An area trigger id: decimal, 1 to 999999.
inline bool ParseTriggerId(std::string const& token, uint32_t& out)
{
    out = 0;
    if (token.empty() || token.size() > 6)
        return false;
    uint32_t value = 0;
    for (char c : token)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    if (!value)
        return false;
    out = value;
    return true;
}

// cmd enter <trigger id | near> [bot]
inline bool ParseEnterOrder(std::vector<std::string> const& parts, EnterOrder& out)
{
    if (parts.empty() || parts.size() > 2)
        return false;
    EnterOrder order;
    std::string first = parts[0];
    for (char& c : first)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (first == "near")
        order.nearest = true;
    else if (!ParseTriggerId(parts[0], order.triggerId))
        return false;
    if (parts.size() == 2)
    {
        if (parts[1].empty())
            return false;
        order.bot = parts[1];
    }
    out = order;
    return true;
}

// ---------------------------------------------------------------------------
// Which trigger
// ---------------------------------------------------------------------------

// An instance entrance is an area trigger that teleports into a dungeon map other than
// the one it stands in (the exit triggers inside a dungeon lead to a continent).
inline bool IsInstanceEntrance(bool hasTeleport, bool destinationIsDungeon, uint32_t triggerMapId,
    uint32_t destinationMapId)
{
    return hasTeleport && destinationIsDungeon && triggerMapId != destinationMapId;
}

struct TriggerCandidate
{
    uint32_t id = 0;
    float distance = 0.0f; // from the reference bot to the trigger's centre
};

// `near`: the closest candidate within the react distance; equally near ones go to the
// lowest id, so the same world always gives the same answer.
inline bool PickNearestTrigger(std::vector<TriggerCandidate> const& candidates, float reactDistance, uint32_t& id)
{
    bool found = false;
    TriggerCandidate best;
    for (TriggerCandidate const& candidate : candidates)
    {
        if (!candidate.id || !(candidate.distance <= reactDistance))
            continue;
        if (!found || candidate.distance < best.distance ||
            (candidate.distance == best.distance && candidate.id < best.id))
        {
            best = candidate;
            found = true;
        }
    }
    if (found)
        id = best.id;
    return found;
}

// ---------------------------------------------------------------------------
// Requirement verdict
// ---------------------------------------------------------------------------

// What the core's own entry path asks of a player (WorldSession::HandleAreaTriggerOpcode,
// MapManager::CanPlayerEnter), read before the packet goes so a refusal can say why. The
// core stays the judge: this never lets a bot in, it only names the reason of a refusal
// the core would give anyway (a bot that passes here is still refused by the core when
// the instance is full, locked by an encounter, or a script says no).
struct EntryFacts
{
    bool gm = false;
    bool phaseOpen = true;           // the server's content phase has reached the trigger's phase
    uint32_t requiredPhase = 0;
    uint32_t level = 0;
    uint32_t requiredLevel = 0;
    bool ignoreLevel = false;        // Instance.IgnoreLevel
    bool lunaticOverride = false;    // a Lunatic challenger on one of the maps that ignore the level
    bool conditionFailed = false;    // the trigger's condition (key, quest, ...) is not met
    std::string conditionMessage;    // the trigger's own message for it ("" = none)
    bool raidMap = false;
    bool ignoreRaid = false;         // Instance.IgnoreRaid
    bool inRaidGroup = false;
    bool instanceCountOk = true;     // the account may enter one more instance this hour
};

struct EntryVerdict
{
    bool ok = true;
    char const* code = "";
    std::string message;
};

inline EntryVerdict RefuseEntry(char const* code, std::string const& message)
{
    EntryVerdict verdict;
    verdict.ok = false;
    verdict.code = code;
    verdict.message = message;
    return verdict;
}

// The checks in the core's order: content phase, level and condition (a GameMaster
// skips both), the raid group of a raid map, the per-account instance limit.
inline EntryVerdict JudgeEntry(EntryFacts const& facts)
{
    if (!facts.phaseOpen)
        return RefuseEntry(kCodeRequirement, "Not open yet: the server's content phase is below phase " +
            std::to_string(facts.requiredPhase + 1) + ", which this instance needs.");
    if (!facts.gm)
    {
        if (facts.level < facts.requiredLevel && !facts.ignoreLevel && !facts.lunaticOverride)
            return RefuseEntry(kCodeRequirement, "Needs level " + std::to_string(facts.requiredLevel) +
                " (the bot is level " + std::to_string(facts.level) + ").");
        if (facts.conditionFailed)
            return RefuseEntry(kCodeRequirement, facts.conditionMessage.empty()
                ? std::string("The bot does not meet this entrance's requirement (a key, quest or other condition).")
                : facts.conditionMessage);
        if (facts.raidMap && !facts.ignoreRaid && !facts.inRaidGroup)
            return RefuseEntry(kCodeRequirement, "A raid instance needs a raid group.");
        if (!facts.instanceCountOk)
            return RefuseEntry(kCodeInstanceLimit, "The bot's account has entered too many instances this hour.");
    }
    return EntryVerdict();
}

// ---------------------------------------------------------------------------
// One bot's way in
// ---------------------------------------------------------------------------

enum class EnterStage : uint8_t
{
    Walk,     // closing in on the trigger
    WaitLead, // a follower in range, holding until the lead bot is in
    Arrive,   // the packet is sent; waiting for the bot to stand in the dungeon
};

struct EnterState
{
    EnterStage stage = EnterStage::Walk;
    bool follower = false; // waits for the lead bot to get in first
    uint32_t elapsedMs = 0;
    uint32_t stageMs = 0;
    uint32_t stalledMs = 0;
    float bestDistance = 1.0e9f;
    uint32_t walkBudgetMs = kMinWalkMs;
    uint32_t leadWaitBudgetMs = kMaxWalkMs + kEnterArriveMs; // follower: total time from the start
};

// maxDistance: the farthest bot of the order from the trigger (every bot gets the same
// budgets, so a follower never gives up before the lead bot could have walked).
inline EnterState MakeEnterState(float distance, float maxDistance, bool follower)
{
    EnterState state;
    state.follower = follower;
    state.walkBudgetMs = WalkBudgetMs(distance);
    state.leadWaitBudgetMs = WalkBudgetMs(maxDistance) + kEnterArriveMs;
    return state;
}

// The budget the PENDING line announces: the longest walk, the lead bot's transfer and,
// with followers, theirs.
inline uint32_t EnterBudgetMs(float maxDistance, bool hasFollowers)
{
    return WalkBudgetMs(maxDistance) + kEnterArriveMs * (hasFollowers ? 2u : 1u);
}

struct EnterFacts
{
    bool available = true;   // in world, alive, not teleporting, not on a taxi
    bool inTransit = false;  // alive but out of the world or in a teleport (the transfer under way)
    bool inCombat = false;
    bool inRange = false;    // inside the trigger zone grown by kEnterTriggerMargin
    float distance = 0.0f;   // to the trigger's centre
    bool arrived = false;    // in the world on the dungeon's map
    bool leadEntered = false;
    bool leadFailed = false;
};

enum class EnterStep : uint8_t
{
    Wait,
    SendTrigger, // in range and cleared to go: hand the core the area trigger now
    Succeed,
    Fail,
};

struct EnterVerdict
{
    EnterStep step = EnterStep::Wait;
    char const* code = "";
    char const* message = "";
};

inline EnterVerdict EnterFail(char const* code, char const* message)
{
    EnterVerdict verdict;
    verdict.step = EnterStep::Fail;
    verdict.code = code;
    verdict.message = message;
    return verdict;
}

// One look at a running entry, `diffMs` after the last.
inline EnterVerdict AdvanceEnter(EnterState& state, EnterFacts const& facts, uint32_t diffMs)
{
    state.elapsedMs += diffMs;
    state.stageMs += diffMs;
    EnterVerdict verdict;

    if (state.stage == EnterStage::Arrive)
    {
        if (facts.arrived)
        {
            verdict.step = EnterStep::Succeed;
            return verdict;
        }
        if (facts.inTransit)
        {
            if (state.stageMs >= kEnterArriveMs)
                return EnterFail(kCodeTimeout, "The bot did not arrive in the dungeon in time.");
            return verdict;
        }
        return EnterFail(kCodeFailed, "The entry did not go through: the bot is still outside.");
    }

    if (state.follower && facts.leadFailed)
        return EnterFail(kCodeCancelled, "The lead bot did not get in, so the rest stay out.");
    if (!facts.available)
        return EnterFail(kCodeUnavailable, "The bot died, left the map or is teleporting.");
    if (facts.inCombat)
        return EnterFail(kCodeInCombat, "The bot is in combat and stopped.");

    if (state.stage == EnterStage::WaitLead)
    {
        if (facts.leadEntered)
        {
            state.stage = EnterStage::Arrive;
            state.stageMs = 0;
            verdict.step = EnterStep::SendTrigger;
            return verdict;
        }
        if (state.elapsedMs >= state.leadWaitBudgetMs)
            return EnterFail(kCodeTimeout, "The lead bot did not get in in time.");
        return verdict;
    }

    // Walk
    if (facts.inRange)
    {
        state.stageMs = 0;
        if (state.follower && !facts.leadEntered)
        {
            state.stage = EnterStage::WaitLead;
            return verdict;
        }
        state.stage = EnterStage::Arrive;
        verdict.step = EnterStep::SendTrigger;
        return verdict;
    }
    if (facts.distance < state.bestDistance - kProgressYards)
    {
        state.bestDistance = facts.distance;
        state.stalledMs = 0;
    }
    else
        state.stalledMs += diffMs;
    if (state.stalledMs >= kStallMs)
        return EnterFail(kCodeUnreachable, "The bot stopped short of the entrance and cannot get closer.");
    if (state.elapsedMs >= state.walkBudgetMs)
        return EnterFail(kCodeTimeout, "The bot did not reach the entrance in time.");
    return verdict;
}

// ---------------------------------------------------------------------------
// The order's answer
// ---------------------------------------------------------------------------

// The bots' results, collected until all have reported; then one final line.
struct EnterTally
{
    uint32_t total = 0;
    uint32_t reported = 0;
    uint32_t entered = 0;
    bool leadEntered = false; // the lead bot stands in the dungeon: the followers may go
    bool leadFailed = false;  // the lead bot will not get in: the followers stay out
    bool hasFailure = false;
    std::string failCode;
    std::string failMessage;  // "<Bot>: <message>" of the newest-ranking failure
};

inline void NoteEntered(EnterTally& tally, bool isLead)
{
    ++tally.reported;
    ++tally.entered;
    if (isLead)
        tally.leadEntered = true;
}

// The first real failure names the answer; a plain `cancelled` (a later order, or the
// followers staying out after the lead failed) never hides a cause.
inline void NoteFailed(EnterTally& tally, bool isLead, std::string const& botName, std::string const& code,
    std::string const& message)
{
    ++tally.reported;
    if (isLead)
        tally.leadFailed = true;
    bool const cancelled = code == kCodeCancelled;
    if (!tally.hasFailure || (tally.failCode == kCodeCancelled && !cancelled))
    {
        tally.hasFailure = true;
        tally.failCode = code;
        tally.failMessage = botName + ": " + message;
    }
}

inline bool EnterComplete(EnterTally const& tally)
{
    return tally.reported >= tally.total;
}

struct EnterAnswer
{
    bool ok = false;
    uint32_t count = 0;   // ACK: the bots that stand in the dungeon
    std::string detail;   // ACK: "entered"
    std::string code;     // ERR
    std::string message;  // ERR
};

inline EnterAnswer FinalEnterAnswer(EnterTally const& tally)
{
    EnterAnswer answer;
    answer.count = tally.entered;
    if (!tally.hasFailure && tally.entered == tally.total)
    {
        answer.ok = true;
        answer.detail = "entered";
        return answer;
    }
    answer.code = tally.hasFailure ? tally.failCode : std::string(kCodeFailed);
    answer.message = tally.hasFailure ? tally.failMessage : std::string("Not every bot got in.");
    if (tally.entered)
        answer.message += " (" + std::to_string(tally.entered) + " of " + std::to_string(tally.total) + " got in)";
    return answer;
}

} // namespace RemoteCommander
} // namespace TortoiseBots
