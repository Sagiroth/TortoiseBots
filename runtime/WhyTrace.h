#pragma once

// `.bot why <bot> [seconds]`: a GM's per-bot decision trace, for one bot and a
// short window (default 30 s, max 120). Every AI tick writes one server-log
// line with the fixed prefix `TBWHY|`: the chosen action and its relevance,
// the top five triggered-but-unchosen actions with the reason each lost, the
// current target and its victim, and (separately, at start and on change) the
// active strategies. Chat orders the bot receives are lines too.
//
// Off unless the command is used: while no trace runs every hook is one bool
// test, and the only permanent cost is one attached listener per engine of the
// traced bot, removed when the trace ends.
//
// The reason vocabulary and the 20-lines-per-second budget are
// runtime/WhyTracePolicy.h (standalone-tested). This service supplies what the
// policy cannot know without the game: the engine's outcome per action, the
// spell cooldown / global cooldown at verdict time, the bot's target and its
// victim.
//
// Reused plumbing: the engine's ActionExecutionListener::Outcome seam, the
// same one the run trace records through (runtime/RunTrace.cpp). The decision
// capture (runtime/DecisionCapture.*) records the run leader, not a bot's AI
// tick, and writes JSONL fixtures rather than live log lines, so it is not the
// path used here.
//
// Threading: Start, Stop and Update run on the world thread (the command queue
// and the world hook). The engine tick entry points (DoNextAction, TickScope)
// run on map threads, and they are safe without a lock because the world
// thread never runs while the maps tick, and only the traced bot writes the
// trace state from its own tick. One trace per server at a time.

#include "WhyTracePolicy.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Player;
class PlayerbotAI;

namespace TortoiseBots
{

class WhyTraceService
{
public:
    static WhyTraceService& Instance();

    bool Active() const { return session_.active(); }

    // World thread. False with `message` when the trace cannot start (one is
    // running, the bot has no AI, the AI is off). `seconds` is already parsed
    // and clamped (WhyTrace::ParseSeconds).
    bool Start(Player* bot, unsigned seconds, Player* requester, std::string& message);
    void Stop(WhyTrace::StopReason reason);

    // World thread, every world tick: the timeout, a bot that left the world,
    // the strategy-change lines.
    void Update(uint32_t diff);
    // Server shutdown: ends the trace without touching the bot's engines (they
    // are being torn down with the AI).
    void Shutdown();

    // One outcome the engine gave an action, mapped from ActionOutcome by the
    // listener; buffered for the tick line.
    struct Verdict
    {
        std::string name;
        uint8_t engineState = 0;
        float relevance = 0.0f;
        bool chosen = false;
        bool possible = true;
        bool useful = true;
        bool hasTarget = true;
        bool strategyActive = true;
        bool vetoed = false;
        bool backedOff = false;
        bool prereq = false;
        bool failed = false;
        WhyTrace::CastCheck cast = WhyTrace::CastCheck::None;
        uint32_t spellCooldownMs = 0;
        uint32_t gcdRemainingMs = 0;
    };
    void NoteVerdict(uint64_t botRaw, Verdict const& verdict);
    // Cheap test for the engine's once-per-tick snapshot hook.
    bool Traces(PlayerbotAI* ai) const;
    // The engine's queue right after this tick's triggers and default actions
    // were pushed, before the action loop: only these baskets are candidates
    // that could have lost. Work the loop itself pushes (a successful action's
    // continuers, a prerequisite's requeue) is future work, never a loser.
    void NoteTickStart(uint64_t botRaw, std::vector<std::pair<std::string, float>> const& pending);

    // Hook sites (PlayerbotAI): cheap when this bot is not the traced one.
    // `NoteOrder` is the chat text the bot received; `NoteOrderAction` is one
    // action run because somebody ordered it (the commander and run-leader
    // order helpers land here through PlayerbotAI::DoSpecificAction).
    void NoteOrder(PlayerbotAI* ai, Player* from, std::string const& text);
    void NoteOrderAction(PlayerbotAI* ai, Player* from, std::string const& action, std::string const& source);
    void FinishTick(PlayerbotAI* ai);

    // RAII around PlayerbotAI::DoNextAction, whose early returns must still
    // close the tick (the reaction pass's outcomes belong to it).
    class TickScope
    {
    public:
        explicit TickScope(PlayerbotAI* ai) : ai_(ai) {}
        ~TickScope();
    private:
        PlayerbotAI* ai_;
    };

    // Attaches / removes the outcome listener on every engine of one bot.
    // Call outside an engine's action loop (world thread).
    static void AttachListeners(PlayerbotAI* ai);
    static void DetachListeners(PlayerbotAI* ai);

private:
    WhyTraceService() = default;

    // One log line through the budget. False when the budget dropped it.
    bool Emit(std::string const& line, uint32_t nowMs);
    // One `TBWHY|strategies|...` line per engine whose strategy set changed.
    void EmitStrategies(PlayerbotAI* ai, uint32_t nowMs);
    std::string TickLine(PlayerbotAI* ai, uint32_t nowMs);
    void NotifyRequester(std::string const& text);
    void Reset();

    WhyTrace::Session session_;
    WhyTrace::LineBudget budget_;
    uint64_t botGuid_ = 0;
    std::string botName_;
    PlayerbotAI* attachedAi_ = nullptr;
    uint64_t requesterGuid_ = 0;
    std::string requesterName_;
    std::vector<Verdict> verdicts_;
    std::vector<std::pair<std::string, float>> tickStart_;
    // Last emitted strategy list per engine state, keyed by BotState's value.
    std::map<uint8_t, std::string> engineStrategies_;
    uint32_t strategyCheckAccumMs_ = 0;
    // The requester's trace start line is the only chat message besides the
    // summary, so the budget never hides the end.
    bool started_ = false;
};

} // namespace TortoiseBots
