#pragma once

// Run trace, record layer: the "bt-trace v1" JSONL records, and the two small
// decisions that shape what is recorded (merging repeated action outcomes, and
// spotting an action failure loop as it happens).
//
// Plain data and the standard library only, so tools/test_run_trace_records.cpp
// builds it without a single game header. The world glue that fills the structs
// is runtime/RunTrace.cpp; the file writer is runtime/RunTraceWriter.h. The
// schema is documented in docs/server/run-trace.md and scored offline by
// tools/trace_score.py.
//
// Provenance: the record kinds, the flat one-object-per-line layout, the
// "every record carries the run id" rule and the idea of scoring a run from its
// trace come from mod-dungeon-clear's Pull Lab (jrad7/mod-dungeon-clear @
// da0bdfbc, src/Lab/DcLabTrace.*, AGPL-3.0-or-later). This code is a
// reimplementation for the 1.12 module, provenance in its PR.
//
// File layout, one flat JSON object per line, `k` names the kind:
//   header   once: run id, scope, interval, map, build
//   party    one per scoped bot (again when a bot joins the scope)
//   mobs     one per hostile the first time a bot sees it (registry)
//   frame    every interval: all scoped bots + the hostiles they see
//   event    deaths, combat enter/leave, loot, action loops, notes
//   action   an executed action with its outcome (repeats merged: n, tl)
//   end      once: reason + counters
// Every record carries "v":1, "k", "run", "src":"server" and "t" (ms since the
// trace started). GUIDs are "0x..." hex strings (a 64-bit GUID does not survive
// a JSON double).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace RunTrace {

constexpr int kSchemaVersion = 1;
constexpr char kSchemaName[] = "bt-trace";

// What this producer records, written into the header ("caps") so a scorer can
// tell "nothing happened" from "cannot be seen":
//   facing      per bot "fa": degrees between its facing and its hostile target
//   moving      per bot "mv": a movement generator is active
//   interrupts  event cast_interrupted (own cast cancelled), with "s":"moving" when the bot was moving
//   in_fire     per bot "fr": spell id of a harmful ground / area aura it stands in
constexpr char kCapsJson[] = "[\"facing\",\"moving\",\"interrupts\",\"in_fire\"]";

// AiPlayerbot.RunTrace.IntervalMs: default, and the clamp applied to it.
constexpr uint32_t kDefaultIntervalMs = 500;
constexpr uint32_t kMinIntervalMs = 250;
constexpr uint32_t kMaxIntervalMs = 10000;
// AiPlayerbot.RunTrace.MaxFileMb: default, and the clamp applied to it.
constexpr uint32_t kDefaultMaxFileMb = 64;
constexpr uint32_t kMinMaxFileMb = 1;
constexpr uint32_t kMaxMaxFileMb = 1024;
// Hostiles per frame, and units in the registry, per run.
constexpr size_t kMaxMobsPerFrame = 24;
constexpr size_t kMaxRegistryMobs = 1024;
// Threat entries kept per engaged hostile.
constexpr size_t kThreatTopN = 3;
// Bots in the scope of one run.
constexpr size_t kMaxScopeBots = 40;

inline uint32_t ClampIntervalMs(int64_t ms)
{
    if (ms < static_cast<int64_t>(kMinIntervalMs))
        return kMinIntervalMs;
    if (ms > static_cast<int64_t>(kMaxIntervalMs))
        return kMaxIntervalMs;
    return static_cast<uint32_t>(ms);
}

inline uint32_t ClampMaxFileMb(int64_t mb)
{
    if (mb < static_cast<int64_t>(kMinMaxFileMb))
        return kMinMaxFileMb;
    if (mb > static_cast<int64_t>(kMaxMaxFileMb))
        return kMaxMaxFileMb;
    return static_cast<uint32_t>(mb);
}

// A run id becomes a file name: letters, digits, '.', '_' and '-', 1..64
// characters, not starting with '.'.
inline bool ValidRunId(std::string_view id)
{
    if (id.empty() || id.size() > 64 || id[0] == '.')
        return false;
    for (char c : id)
    {
        bool const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-';
        if (!ok)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// JSON line writer
// ---------------------------------------------------------------------------

inline std::string JsonEscape(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (char ch : s)
    {
        unsigned char const c = static_cast<unsigned char>(ch);
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                    out += ch;
        }
    }
    return out;
}

// Fixed-point text without printf's locale: 12.5 -> "12.5", -3.04 with 1
// decimal -> "-3.0". Non-finite input reads as 0.
inline std::string FixedStr(double v, int decimals)
{
    if (!(v == v) || v > 1e15 || v < -1e15)
        v = 0.0;
    int64_t scale = 1;
    for (int i = 0; i < decimals; ++i)
        scale *= 10;
    bool const neg = v < 0;
    double const mag = neg ? -v : v;
    int64_t scaled = static_cast<int64_t>(mag * static_cast<double>(scale) + 0.5);
    std::string out;
    if (neg && scaled != 0)
        out += '-';
    out += std::to_string(scaled / scale);
    if (decimals > 0)
    {
        std::string frac = std::to_string(scaled % scale);
        out += '.';
        out.append(static_cast<size_t>(decimals) - frac.size(), '0');
        out += frac;
    }
    return out;
}

inline std::string GuidHex(uint64_t guid)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(guid));
    return buf;
}

// One flat JSON object. Chainable; Str() closes it.
class Line
{
public:
    Line() { s_.reserve(160); s_ += '{'; }
    Line& Str(char const* key, std::string_view v)
    {
        Key(key);
        s_ += '"';
        s_ += JsonEscape(v);
        s_ += '"';
        return *this;
    }
    Line& U(char const* key, uint64_t v) { Key(key); s_ += std::to_string(v); return *this; }
    Line& I(char const* key, int64_t v) { Key(key); s_ += std::to_string(v); return *this; }
    Line& F(char const* key, double v, int decimals) { Key(key); s_ += FixedStr(v, decimals); return *this; }
    Line& B(char const* key, bool v) { Key(key); s_ += v ? "true" : "false"; return *this; }
    Line& Guid(char const* key, uint64_t guid) { return Str(key, GuidHex(guid)); }
    // A value that is already JSON (an array or object built by the caller).
    Line& Raw(char const* key, std::string const& json) { Key(key); s_ += json; return *this; }
    // Already-rendered members (`"a":1,"b":"x"`, no braces); nothing when empty.
    Line& Members(std::string const& members)
    {
        if (members.empty())
            return *this;
        if (s_.size() > 1)
            s_ += ',';
        s_ += members;
        return *this;
    }
    std::string End() { s_ += '}'; return std::move(s_); }

private:
    void Key(char const* key)
    {
        if (s_.size() > 1)
            s_ += ',';
        s_ += '"';
        s_ += key;
        s_ += "\":";
    }
    std::string s_;
};

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

enum class ActionOutcome : uint8_t
{
    Ok,         // executed
    Failed,     // possible, ran, returned false
    Impossible, // isPossible() said no
    Useless,    // isUseful() said no
    Prereq,     // its prerequisites were queued first
    Vetoed,     // a multiplier zeroed its relevance
    Backoff     // the failure backoff dropped it
};

inline char const* OutcomeName(ActionOutcome r)
{
    switch (r)
    {
        case ActionOutcome::Ok: return "OK";
        case ActionOutcome::Failed: return "FAILED";
        case ActionOutcome::Impossible: return "IMPOSSIBLE";
        case ActionOutcome::Useless: return "USELESS";
        case ActionOutcome::Prereq: return "PREREQ";
        case ActionOutcome::Vetoed: return "VETOED";
        case ActionOutcome::Backoff: return "BACKOFF";
    }
    return "UNKNOWN";
}

inline bool OutcomeIsFailure(ActionOutcome r)
{
    return r == ActionOutcome::Failed || r == ActionOutcome::Impossible;
}

struct Header
{
    std::string runId;
    std::string scope;      // "party" | "bot"
    std::string scopeName;  // the requester's party leader, or the bot's name
    std::string requester;  // who started it
    std::string tbVersion;
    uint64_t startUnix = 0;
    uint32_t intervalMs = 0;
    uint64_t maxBytes = 0;
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
};

struct PartyMember
{
    uint64_t guid = 0;
    std::string name;
    std::string cls;   // class token: warrior, priest, ...
    std::string role;  // tank | heal | dps
    uint32_t level = 0;
};

struct Mob
{
    uint64_t guid = 0;
    uint32_t entry = 0;
    std::string name;
    uint32_t level = 0;
    uint32_t rank = 0;  // CreatureEliteType: 0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare
    bool boss = false;
};

struct BotSample
{
    uint64_t guid = 0;
    bool alive = true;
    bool inCombat = false;
    char state = 'n';  // c combat engine, n non-combat, d dead, r reaction
    uint32_t hp = 0, maxHp = 0;
    uint32_t power = 0, maxPower = 0;
    uint8_t powerType = 0;  // 0 mana, 1 rage, 2 focus, 3 energy
    float x = 0, y = 0, z = 0;
    uint64_t target = 0;
    uint32_t casting = 0;   // spell id, 0 = none
    bool moving = false;    // a movement generator is active
    int facing = -1;        // degrees (0..180) between its facing and its hostile target; -1 = no target
    uint32_t fire = 0;      // spell id of a harmful ground / area aura it stands in, 0 = none
    std::string action;     // last executed action
    std::string strategy;   // engine strategies; empty = unchanged since the last frame
};

struct ThreatEntry
{
    uint64_t guid = 0;
    uint32_t pct = 0;  // of the top entry
};

struct MobSample
{
    uint64_t guid = 0;
    uint32_t hpPct = 0;
    float x = 0, y = 0, z = 0;
    uint64_t target = 0;
    uint32_t casting = 0;
    bool inCombat = false;
    bool evading = false;
    bool cc = false;
    std::vector<ThreatEntry> threat;  // top entries, highest first
};

struct Event
{
    std::string ev;       // death | combat_on | combat_off | loot | action_loop | note
    uint64_t a = 0;       // the bot (or unit)
    uint64_t b = 0;       // the killer / victim
    int64_t v = 0;
    std::string s;
    std::string s2;
};

struct ActionRecord
{
    uint64_t guid = 0;
    std::string name;
    std::string source;      // trigger that queued it, "" for a default action
    ActionOutcome outcome = ActionOutcome::Ok;
    float relevance = 0.0f;
    uint32_t t = 0;          // first occurrence
    uint32_t tLast = 0;      // last occurrence merged in
    uint32_t n = 1;          // occurrences merged
};

struct Counters
{
    uint64_t frames = 0, events = 0, actions = 0, party = 0, mobs = 0;
    uint64_t deaths = 0, loops = 0;
    uint64_t records = 0, bytes = 0, dropped = 0;
    bool truncated = false;  // the size cap stopped the file
};

struct EndInfo
{
    std::string reason;  // stop | scope_lost | size_cap | shutdown | disabled
    Counters counters;
};

// Builds the lines of one run; the run id is stamped on every one.
class Builder
{
public:
    explicit Builder(std::string runId) : run_(std::move(runId)) {}

    std::string const& RunId() const { return run_; }

    std::string BuildHeader(Header const& h) const
    {
        Line l = Open("header", 0);
        l.Str("schema", kSchemaName).Str("scope", h.scope).Str("scope_name", h.scopeName)
            .Str("requester", h.requester).Str("tb_version", h.tbVersion)
            .U("start_unix", h.startUnix).U("interval_ms", h.intervalMs).U("max_bytes", h.maxBytes)
            .U("map", h.mapId).U("instance", h.instanceId).Raw("caps", kCapsJson);
        return l.End();
    }

    std::string BuildParty(uint32_t t, PartyMember const& m) const
    {
        Line l = Open("party", t);
        l.Guid("g", m.guid).Str("name", m.name).Str("class", m.cls).Str("role", m.role).U("level", m.level);
        return l.End();
    }

    std::string BuildMobs(uint32_t t, Mob const& m) const
    {
        Line l = Open("mobs", t);
        l.Guid("g", m.guid).U("entry", m.entry).Str("name", m.name).U("level", m.level)
            .U("rank", m.rank).B("boss", m.boss);
        return l.End();
    }

    std::string BuildFrame(uint32_t t, std::vector<BotSample> const& bots, std::vector<MobSample> const& mobs) const
    {
        std::string botsJson = "[";
        for (size_t i = 0; i < bots.size(); ++i)
        {
            if (i)
                botsJson += ',';
            botsJson += BotJson(bots[i]);
        }
        botsJson += ']';
        std::string mobsJson = "[";
        for (size_t i = 0; i < mobs.size(); ++i)
        {
            if (i)
                mobsJson += ',';
            mobsJson += MobJson(mobs[i]);
        }
        mobsJson += ']';
        Line l = Open("frame", t);
        l.Raw("bots", botsJson).Raw("mobs", mobsJson);
        return l.End();
    }

    std::string BuildEvent(uint32_t t, Event const& e) const
    {
        Line l = Open("event", t);
        l.Str("ev", e.ev);
        if (e.a)
            l.Guid("a", e.a);
        if (e.b)
            l.Guid("b", e.b);
        if (e.v)
            l.I("v", e.v);
        if (!e.s.empty())
            l.Str("s", e.s);
        if (!e.s2.empty())
            l.Str("s2", e.s2);
        return l.End();
    }

    std::string BuildAction(ActionRecord const& a) const
    {
        Line l = Open("action", a.t);
        l.Guid("g", a.guid).Str("name", a.name).Str("res", OutcomeName(a.outcome))
            .F("rel", a.relevance, 3);
        if (!a.source.empty())
            l.Str("src_trigger", a.source);
        if (a.n > 1)
            l.U("n", a.n).U("tl", a.tLast);
        return l.End();
    }

    std::string BuildEnd(uint32_t t, EndInfo const& e) const
    {
        Line l = Open("end", t);
        l.Str("reason", e.reason).Raw("counters", CountersJson(e.counters));
        return l.End();
    }

    static std::string CountersJson(Counters const& c)
    {
        Line l;
        l.U("frames", c.frames).U("events", c.events).U("actions", c.actions).U("party", c.party)
            .U("mobs", c.mobs).U("deaths", c.deaths).U("loops", c.loops).U("records", c.records)
            .U("bytes", c.bytes).U("dropped", c.dropped).B("truncated", c.truncated);
        return l.End();
    }

    // <run_id>.summary.json: one object, rewritten at start, at stop, and by
    // tools/trace_score.py (which fills "oracles" and "scored_unix"). `error`
    // (status "failed": the trace file could not be flushed or closed) is
    // written only when set. `extraMembers` are the run digest's fields
    // (runtime/RunTraceSummary.h: mode, build, driver, outcome, bosses, wipes,
    // deaths, ...), already rendered; they come after the counters and before
    // "oracles", never ahead of "status" (MarkSummaryIncomplete reads the first
    // "status" key).
    std::string BuildSummary(Header const& h, char const* status, std::string const& endReason,
        uint64_t endUnix, uint32_t durationMs, Counters const& c, std::string const& traceFile,
        std::string const& error = std::string(), std::string const& extraMembers = std::string()) const
    {
        Line l;
        l.U("v", kSchemaVersion).Str("schema", kSchemaName).Str("run", run_).Str("src", "server")
            .Str("status", status).Str("end_reason", endReason).Str("scope", h.scope)
            .Str("scope_name", h.scopeName).Str("requester", h.requester).Str("tb_version", h.tbVersion)
            .Str("trace_file", traceFile).U("start_unix", h.startUnix).U("end_unix", endUnix)
            .U("duration_ms", durationMs).U("interval_ms", h.intervalMs).U("map", h.mapId)
            .U("instance", h.instanceId).Raw("counters", CountersJson(c));
        if (!error.empty())
            l.Str("error", error);
        l.Members(extraMembers);
        l.Raw("oracles", "[]");
        return l.End();
    }

private:
    Line Open(char const* kind, uint32_t t) const
    {
        Line l;
        l.U("v", kSchemaVersion).Str("k", kind).Str("run", run_).Str("src", "server").U("t", t);
        return l;
    }

    static std::string BotJson(BotSample const& b)
    {
        Line l;
        l.Guid("g", b.guid).B("al", b.alive).B("cb", b.inCombat).Str("st", std::string(1, b.state))
            .U("hp", b.hp).U("mhp", b.maxHp).U("pw", b.power).U("mpw", b.maxPower).U("pt", b.powerType)
            .F("x", b.x, 1).F("y", b.y, 1).F("z", b.z, 1).B("mv", b.moving);
        if (b.target)
            l.Guid("tg", b.target);
        if (b.facing >= 0)
            l.U("fa", static_cast<uint64_t>(b.facing));
        if (b.casting)
            l.U("cs", b.casting);
        if (b.fire)
            l.U("fr", b.fire);
        if (!b.action.empty())
            l.Str("act", b.action);
        if (!b.strategy.empty())
            l.Str("strat", b.strategy);
        return l.End();
    }

    static std::string MobJson(MobSample const& m)
    {
        Line l;
        l.Guid("g", m.guid).U("hp", m.hpPct).F("x", m.x, 1).F("y", m.y, 1).F("z", m.z, 1).B("cb", m.inCombat);
        if (m.target)
            l.Guid("tg", m.target);
        if (m.casting)
            l.U("cs", m.casting);
        if (m.evading)
            l.B("ev", true);
        if (m.cc)
            l.B("cc", true);
        if (!m.threat.empty())
        {
            std::string arr = "[";
            for (size_t i = 0; i < m.threat.size(); ++i)
            {
                if (i)
                    arr += ',';
                arr += "[\"" + GuidHex(m.threat[i].guid) + "\"," + std::to_string(m.threat[i].pct) + "]";
            }
            arr += ']';
            l.Raw("thr", arr);
        }
        return l.End();
    }

    std::string run_;
};

// ---------------------------------------------------------------------------
// Who a run may record
//
// Permission is judged when a run starts and again at every frame, because the
// scope is re-read from the group each time: a player who joins the party (or a
// raid) later must not bring their bots into somebody else's trace. A bot is in
// scope while the requester is online and `controllable` for it: the glue sets
// that from "a GameMaster, or controls the bot" (CanControlBot) at every frame.
// At most kMaxScopeBots are kept, counted after the filter.
// ---------------------------------------------------------------------------

struct ScopeCandidate
{
    uint64_t guid = 0;
    bool controllable = false;  // the requester controls this bot right now (CanControlBot)
};

inline std::vector<size_t> SelectScope(bool requesterOnline, std::vector<ScopeCandidate> const& candidates,
    size_t cap = kMaxScopeBots)
{
    std::vector<size_t> allowed;
    if (!requesterOnline)
        return allowed;
    for (size_t i = 0; i < candidates.size() && allowed.size() < cap; ++i)
        if (candidates[i].controllable)
            allowed.push_back(i);
    return allowed;
}

// Where a frame's candidates come from. A `party` run reads the anchor's group
// and nothing else: when that group is gone the run has no bots (and ends with
// scope_lost after kEmptyScopeMs), it does not shrink to the anchor bot alone.
// A `bot` run reads the anchor bot itself.
enum class ScopeSource
{
    None,
    Group,
    Anchor
};

inline ScopeSource PickScopeSource(bool wholeParty, bool anchorHasGroup, bool anchorIsControllableBot)
{
    if (wholeParty)
        return anchorHasGroup ? ScopeSource::Group : ScopeSource::None;
    return anchorIsControllableBot ? ScopeSource::Anchor : ScopeSource::None;
}

// The bots the engine and spell hooks may record for. Each frame replaces the
// whole set, an empty frame included, so a bot that left the scope (or every
// bot, when the requester logged out) stops producing records at once instead
// of when the run ends.
class ScopeSet
{
public:
    void Replace(std::vector<uint64_t> const& guids)
    {
        guids_.clear();
        guids_.insert(guids.begin(), guids.end());
    }
    void Clear() { guids_.clear(); }
    bool Contains(uint64_t guid) const { return guids_.count(guid) != 0; }
    bool Empty() const { return guids_.empty(); }

private:
    std::unordered_set<uint64_t> guids_;
};

// Who may do what. The glue (commands/BotCommands.cpp, runtime/RunTrace.cpp)
// reads the facts from the core and asks these.
//
// A bot may be recorded while the requester is a GameMaster, controls it
// (owner, master or claim), or is the bound remote commander of the group the
// bot is in: the same rule `.bot trace start party` applies to a commanded
// group, so a commander's trace keeps its bots after the first frame.
inline bool MayRecordBot(bool requesterIsGm, bool controlsBot, bool commandsBotsGroup)
{
    return requesterIsGm || controlsBot || commandsBotsGroup;
}

// AiPlayerbot.RunTrace.GmOnly (default 1): only a GameMaster may start a run
// (traces take disk space); with 0 the owner of the bots may too.
inline bool MayStartTrace(bool gmOnly, bool requesterIsGm)
{
    return requesterIsGm || !gmOnly;
}

// `.bot trace status` names the run id and scope to a GameMaster and to the
// player who started the run; anyone else only learns that one is running.
inline bool MaySeeRunDetail(bool requesterIsGm, bool isStarter)
{
    return requesterIsGm || isStarter;
}

// `.bot trace stop [<run_id>]`: a named run id must be the running trace's
// own. Null when the stop may go on (no id given, or the running trace's),
// else the refusal code. The reply names the running run id (the requester
// here is a GameMaster or the starter, so nothing leaks).
inline char const* StopRunIdRefusal(std::string const& active, std::string const& asked)
{
    if (asked.empty() || asked == active)
        return nullptr;
    return "other-run";
}

// A run needs both the bot AI (AiPlayerbot.Enabled) and the trace switch
// (AiPlayerbot.RunTrace.Enabled): null when it may start or go on, else the
// message. A running trace whose switch goes off (a config reload) ends with
// end_reason "disabled"; a start is refused.
inline char const* TraceOffReason(bool aiEnabled, bool traceEnabled)
{
    if (!aiEnabled)
        return "The bot AI is off (AiPlayerbot.Enabled = 0).";
    if (!traceEnabled)
        return "The run trace is off (AiPlayerbot.RunTrace.Enabled = 0).";
    return nullptr;
}

// One run at a time. Ending a run only queues the end marker (the writer
// thread writes the end record and the summary and closes the file), so a run
// is `Finishing` until the world thread sees the writer done; a new start is
// refused until then.
enum class RunPhase
{
    Idle,
    Running,
    Finishing
};

class RunLifecycle
{
public:
    RunPhase Phase() const { return phase_; }
    // Null when a run may start, else the refusal code.
    char const* StartRefusal() const
    {
        switch (phase_)
        {
            case RunPhase::Running: return "running";
            case RunPhase::Finishing: return "finishing";
            default: return nullptr;
        }
    }
    bool Start()
    {
        if (phase_ != RunPhase::Idle)
            return false;
        phase_ = RunPhase::Running;
        return true;
    }
    // Stop, size cap, write failure, lost scope, disabled, shutdown.
    bool BeginFinish()
    {
        if (phase_ != RunPhase::Running)
            return false;
        phase_ = RunPhase::Finishing;
        return true;
    }
    // The writer has written the end record and the summary (or shutdown
    // stopped waiting for it).
    bool Finished()
    {
        if (phase_ != RunPhase::Finishing)
            return false;
        phase_ = RunPhase::Idle;
        return true;
    }

private:
    RunPhase phase_ = RunPhase::Idle;
};

// ---------------------------------------------------------------------------
// Merging repeated outcomes
//
// A bot that spends a second failing the same action makes the same record
// dozens of times. Records of one bot with the same action, trigger and
// outcome that fall within kMergeWindowMs of each other are one record with a
// count (n) and the time of the last (tl). The record is handed out when a
// different one follows, or when Flush() sees it has gone quiet.
// ---------------------------------------------------------------------------

constexpr uint32_t kMergeWindowMs = 1000;

class ActionCoalescer
{
public:
    // Takes one record. When it could not merge into the pending record of the
    // same bot, the pending one is moved to `out` (return true) and the new one
    // becomes pending.
    bool Add(ActionRecord const& rec, ActionRecord& out)
    {
        auto it = pending_.find(rec.guid);
        if (it == pending_.end())
        {
            pending_.emplace(rec.guid, rec);
            return false;
        }
        ActionRecord& cur = it->second;
        if (cur.name == rec.name && cur.source == rec.source && cur.outcome == rec.outcome &&
            rec.t >= cur.tLast && rec.t - cur.tLast <= kMergeWindowMs)
        {
            ++cur.n;
            cur.tLast = rec.t;
            cur.relevance = rec.relevance;
            return false;
        }
        out = std::move(cur);
        cur = rec;
        return true;
    }

    // Hands out every pending record that has been quiet for the merge window
    // (all of them when `all`).
    void Flush(uint32_t nowMs, bool all, std::vector<ActionRecord>& out)
    {
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            ActionRecord& cur = it->second;
            if (all || nowMs < cur.tLast || nowMs - cur.tLast > kMergeWindowMs)
            {
                out.push_back(std::move(cur));
                it = pending_.erase(it);
            }
            else
                ++it;
        }
    }

    size_t Pending() const { return pending_.size(); }

private:
    std::unordered_map<uint64_t, ActionRecord> pending_;
};

// ---------------------------------------------------------------------------
// Action failure loop
//
// "The same action failed more than kLoopFailures times within kLoopWindowMs":
// the shape of the spellstone, stay and melee loops. Note() is told about each
// failing outcome; it answers true once when a loop forms, and stays quiet for
// that bot and action until the window has passed again. Bounded to
// kLoopMaxKeys bot+action pairs.
// ---------------------------------------------------------------------------

constexpr uint32_t kLoopFailures = 8;
constexpr uint32_t kLoopWindowMs = 10000;
constexpr size_t kLoopMaxKeys = 256;

class LoopDetector
{
public:
    LoopDetector(uint32_t failures = kLoopFailures, uint32_t windowMs = kLoopWindowMs)
        : failures_(failures), windowMs_(windowMs) {}

    // `count` receives the failures inside the window when a loop is reported.
    bool Note(uint64_t guid, std::string const& action, uint32_t nowMs, uint32_t* count = nullptr)
    {
        std::string const key = std::to_string(guid) + "|" + action;
        // Make room before the new key goes in, so the entry used below is never the one evicted.
        if (tracks_.size() >= kLoopMaxKeys && !tracks_.count(key))
            Evict(nowMs);
        Track& tr = tracks_[key];
        tr.stamps.push_back(nowMs);
        while (!tr.stamps.empty() && nowMs - tr.stamps.front() > windowMs_)
            tr.stamps.pop_front();
        while (tr.stamps.size() > failures_ + 1)
            tr.stamps.pop_front();
        tr.lastSeen = nowMs;
        if (tr.stamps.size() > failures_ && (!tr.reported || nowMs - tr.lastReport > windowMs_))
        {
            tr.reported = true;
            tr.lastReport = nowMs;
            if (count)
                *count = static_cast<uint32_t>(tr.stamps.size());
            return true;
        }
        return false;
    }

    size_t Keys() const { return tracks_.size(); }

private:
    struct Track
    {
        std::deque<uint32_t> stamps;
        uint32_t lastSeen = 0;
        uint32_t lastReport = 0;
        bool reported = false;
    };

    // Drops the keys that have gone quiet, then the longest-quiet ones, until
    // there is room for one more.
    void Evict(uint32_t nowMs)
    {
        for (auto it = tracks_.begin(); it != tracks_.end();)
        {
            if (nowMs - it->second.lastSeen > windowMs_)
                it = tracks_.erase(it);
            else
                ++it;
        }
        while (tracks_.size() >= kLoopMaxKeys)
        {
            auto oldest = tracks_.begin();
            for (auto it = tracks_.begin(); it != tracks_.end(); ++it)
                if (it->second.lastSeen < oldest->second.lastSeen)
                    oldest = it;
            tracks_.erase(oldest);
        }
    }

    uint32_t failures_;
    uint32_t windowMs_;
    std::unordered_map<std::string, Track> tracks_;
};

} // namespace RunTrace
} // namespace TortoiseBots
