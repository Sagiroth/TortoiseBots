#pragma once

// Policy for the `.bot why` decision trace: why a triggered-but-unchosen
// action lost, the 20-lines-per-second budget, and the lifetime of one trace.
// Plain data and the standard library only, so tools/test_why_trace_policy.cpp
// drives it without the game. The service (runtime/WhyTrace.{h,cpp}) feeds it
// with what the engine and the bot read every tick.

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

namespace TortoiseBots
{
namespace WhyTrace
{

// One trace per server, from the command until its seconds run out (or the
// bot leaves the world). 120 s is two minutes of a boss attempt: enough for
// one pull, small enough that the log budget stays a few thousand lines.
constexpr unsigned kDefaultSeconds = 30;
constexpr unsigned kMaxSeconds = 120;
// Fixed server-log budget: every trace line (tick, strategy change, order)
// passes this, and what it drops is counted for the summary.
constexpr uint32_t kMaxLinesPerSecond = 20;
// The action names a tick line names besides the chosen one.
constexpr size_t kMaxLostCandidates = 5;
// Names are clipped to keep one tick on one bounded log line.
constexpr size_t kMaxNameChars = 48;
constexpr size_t kMaxOrderChars = 120;
// One tick cannot produce more useful verdicts than this; a runaway queue
// must not grow the trace's memory. The verdict that ran is the diagnosis:
// at the cap it displaces the oldest verdict that did not run, while the
// cap still drops further verdicts that did not run.
constexpr size_t kMaxTickVerdicts = 128;
// Bounded per-tick buffer shared with the service (runtime/WhyTrace.cpp).
// The newcomer joins unless the buffer is full; a full buffer still takes
// the verdict that ran in place of the oldest one that did not. Returns
// false when the newcomer was dropped.
template <typename Container, typename Verdict>
bool PushCappedVerdict(Container& kept, Verdict const& verdict, size_t cap = kMaxTickVerdicts)
{
    if (kept.size() < cap)
    {
        kept.push_back(verdict);
        return true;
    }
    if (!verdict.chosen)
        return false;
    for (auto it = kept.begin(); it != kept.end(); ++it)
        if (!it->chosen)
        {
            kept.erase(it);
            kept.push_back(verdict);
            return true;
        }
    return false;
}

// What an action that did not run lost on. The first eight are the vocabulary
// a GM reads for a boss attempt; the rest keep the engine's own gates
// distinguishable instead of folding them into "lower relevance".
enum class LossReason
{
    LowerRelevance,   // never popped: something with higher relevance ran first
    NotPossible,      // isPossible() said no, with nothing more specific
    Useless,          // isUseful() said no
    Cooldown,         // its spell is on cooldown
    Gcd,              // global cooldown or a cast still in flight
    OutOfRange,       // isPossible() refused on range
    NoTarget,         // no unit to act on
    StrategyInactive, // no attached strategy defines the action any more
    Vetoed,           // a multiplier zeroed its relevance
    Backoff,          // the failure backoff dropped it
    Prereq,           // its prerequisites were queued first
    Failed,           // it ran and Execute() returned false
    NotChosen,        // no verdict and no reason found (should not happen)
};

char const* LossReasonName(LossReason reason);

// A refusal of the cast check, classified so the policy does not include the
// core's SpellCastResult enum.
enum class CastCheck
{
    None,
    NotReady,
    OutOfRange,
    BadTargets,
};

// What the trace knows about one triggered action that was not chosen.
struct Candidate
{
    std::string name;
    float relevance = 0.0f;
    bool queuedOnly = false;      // still in the queue when the tick ended
    bool strategyActive = true;   // an attached strategy defines it
    bool hasTarget = true;        // the action's target resolves
    bool possible = true;
    bool useful = true;
    bool vetoed = false;
    bool backedOff = false;
    bool prereq = false;
    bool failed = false;
    CastCheck cast = CastCheck::None;
    uint32_t spellCooldownMs = 0;
    uint32_t gcdRemainingMs = 0;
};

// The order is the diagnosis: the structural answers first, then the concrete
// cast refusal, then the engine's own isUseful/isPossible verdicts.
LossReason Classify(Candidate const& candidate);

// Bounded name for a log line; a pipe would split the field, so it becomes '/'.
std::string ClipName(std::string const& text, size_t max = kMaxNameChars);

// One trace line per AI tick, strategy change or order. The budget is a fixed
// window of one second from the first line taken, not a sliding one: a burst
// of ticks can spend the window at once, and the next full second opens the
// next window.
class LineBudget
{
public:
    bool Take(uint32_t nowMs)
    {
        if (!started_ || nowMs - windowStartMs_ >= 1000)
        {
            started_ = true;
            windowStartMs_ = nowMs;
            taken_ = 0;
        }
        if (taken_ >= kMaxLinesPerSecond)
        {
            ++dropped_;
            return false;
        }
        ++taken_;
        return true;
    }

    uint32_t dropped() const { return dropped_; }

private:
    bool started_ = false;
    uint32_t windowStartMs_ = 0;
    uint32_t taken_ = 0;
    uint32_t dropped_ = 0;
};

enum class StopReason
{
    None,
    Timeout,
    BotGone,
    ServerShutdown,
};

char const* StopReasonName(StopReason reason);

// Start, stop and timeout of one trace, plus the tallies its summary reports.
// Monotonic milliseconds (WorldTimer) only.
class Session
{
public:
    // False when a trace is already running.
    bool Start(uint32_t nowMs, unsigned seconds)
    {
        if (active_)
            return false;
        active_ = true;
        startedMs_ = nowMs;
        seconds_ = seconds;
        stopReason_ = StopReason::None;
        ticks_ = 0;
        chosen_.clear();
        lost_.clear();
        return true;
    }

    void Stop(StopReason reason, uint32_t nowMs)
    {
        if (!active_)
            return;
        active_ = false;
        stopReason_ = reason;
        endedMs_ = nowMs;
    }

    bool active() const { return active_; }
    unsigned seconds() const { return seconds_; }
    uint32_t ticks() const { return ticks_; }
    StopReason stopReason() const { return stopReason_; }
    uint32_t RemainingMs(uint32_t nowMs) const
    {
        if (!active_)
            return 0;
        uint32_t const budget = seconds_ * 1000u;
        uint32_t const spent = nowMs - startedMs_;
        return spent >= budget ? 0 : budget - spent;
    }

    bool ShouldStop(uint32_t nowMs) const { return active_ && RemainingMs(nowMs) == 0; }
    uint32_t ElapsedMs(uint32_t nowMs) const { return active_ ? nowMs - startedMs_ : 0; }

    void NoteTick(uint32_t /*nowMs*/) { ++ticks_; }
    void NoteChosen(std::string const& name, uint32_t /*nowMs*/) { ++chosen_[name]; }
    void NoteLoss(LossReason reason, uint32_t /*nowMs*/) { ++lost_[reason]; }

    uint32_t chosenCount(std::string const& name) const
    {
        auto it = chosen_.find(name);
        return it == chosen_.end() ? 0 : it->second;
    }

    uint32_t lossCount(LossReason reason) const
    {
        auto it = lost_.find(reason);
        return it == lost_.end() ? 0 : it->second;
    }

    std::map<std::string, uint32_t> const& chosenTallies() const { return chosen_; }
    std::map<LossReason, uint32_t> const& lossTallies() const { return lost_; }

    // The line the GM reads when the trace ends: what ran, what lost and why,
    // in the same words as the log lines.
    std::string Summary(std::string const& botName) const;

private:
    bool active_ = false;
    unsigned seconds_ = kDefaultSeconds;
    uint32_t startedMs_ = 0;
    uint32_t endedMs_ = 0;
    StopReason stopReason_ = StopReason::None;
    uint32_t ticks_ = 0;
    std::map<std::string, uint32_t> chosen_;
    std::map<LossReason, uint32_t> lost_;
};

inline char const* LossReasonName(LossReason reason)
{
    switch (reason)
    {
        case LossReason::LowerRelevance: return "lower-relevance";
        case LossReason::NotPossible: return "not-possible";
        case LossReason::Useless: return "not-useful";
        case LossReason::Cooldown: return "cooldown";
        case LossReason::Gcd: return "gcd";
        case LossReason::OutOfRange: return "out-of-range";
        case LossReason::NoTarget: return "no-target";
        case LossReason::StrategyInactive: return "strategy-inactive";
        case LossReason::Vetoed: return "vetoed";
        case LossReason::Backoff: return "backoff";
        case LossReason::Prereq: return "prereq";
        case LossReason::Failed: return "failed";
        case LossReason::NotChosen: return "not-chosen";
    }
    return "not-chosen";
}

inline LossReason Classify(Candidate const& candidate)
{
    if (candidate.queuedOnly)
        return LossReason::LowerRelevance;
    if (!candidate.strategyActive)
        return LossReason::StrategyInactive;
    if (candidate.backedOff)
        return LossReason::Backoff;
    if (candidate.vetoed)
        return LossReason::Vetoed;
    if (candidate.prereq)
        return LossReason::Prereq;
    if (candidate.failed)
        return LossReason::Failed;
    if (!candidate.hasTarget)
        return LossReason::NoTarget;
    // The engine asks isUseful() before isPossible(), so a useless action never
    // reached a cooldown or cast check; its own verdict wins.
    if (!candidate.useful)
        return LossReason::Useless;
    if (!candidate.possible)
    {
        // A spell cooldown is checked before the cast result: the core answers
        // NOT_READY for both a cooldown and the global cooldown, and only the
        // spell's own cooldown map tells them apart.
        if (candidate.spellCooldownMs > 0)
            return LossReason::Cooldown;
        if (candidate.gcdRemainingMs > 0)
            return LossReason::Gcd;
        switch (candidate.cast)
        {
            case CastCheck::OutOfRange: return LossReason::OutOfRange;
            case CastCheck::BadTargets: return LossReason::NoTarget;
            case CastCheck::NotReady: return LossReason::Gcd;
            case CastCheck::None: break;
        }
        return LossReason::NotPossible;
    }
    return LossReason::NotChosen;
}

inline std::string ClipName(std::string const& text, size_t max)
{
    std::string out = text.substr(0, max);
    for (char& c : out)
        if (c == '|' || c == '\n' || c == '\r' || c == '\t')
            c = '/';
    return out;
}

// The tick line's fixed schema. A reader parses by field name and never needs
// to reflow: `-` stands for an absent chosen action, target, victim or loss.
inline std::string FormatTarget(std::string const& guid, std::string const& name)
{
    if (guid.empty())
        return "-";
    return ClipName(guid) + ":" + ClipName(name);
}

inline std::string FormatFixed(float value, int decimals)
{
    char text[24];
    if (decimals == 3)
        std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(value));
    else
        std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(value));
    return text;
}

inline std::string FormatLostEntry(std::string const& name, float relevance, LossReason reason)
{
    return ClipName(name) + "(" + FormatFixed(relevance, 2) + "," + LossReasonName(reason) + ")";
}

inline std::string FormatTickLine(std::string const& bot, uint32_t elapsedMs, uint32_t tick,
    std::string const& state, std::string const& chosenName, float chosenRelevance, bool hasChosen,
    std::string const& target, std::string const& victim, std::string const& lost)
{
    std::string chosen = "-";
    if (hasChosen)
        chosen = ClipName(chosenName) + "(" + FormatFixed(chosenRelevance, 2) + ")";
    return "TBWHY|tick|" + ClipName(bot) + "|t=" + FormatFixed(static_cast<float>(elapsedMs) / 1000.0f, 3) +
        "|n=" + std::to_string(tick) + "|st=" + ClipName(state) + "|chosen=" + chosen +
        "|tgt=" + (target.empty() ? "-" : ClipName(target, 80)) +
        "|victim=" + (victim.empty() ? "-" : ClipName(victim, 80)) + "|lost=" + (lost.empty() ? "-" : lost);
}

inline char const* StopReasonName(StopReason reason)
{
    switch (reason)
    {
        case StopReason::None: return "none";
        case StopReason::Timeout: return "timeout";
        case StopReason::BotGone: return "bot-gone";
        case StopReason::ServerShutdown: return "shutdown";
    }
    return "none";
}

// 0 combat, 1 non-combat, 2 dead, 3 reaction (BotState's values); "all" for
// anything else.
inline char const* EngineStateName(uint8_t state)
{
    switch (state)
    {
        case 0: return "combat";
        case 1: return "non-combat";
        case 2: return "dead";
        case 3: return "reaction";
        default: return "all";
    }
}

inline unsigned ClampSeconds(int seconds)
{
    if (seconds < 1)
        return 1;
    if (static_cast<unsigned>(seconds) > kMaxSeconds)
        return kMaxSeconds;
    return static_cast<unsigned>(seconds);
}

// Empty text keeps the default; a number is clamped; anything else is refused.
inline bool ParseSeconds(std::string const& text, unsigned& seconds)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    if (begin == end)
    {
        seconds = kDefaultSeconds;
        return true;
    }
    for (size_t i = begin; i < end; ++i)
        if (!std::isdigit(static_cast<unsigned char>(text[i])))
            return false;
    long value = std::strtol(text.substr(begin, end - begin).c_str(), nullptr, 10);
    seconds = ClampSeconds(static_cast<int>(value > 100000 ? 100000 : value));
    return true;
}

inline std::string Session::Summary(std::string const& botName) const
{
    std::string out = botName + ": " + std::to_string(ticks_) + " AI tick(s) traced, stopped (" +
        StopReasonName(stopReason_) + ").";

    out += " Chosen:";
    if (chosen_.empty())
        out += " none";
    else
    {
        size_t shown = 0;
        for (auto const& entry : chosen_)
        {
            if (shown++ >= kMaxLostCandidates)
                break;
            out += " " + entry.first + " x" + std::to_string(entry.second);
        }
    }

    out += ". Lost:";
    if (lost_.empty())
        out += " none";
    else
    {
        size_t shown = 0;
        for (auto const& entry : lost_)
        {
            if (shown++ >= kMaxLostCandidates)
                break;
            out += " " + std::string(LossReasonName(entry.first)) + " x" + std::to_string(entry.second);
        }
    }
    out += ".";
    return out;
}

} // namespace WhyTrace
} // namespace TortoiseBots
