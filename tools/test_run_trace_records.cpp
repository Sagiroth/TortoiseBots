// Standalone tests for the run trace's record layer (runtime/RunTraceRecords.h):
// the bt-trace v1 JSONL builders, the run id and number helpers, the merging of
// repeated action outcomes and the action failure loop detector.
//
// The header includes the standard library only, and this file compiles it
// without a single game header, so the builders can only format what they are
// handed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_run_trace_records.cpp -o /tmp/test_run_trace_records
//   /tmp/test_run_trace_records

#include "../runtime/RunTraceRecords.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace TortoiseBots::RunTrace;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)
#define CHECK_EQ(a, b) do { \
    ++checks; \
    std::string const va = (a); \
    std::string const vb = (b); \
    if (va != vb) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
        std::exit(1); \
    } \
} while (0)

static bool Has(std::string const& s, char const* part)
{
    return s.find(part) != std::string::npos;
}

static void TestHelpers()
{
    CHECK(ValidRunId("run-2026.10.14_a"));
    CHECK(ValidRunId("a"));
    CHECK(!ValidRunId(""));
    CHECK(!ValidRunId(".hidden"));
    CHECK(!ValidRunId("../escape"));
    CHECK(!ValidRunId("a b"));
    CHECK(!ValidRunId("a/b"));
    CHECK(!ValidRunId(std::string(65, 'x')));
    CHECK(ValidRunId(std::string(64, 'x')));

    CHECK(ClampIntervalMs(10) == kMinIntervalMs);
    CHECK(ClampIntervalMs(500) == 500);
    CHECK(ClampIntervalMs(999999) == kMaxIntervalMs);
    CHECK(ClampMaxFileMb(0) == kMinMaxFileMb);
    CHECK(ClampMaxFileMb(64) == 64);
    CHECK(ClampMaxFileMb(99999) == kMaxMaxFileMb);

    CHECK_EQ(FixedStr(12.5, 1), "12.5");
    CHECK_EQ(FixedStr(-3.04, 1), "-3.0");
    CHECK_EQ(FixedStr(-0.01, 1), "0.0");
    CHECK_EQ(FixedStr(0.0, 1), "0.0");
    CHECK_EQ(FixedStr(1234.56, 1), "1234.6");
    CHECK_EQ(FixedStr(0.05, 3), "0.050");
    CHECK_EQ(FixedStr(0.0 / 0.0 * 0.0, 1), "0.0"); // NaN reads as 0
    CHECK_EQ(FixedStr(7.0, 0), "7");

    CHECK_EQ(GuidHex(0), "0x0");
    CHECK_EQ(GuidHex(0xF13000123400ABCDull), "0xf13000123400abcd");

    CHECK_EQ(JsonEscape("a\"b\\c"), "a\\\"b\\\\c");
    CHECK_EQ(JsonEscape("l1\nl2\t"), "l1\\nl2\\t");
    CHECK_EQ(JsonEscape(std::string("x\x01y", 3)), "x\\u0001y");
    CHECK_EQ(JsonEscape("caf\xC3\xA9"), "caf\xC3\xA9"); // UTF-8 passes through
}

static void TestBuilderStamps()
{
    Builder b("run-1");
    Header h;
    h.runId = "run-1";
    h.scope = "party";
    h.scopeName = "Aldo";
    h.requester = "Aldo";
    h.tbVersion = "2026-10-14-v1";
    h.startUnix = 1700000000;
    h.intervalMs = 500;
    h.maxBytes = 67108864;
    h.mapId = 70;
    h.instanceId = 12;

    PartyMember pm;
    pm.guid = 0x10;
    pm.name = "Tank\"y";
    pm.cls = "warrior";
    pm.role = "tank";
    pm.level = 45;
    Mob mob;
    mob.guid = 0xF130;
    mob.entry = 2748;
    mob.name = "Archaedas";
    mob.level = 47;
    mob.rank = 3;
    mob.boss = true;
    Event ev;
    ev.ev = "death";
    ev.a = 0x10;
    ev.b = 0xF130;
    ActionRecord ar;
    ar.guid = 0x10;
    ar.name = "melee";
    ar.outcome = ActionOutcome::Failed;
    ar.relevance = 1.5f;
    ar.t = 1200;
    ar.tLast = 1200;
    EndInfo end;
    end.reason = "stop";

    std::vector<std::string> lines = {
        b.BuildHeader(h), b.BuildParty(0, pm), b.BuildMobs(300, mob), b.BuildFrame(500, {}, {}),
        b.BuildEvent(900, ev), b.BuildAction(ar), b.BuildEnd(5000, end)};
    char const* kinds[] = {"header", "party", "mobs", "frame", "event", "action", "end"};
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string const& l = lines[i];
        CHECK(l.front() == '{' && l.back() == '}');
        CHECK(l.find('\n') == std::string::npos);
        // Every record: schema version, kind, run id, source, time.
        std::string const prefix = std::string("{\"v\":1,\"k\":\"") + kinds[i] + "\",\"run\":\"run-1\",\"src\":\"server\",\"t\":";
        CHECK(l.compare(0, prefix.size(), prefix) == 0);
    }
    CHECK(Has(lines[0], "\"schema\":\"bt-trace\""));
    CHECK(Has(lines[0], "\"scope\":\"party\""));
    CHECK(Has(lines[0], "\"interval_ms\":500"));
    CHECK(Has(lines[0], "\"max_bytes\":67108864"));
    CHECK(Has(lines[0], "\"map\":70"));
    CHECK(Has(lines[0], "\"caps\":[\"facing\",\"moving\",\"interrupts\",\"in_fire\"]"));
    CHECK(Has(lines[1], "\"name\":\"Tank\\\"y\""));
    CHECK(Has(lines[1], "\"role\":\"tank\""));
    CHECK(Has(lines[2], "\"boss\":true"));
    CHECK(Has(lines[2], "\"entry\":2748"));
    CHECK_EQ(lines[3], "{\"v\":1,\"k\":\"frame\",\"run\":\"run-1\",\"src\":\"server\",\"t\":500,\"bots\":[],\"mobs\":[]}");
    CHECK(Has(lines[4], "\"ev\":\"death\""));
    CHECK(Has(lines[4], "\"a\":\"0x10\""));
    CHECK(Has(lines[4], "\"b\":\"0xf130\""));
    CHECK(!Has(lines[4], "\"v\":0")); // zero value omitted
    CHECK_EQ(lines[5], "{\"v\":1,\"k\":\"action\",\"run\":\"run-1\",\"src\":\"server\",\"t\":1200,\"g\":\"0x10\",\"name\":\"melee\",\"res\":\"FAILED\",\"rel\":1.500}");
    CHECK(Has(lines[6], "\"reason\":\"stop\""));
    CHECK(Has(lines[6], "\"counters\":{\"frames\":0"));
}

static void TestFrame()
{
    Builder b("r");
    BotSample bot;
    bot.guid = 0x10;
    bot.alive = true;
    bot.inCombat = true;
    bot.state = 'c';
    bot.hp = 900;
    bot.maxHp = 1000;
    bot.power = 40;
    bot.maxPower = 100;
    bot.powerType = 1;
    bot.x = 10.04f;
    bot.y = -20.06f;
    bot.z = 5.0f;
    bot.target = 0xF1;
    bot.casting = 133;
    bot.moving = true;
    bot.facing = 135;
    bot.fire = 5740;
    bot.action = "melee";
    bot.strategy = "dps,threat";
    BotSample idle;
    idle.guid = 0x11;
    idle.state = 'n';

    MobSample mob;
    mob.guid = 0xF1;
    mob.hpPct = 80;
    mob.x = 1;
    mob.y = 2;
    mob.z = 3;
    mob.target = 0x10;
    mob.inCombat = true;
    mob.evading = true;
    mob.threat = {{0x10, 100}, {0x11, 62}};

    std::string const frame = b.BuildFrame(1500, {bot, idle}, {mob});
    CHECK(Has(frame, "\"t\":1500"));
    CHECK(Has(frame, "{\"g\":\"0x10\",\"al\":true,\"cb\":true,\"st\":\"c\",\"hp\":900,\"mhp\":1000,\"pw\":40,\"mpw\":100,\"pt\":1,\"x\":10.0,\"y\":-20.1,\"z\":5.0,\"mv\":true,\"tg\":\"0xf1\",\"fa\":135,\"cs\":133,\"fr\":5740,\"act\":\"melee\",\"strat\":\"dps,threat\"}"));
    // An idle bot omits target, cast, action and an unchanged strategy.
    CHECK(Has(frame, "{\"g\":\"0x11\",\"al\":true,\"cb\":false,\"st\":\"n\",\"hp\":0,\"mhp\":0,\"pw\":0,\"mpw\":0,\"pt\":0,\"x\":0.0,\"y\":0.0,\"z\":0.0,\"mv\":false}"));
    CHECK(Has(frame, "\"mobs\":[{\"g\":\"0xf1\",\"hp\":80,\"x\":1.0,\"y\":2.0,\"z\":3.0,\"cb\":true,\"tg\":\"0x10\",\"ev\":true,\"thr\":[[\"0x10\",100],[\"0x11\",62]]}]"));
}

static void TestSummary()
{
    Builder b("run-9");
    Header h;
    h.runId = "run-9";
    h.scope = "bot";
    h.scopeName = "Mage";
    h.startUnix = 100;
    h.intervalMs = 500;
    Counters c;
    c.frames = 10;
    c.actions = 40;
    c.dropped = 2;
    c.truncated = true;
    std::string const s = b.BuildSummary(h, "complete", "size_cap", 160, 60000, c, "run-9.jsonl");
    CHECK(s.compare(0, 8, "{\"v\":1,\"") == 0);
    CHECK(Has(s, "\"schema\":\"bt-trace\""));
    CHECK(Has(s, "\"run\":\"run-9\""));
    CHECK(Has(s, "\"src\":\"server\""));
    CHECK(Has(s, "\"status\":\"complete\""));
    CHECK(Has(s, "\"end_reason\":\"size_cap\""));
    CHECK(Has(s, "\"trace_file\":\"run-9.jsonl\""));
    CHECK(Has(s, "\"duration_ms\":60000"));
    CHECK(Has(s, "\"start_unix\":100"));
    CHECK(Has(s, "\"end_unix\":160"));
    CHECK(Has(s, "\"counters\":{\"frames\":10,\"events\":0,\"actions\":40"));
    CHECK(Has(s, "\"dropped\":2,\"truncated\":true}"));
    CHECK(Has(s, "\"oracles\":[]}"));
}

static void TestOutcomes()
{
    CHECK_EQ(OutcomeName(ActionOutcome::Ok), "OK");
    CHECK_EQ(OutcomeName(ActionOutcome::Failed), "FAILED");
    CHECK_EQ(OutcomeName(ActionOutcome::Impossible), "IMPOSSIBLE");
    CHECK_EQ(OutcomeName(ActionOutcome::Useless), "USELESS");
    CHECK_EQ(OutcomeName(ActionOutcome::Prereq), "PREREQ");
    CHECK_EQ(OutcomeName(ActionOutcome::Vetoed), "VETOED");
    CHECK_EQ(OutcomeName(ActionOutcome::Backoff), "BACKOFF");
    CHECK(OutcomeIsFailure(ActionOutcome::Failed));
    CHECK(OutcomeIsFailure(ActionOutcome::Impossible));
    CHECK(!OutcomeIsFailure(ActionOutcome::Ok));
    CHECK(!OutcomeIsFailure(ActionOutcome::Useless));
    CHECK(!OutcomeIsFailure(ActionOutcome::Vetoed));

    // A merged action record carries its count and the time of the last.
    Builder b("r");
    ActionRecord a;
    a.guid = 1;
    a.name = "x";
    a.t = 100;
    a.tLast = 900;
    a.n = 7;
    a.source = "low health";
    std::string const line = b.BuildAction(a);
    CHECK(Has(line, "\"src_trigger\":\"low health\""));
    CHECK(Has(line, "\"n\":7,\"tl\":900"));
}

static ActionRecord Rec(uint64_t guid, char const* name, ActionOutcome o, uint32_t t, char const* src = "")
{
    ActionRecord r;
    r.guid = guid;
    r.name = name;
    r.outcome = o;
    r.t = r.tLast = t;
    r.source = src;
    return r;
}

static void TestCoalescer()
{
    ActionCoalescer c;
    ActionRecord out;
    CHECK(!c.Add(Rec(1, "melee", ActionOutcome::Ok, 0), out));
    CHECK(!c.Add(Rec(1, "melee", ActionOutcome::Ok, 400), out));
    CHECK(!c.Add(Rec(1, "melee", ActionOutcome::Ok, 1300), out)); // 900 after the last: still merges
    CHECK(c.Pending() == 1);

    // A different outcome ends the run of identical ones and hands it out.
    CHECK(c.Add(Rec(1, "melee", ActionOutcome::Failed, 1400), out));
    CHECK(out.name == "melee" && out.outcome == ActionOutcome::Ok);
    CHECK(out.n == 3 && out.t == 0 && out.tLast == 1300);

    // A different action ends it too, a different trigger as well.
    CHECK(c.Add(Rec(1, "shoot", ActionOutcome::Failed, 1500), out));
    CHECK(out.name == "melee" && out.outcome == ActionOutcome::Failed && out.n == 1);
    CHECK(c.Add(Rec(1, "shoot", ActionOutcome::Failed, 1600, "ranged"), out));
    CHECK(out.name == "shoot" && out.source.empty());

    // Bots do not merge into each other.
    CHECK(!c.Add(Rec(2, "shoot", ActionOutcome::Failed, 1600, "ranged"), out));
    CHECK(c.Pending() == 2);

    // A gap longer than the window starts a new record.
    ActionCoalescer gap;
    CHECK(!gap.Add(Rec(1, "a", ActionOutcome::Ok, 0), out));
    CHECK(gap.Add(Rec(1, "a", ActionOutcome::Ok, 2500), out));
    CHECK(out.n == 1 && out.tLast == 0);

    // Flush hands out what has gone quiet, everything when asked.
    std::vector<ActionRecord> done;
    ActionCoalescer f;
    f.Add(Rec(1, "a", ActionOutcome::Ok, 0), out);
    f.Add(Rec(2, "b", ActionOutcome::Ok, 1800), out);
    f.Flush(2000, false, done);
    CHECK(done.size() == 1 && done[0].name == "a");
    CHECK(f.Pending() == 1);
    done.clear();
    f.Flush(2100, true, done);
    CHECK(done.size() == 1 && done[0].name == "b");
    CHECK(f.Pending() == 0);
}

static void TestLoopDetector()
{
    // Default: more than 8 failures inside 10 s.
    {
        LoopDetector d;
        uint32_t count = 0;
        for (uint32_t i = 0; i < 8; ++i)
            CHECK(!d.Note(1, "melee", i * 500, &count));
        CHECK(d.Note(1, "melee", 4000, &count)); // the 9th
        CHECK(count == 9);
        // Quiet for the rest of the window, even while it goes on failing.
        for (uint32_t t = 4500; t < 14000; t += 500)
            CHECK(!d.Note(1, "melee", t));
        // The window passed and it is still failing: reported again.
        bool again = false;
        for (uint32_t t = 14000; t < 16000; t += 500)
            again = again || d.Note(1, "melee", t);
        CHECK(again);
    }
    // Slow failures never add up: one every 2 s keeps 6 in a 10 s window.
    {
        LoopDetector d;
        for (uint32_t i = 0; i < 60; ++i)
            CHECK(!d.Note(1, "melee", i * 2000));
    }
    // A loop is per bot and per action.
    {
        LoopDetector d;
        for (uint32_t i = 0; i < 8; ++i)
        {
            CHECK(!d.Note(1, "melee", i * 100));
            CHECK(!d.Note(2, "melee", i * 100));
            CHECK(!d.Note(1, "shoot", i * 100));
        }
        CHECK(d.Note(2, "melee", 900));
        CHECK(d.Note(1, "melee", 900)); // bot 1's melee is the 9th too
    }
    // Custom limits.
    {
        LoopDetector d(2, 1000);
        CHECK(!d.Note(1, "a", 0));
        CHECK(!d.Note(1, "a", 100));
        CHECK(d.Note(1, "a", 200));
    }
    // The table stays bounded.
    {
        LoopDetector d;
        for (uint32_t i = 0; i < 2000; ++i)
            d.Note(i, "spam", i);
        CHECK(d.Keys() <= kLoopMaxKeys);
    }
}

static void TestScopePolicy()
{
    std::vector<ScopeCandidate> group = {{1, true}, {2, false}, {3, true}, {4, false}};

    // The owner keeps only the bots they control, in order.
    auto own = SelectScope(true, group);
    CHECK(own.size() == 2 && own[0] == 0 && own[1] == 2);

    // A stranger's bots that joined the group later are not recorded.
    std::vector<ScopeCandidate> strangers = {{1, false}, {2, false}};
    CHECK(SelectScope(true, strangers).empty());

    // A GameMaster controls every bot (the glue marks them all controllable).
    std::vector<ScopeCandidate> gm = {{1, true}, {2, true}, {3, true}};
    CHECK(SelectScope(true, gm).size() == 3);

    // An offline requester records nothing.
    CHECK(SelectScope(false, group).empty());
    CHECK(SelectScope(false, gm).empty());

    // The cap applies after the filter: uncontrolled bots do not use up places.
    std::vector<ScopeCandidate> big;
    for (uint64_t i = 0; i < 10; ++i)
        big.push_back({i, i % 2 == 1});
    auto capped = SelectScope(true, big, 3);
    CHECK(capped.size() == 3 && capped[0] == 1 && capped[1] == 3 && capped[2] == 5);

    CHECK(SelectScope(true, {}).empty());
}

static void TestScopeSource()
{
    // A party run reads the group; once the group is gone it has no bots, so
    // the empty-scope timer runs and the run ends with scope_lost. It does not
    // shrink to the anchor (the commanded lead bot) on its own.
    CHECK(PickScopeSource(true, true, true) == ScopeSource::Group);
    CHECK(PickScopeSource(true, true, false) == ScopeSource::Group);
    CHECK(PickScopeSource(true, false, true) == ScopeSource::None);
    CHECK(PickScopeSource(true, false, false) == ScopeSource::None);

    // A bot run follows that one bot, grouped or not.
    CHECK(PickScopeSource(false, true, true) == ScopeSource::Anchor);
    CHECK(PickScopeSource(false, false, true) == ScopeSource::Anchor);
    CHECK(PickScopeSource(false, true, false) == ScopeSource::None);
    CHECK(PickScopeSource(false, false, false) == ScopeSource::None);
}

static void TestPermissions()
{
    // Who may record a bot at a frame.
    CHECK(MayRecordBot(true, false, false));    // GameMaster
    CHECK(MayRecordBot(false, true, false));    // controls it
    CHECK(MayRecordBot(false, false, true));    // bound commander of its group
    CHECK(!MayRecordBot(false, false, false));  // a stranger's bot

    // A commanded party trace records the bots: the commander controls none of
    // them (hired or GM-bound), but commands their group, so every frame keeps
    // them instead of dropping to an empty scope after the first one.
    std::vector<ScopeCandidate> commanded;
    for (uint64_t guid : {21u, 22u, 23u})
        commanded.push_back({guid, MayRecordBot(false, false, true)});
    CHECK(SelectScope(true, commanded).size() == 3);
    std::vector<ScopeCandidate> notCommanded;
    for (uint64_t guid : {21u, 22u, 23u})
        notCommanded.push_back({guid, MayRecordBot(false, false, false)});
    CHECK(SelectScope(true, notCommanded).empty());

    // `.bot trace start party` checks each bot of the party with the same
    // rule: a GameMaster's party holding a stranger's bots may be traced, a
    // player's may not.
    struct PartyBot { bool controlled; };
    std::vector<PartyBot> party = {{true}, {false}, {false}};
    auto partyStartAllowed = [&party](bool gm, bool commanded)
    {
        for (PartyBot const& bot : party)
            if (!MayRecordBot(gm, bot.controlled, commanded))
                return false;
        return true;
    };
    CHECK(partyStartAllowed(true, false));    // GameMaster
    CHECK(partyStartAllowed(false, true));    // bound commander
    CHECK(!partyStartAllowed(false, false));  // a player with a stranger's bots

    // AiPlayerbot.RunTrace.GmOnly.
    CHECK(MayStartTrace(true, true));
    CHECK(!MayStartTrace(true, false));
    CHECK(MayStartTrace(false, false));
    CHECK(MayStartTrace(false, true));

    // `.bot trace status` detail.
    CHECK(MaySeeRunDetail(true, false));
    CHECK(MaySeeRunDetail(false, true));
    CHECK(!MaySeeRunDetail(false, false));
}

static void TestStopRunIdRefusal()
{
    // `.bot trace stop [<run_id>]`: no id, or the running trace's own id,
    // stops it; another run's id is refused and nothing is stopped.
    CHECK(StopRunIdRefusal("r1", "") == nullptr);
    CHECK(StopRunIdRefusal("r1", "r1") == nullptr);
    CHECK_EQ(StopRunIdRefusal("r1", "r2"), "other-run");
}

static void TestLifecycle()
{
    RunLifecycle run;
    CHECK(run.Phase() == RunPhase::Idle);
    CHECK(run.StartRefusal() == nullptr);
    CHECK(!run.BeginFinish());   // stop without a run
    CHECK(!run.Finished());

    CHECK(run.Start());
    CHECK(run.Phase() == RunPhase::Running);
    CHECK_EQ(run.StartRefusal(), "running");
    CHECK(!run.Start());

    // Stop returns at once: the run is finishing until the writer is done, and
    // a new start is refused meanwhile.
    CHECK(run.BeginFinish());
    CHECK(run.Phase() == RunPhase::Finishing);
    CHECK_EQ(run.StartRefusal(), "finishing");
    CHECK(!run.Start());
    CHECK(!run.BeginFinish());   // a second stop does not end it twice

    CHECK(run.Finished());
    CHECK(run.Phase() == RunPhase::Idle);
    CHECK(run.StartRefusal() == nullptr);
    CHECK(run.Start());
}

static void TestTraceNeedsTheAi()
{
    CHECK(TraceOffReason(true, true) == nullptr);
    CHECK_EQ(TraceOffReason(false, true), "The bot AI is off (AiPlayerbot.Enabled = 0).");
    CHECK_EQ(TraceOffReason(false, false), "The bot AI is off (AiPlayerbot.Enabled = 0).");
    CHECK_EQ(TraceOffReason(true, false), "The run trace is off (AiPlayerbot.RunTrace.Enabled = 0).");

    // A reload switches the AI off during a run: the next tick ends it (the
    // glue calls BeginFinish with end_reason "disabled"), and a start is
    // refused while the AI stays off.
    RunLifecycle run;
    CHECK(run.Start());
    bool aiEnabled = false;
    if (TraceOffReason(aiEnabled, true))
        CHECK(run.BeginFinish());
    CHECK(run.Phase() == RunPhase::Finishing);
    CHECK(run.Finished());
    CHECK(TraceOffReason(aiEnabled, true) != nullptr);  // start refused
}

static void TestScopeSetEmptiesOnLogout()
{
    // Two bots in scope, then the requester logs out: the next frame selects
    // no bot, and the set the action hook reads must be empty from that frame
    // on (no records until the scope_lost timeout).
    std::vector<ScopeCandidate> group = {{11, true}, {12, true}};
    ScopeSet scope;
    std::vector<uint64_t> guids;
    for (size_t i : SelectScope(true, group))
        guids.push_back(group[i].guid);
    scope.Replace(guids);
    CHECK(scope.Contains(11) && scope.Contains(12));

    guids.clear();
    for (size_t i : SelectScope(false, group))
        guids.push_back(group[i].guid);
    CHECK(guids.empty());
    scope.Replace(guids);
    CHECK(scope.Empty());
    CHECK(!scope.Contains(11) && !scope.Contains(12));

    // A frame that drops one bot drops only that one.
    scope.Replace({11, 12});
    scope.Replace({12});
    CHECK(!scope.Contains(11) && scope.Contains(12));
    scope.Clear();
    CHECK(scope.Empty());
}

int main()
{
    TestHelpers();
    TestBuilderStamps();
    TestFrame();
    TestSummary();
    TestOutcomes();
    TestCoalescer();
    TestLoopDetector();
    TestScopePolicy();
    TestScopeSource();
    TestScopeSetEmptiesOnLogout();
    TestPermissions();
    TestStopRunIdRefusal();
    TestLifecycle();
    TestTraceNeedsTheAi();
    std::printf("test_run_trace_records: %d checks passed\n", checks);
    return 0;
}
