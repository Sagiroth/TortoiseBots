// Standalone check for runtime/DecisionCaptureRecorder.h (decision capture C1):
//   g++ -std=c++17 -Wall -Wextra tools/test_decision_capture_recorder.cpp -o /tmp/t && /tmp/t
//
// A memory sink records every line; the recorder's segment, keyframe, stub and
// gap rules are read back from the JSON.
#include "../runtime/DecisionCaptureRecorder.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;
using namespace TortoiseBots::DecisionCapture;
using TortoiseBots::DungeonRun::Facts;
using TortoiseBots::DungeonRun::MemberFact;
using TortoiseBots::DungeonRun::Run;
using TortoiseBots::DungeonRun::UnitFact;

struct MemSink : Sink
{
    std::vector<std::string> lines;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> segments;
    std::vector<int> refuseAt; // push numbers (from 1) that return false
    int pushes = 0;
    int sleepMs = 0; // stands in for a slow disk: not the encode's cost

    bool Push(std::string line) override
    {
        ++pushes;
        if (sleepMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        if (std::find(refuseAt.begin(), refuseAt.end(), pushes) != refuseAt.end())
            return false;
        lines.push_back(std::move(line));
        return true;
    }

    bool NewSegment(std::uint32_t seg, std::uint64_t firstSeq) override
    {
        segments.emplace_back(seg, firstSeq);
        return true;
    }
};

static MiniJson::Value ParseLine(const std::string& line)
{
    MiniJson::Value value;
    std::string error;
    if (!MiniJson::Parse(line, value, error))
    {
        std::cerr << "bad line: " << error << "\n" << line << "\n";
        std::exit(1);
    }
    return value;
}

static std::string Str(const MiniJson::Value& value, const char* key)
{
    const MiniJson::Value* found = value.Find(key);
    CHECK(found && found->kind == MiniJson::Value::String);
    return found->text;
}

static std::uint64_t Num(const MiniJson::Value& value, const char* key)
{
    const MiniJson::Value* found = value.Find(key);
    CHECK(found && found->kind == MiniJson::Value::Number);
    return std::strtoull(found->text.c_str(), nullptr, 10);
}

static bool Flag(const MiniJson::Value& value, const char* key)
{
    const MiniJson::Value* found = value.Find(key);
    CHECK(found && found->kind == MiniJson::Value::Bool);
    return found->b;
}

static HeaderInfo Header()
{
    HeaderInfo header;
    header.captureId = "20261102T161939Z-zul_gurub-Fionaa";
    header.commander = "Fionaa";
    header.module = "test";
    header.settingsJson = "{\"segmentMiB\":32,\"keyframeEvery\":60}";
    header.startedUnixMs = 1000;
    return header;
}

static void Tick(Recorder& recorder, Run& run, const Facts& facts, std::uint64_t now, const std::string& order = "")
{
    recorder.BeforeTick(run, facts, 1000, now);
    recorder.AfterTick(run, order);
}

static void TestHeaderThenKeyframe()
{
    MemSink sink;
    Limits limits;
    Recorder recorder(Header(), limits, sink);
    Run run;
    ai::DungeonPlan plan;
    plan.name = "zul_gurub";
    plan.map = 309;
    run.plan = &plan;
    Facts facts;
    Tick(recorder, run, facts, 2000);

    CHECK(sink.lines.size() == 2);
    MiniJson::Value const header = ParseLine(sink.lines[0]);
    CHECK(Str(header, "k") == "hdr");
    CHECK(Num(header, "v") == 1);
    CHECK(Num(header, "seg") == 1);
    CHECK(Str(header, "schema") == std::string(kDecisionSchemaHash));
    CHECK(Str(header, "capture") == "20261102T161939Z-zul_gurub-Fionaa");
    CHECK(Str(header.Find("plan") ? *header.Find("plan") : header, "name") == "zul_gurub");

    MiniJson::Value const tick = ParseLine(sink.lines[1]);
    CHECK(Str(tick, "k") == "tick");
    CHECK(Num(tick, "seq") == 1);
    CHECK(Flag(tick, "key"));
    CHECK(recorder.stats().records == 1);
    CHECK(recorder.stats().segments == 1);
}

static void TestKeyframeEveryThree()
{
    MemSink sink;
    Limits limits;
    limits.keyframeEvery = 3;
    Recorder recorder(Header(), limits, sink);
    Run run;
    Facts facts;
    for (int i = 0; i < 7; ++i)
    {
        facts.itemPlayer = std::to_string(i);
        Tick(recorder, run, facts, 2000 + static_cast<std::uint64_t>(i));
    }
    std::vector<std::uint64_t> keys;
    for (const auto& line : sink.lines)
    {
        MiniJson::Value const value = ParseLine(line);
        if (Str(value, "k") == "tick" && Flag(value, "key"))
            keys.push_back(Num(value, "seq"));
    }
    CHECK((keys == std::vector<std::uint64_t>{1, 4, 7}));
    CHECK(recorder.stats().records == 7);
}

static void TestSegmentRotation()
{
    MemSink sink;
    Limits limits;
    limits.segmentBytes = 100;
    Recorder recorder(Header(), limits, sink);
    Run run;
    ai::DungeonPlan plan;
    plan.name = "p";
    run.plan = &plan;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    Tick(recorder, run, facts, 2001);

    CHECK(sink.segments.size() == 1);
    CHECK(sink.segments[0].first == 2);
    CHECK(sink.segments[0].second == 2);
    CHECK(recorder.stats().segments == 2);

    // hdr, tick 1, hdr (seg 2), tick 2 (keyframe).
    CHECK(sink.lines.size() == 4);
    MiniJson::Value const second = ParseLine(sink.lines[2]);
    CHECK(Str(second, "k") == "hdr");
    CHECK(Num(second, "seg") == 2);
    MiniJson::Value const tick = ParseLine(sink.lines[3]);
    CHECK(Num(tick, "seq") == 2);
    CHECK(Flag(tick, "key"));
}

static void TestStubThenKeyframe()
{
    MemSink sink;
    Limits limits;
    // A delta record that does not fit becomes a stub; the next tick is a
    // keyframe again and fits once the run is smaller.
    limits.recordBytes = 15000;
    Recorder recorder(Header(), limits, sink);
    Run run;
    ai::DungeonPlan plan;
    plan.name = "p";
    run.plan = &plan;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    for (int i = 0; i < 30; ++i)
    {
        UnitFact unit;
        unit.guid = 100 + static_cast<std::uint64_t>(i);
        unit.entry = 4851;
        unit.distLead = 30.0f + static_cast<float>(i);
        facts.units.push_back(unit);
    }
    Tick(recorder, run, facts, 2001);
    facts.units.clear();
    Tick(recorder, run, facts, 2002);

    CHECK(sink.lines.size() == 4);
    MiniJson::Value const first = ParseLine(sink.lines[1]);
    CHECK(Str(first, "k") == "tick");
    CHECK(Num(first, "seq") == 1);
    MiniJson::Value const stub = ParseLine(sink.lines[2]);
    CHECK(Str(stub, "k") == "stub");
    CHECK(Num(stub, "seq") == 2);
    CHECK(Num(stub, "bytes") > 15000);
    MiniJson::Value const third = ParseLine(sink.lines[3]);
    CHECK(Str(third, "k") == "tick");
    CHECK(Num(third, "seq") == 3);
    CHECK(Flag(third, "key"));
    CHECK(recorder.stats().stubs == 1);
}

static void TestOversizedKeyframeIsWritten()
{
    MemSink sink;
    Limits limits;
    limits.recordBytes = 300; // every keyframe is over the cap
    limits.keyframeEvery = 3;
    Recorder recorder(Header(), limits, sink);
    Run run;
    ai::DungeonPlan plan;
    plan.name = "p";
    run.plan = &plan;
    Facts facts;
    for (int i = 0; i < 20; ++i)
    {
        MemberFact member;
        member.name = "Member" + std::to_string(i);
        member.yd = static_cast<float>(i);
        facts.members.push_back(member);
    }
    for (int i = 0; i < 61; ++i)
        Tick(recorder, run, facts, 2000 + static_cast<std::uint64_t>(i));

    // The first keyframe is written over the cap (not a stub), the ones before
    // the 60-record allowance are stubs, and seq 61's keyframe is written.
    MiniJson::Value const first = ParseLine(sink.lines[1]);
    CHECK(Str(first, "k") == "tick");
    CHECK(Flag(first, "key"));
    CHECK(Num(first, "seq") == 1);
    MiniJson::Value const fourth = ParseLine(sink.lines[4]);
    CHECK(Str(fourth, "k") == "stub");
    CHECK(Num(fourth, "seq") == 4);
    MiniJson::Value const last = ParseLine(sink.lines.back());
    CHECK(Str(last, "k") == "tick");
    CHECK(Flag(last, "key"));
    CHECK(Num(last, "seq") == 61);
    CHECK(recorder.stats().oversized == 2);
}

static void TestRefusedPushGap()
{
    MemSink sink;
    Limits limits;
    Recorder recorder(Header(), limits, sink);
    Run run;
    Facts facts;
    // 1: hdr, 2: tick 1, 3: tick 2 (refused), 4: gap, 5: tick 3.
    sink.refuseAt = {3};
    Tick(recorder, run, facts, 2000);
    facts.itemPlayer = "dropped";
    Tick(recorder, run, facts, 2001);
    Tick(recorder, run, facts, 2002);

    CHECK(sink.lines.size() == 4);
    MiniJson::Value const gap = ParseLine(sink.lines[2]);
    CHECK(Str(gap, "k") == "gap");
    CHECK(Num(gap, "dropped") == 1);
    MiniJson::Value const tick = ParseLine(sink.lines[3]);
    CHECK(Str(tick, "k") == "tick");
    CHECK(Num(tick, "seq") == 3);
    CHECK(Flag(tick, "key"));
    CHECK(recorder.stats().dropped == 1);
    CHECK(recorder.stats().gaps == 1);
    CHECK(recorder.stats().records == 2);
}

static void TestPlanChangeOpensSegment()
{
    MemSink sink;
    Limits limits;
    Recorder recorder(Header(), limits, sink);
    Run run;
    ai::DungeonPlan first;
    first.name = "first";
    first.map = 1;
    ai::DungeonPlan second;
    second.name = "second";
    second.map = 2;
    run.plan = &first;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    run.plan = &second;
    Tick(recorder, run, facts, 2001);

    CHECK(sink.segments.size() == 1);
    CHECK(sink.segments[0].first == 2);
    CHECK(sink.segments[0].second == 2);
    std::size_t headers = 0;
    std::string lastName;
    for (const auto& line : sink.lines)
    {
        MiniJson::Value const value = ParseLine(line);
        if (Str(value, "k") == "hdr")
        {
            ++headers;
            const MiniJson::Value* plan = value.Find("plan");
            CHECK(plan && plan->kind == MiniJson::Value::Object);
            lastName = Str(*plan, "name");
        }
    }
    CHECK(headers == 2);
    CHECK(lastName == "second");
}

static void TestGlueWriteIsRunDeltaOnly()
{
    MemSink sink;
    Limits limits;
    Recorder recorder(Header(), limits, sink);
    Run run;
    Facts facts;
    // Tick 1 changes the run itself (pre "before", post "after").
    run.note = "before";
    recorder.BeforeTick(run, facts, 1000, 2000);
    run.note = "after";
    recorder.AfterTick(run, "");
    // The glue's write between the ticks.
    run.leadWalkReset = true;
    Tick(recorder, run, facts, 2001);

    CHECK(sink.lines.size() == 3);
    MiniJson::Value const tick = ParseLine(sink.lines[2]);
    CHECK(Str(tick, "k") == "tick");
    CHECK(!Flag(tick, "key"));
    // The pre-tick delta is the glue's write alone, not tick 1's own change.
    const MiniJson::Value* delta = tick.Find("run");
    CHECK(delta && delta->kind == MiniJson::Value::Object);
    CHECK(delta->obj.size() == 1);
    const MiniJson::Value* written = delta->Find("leadWalkReset");
    CHECK(written && written->kind == MiniJson::Value::Bool && written->b);
    const MiniJson::Value* post = tick.Find("post");
    CHECK(post && post->kind == MiniJson::Value::Object);
    CHECK(post->Find("leadWalkReset") == nullptr);
}

static void TestEndLine()
{
    MemSink sink;
    Limits limits;
    Recorder recorder(Header(), limits, sink);
    Run run;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    Tick(recorder, run, facts, 2001);
    recorder.End(3000, "stopped");

    MiniJson::Value const end = ParseLine(sink.lines.back());
    CHECK(Str(end, "k") == "end");
    CHECK(Str(end, "reason") == "stopped");
    CHECK(Num(end, "records") == recorder.stats().records);
    CHECK(Num(end, "records") == 2);
    CHECK(Num(end, "dropped") == 0);
    CHECK(recorder.Stopped());
    std::size_t const lines = sink.lines.size();
    Tick(recorder, run, facts, 2002);
    CHECK(sink.lines.size() == lines);
}

static void TestRefusedFirstHeader()
{
    MemSink sink;
    sink.refuseAt = {1};
    Recorder recorder(Header(), Limits{}, sink);
    Run run;
    Facts facts;
    Tick(recorder, run, facts, 2000);

    // The header is retried before the record: hdr, the gap for the refused
    // header, then the keyframe. No record before an accepted header.
    CHECK(sink.lines.size() == 3);
    CHECK(Str(ParseLine(sink.lines[0]), "k") == "hdr");
    MiniJson::Value const gap = ParseLine(sink.lines[1]);
    CHECK(Str(gap, "k") == "gap");
    CHECK(Num(gap, "dropped") == 1);
    MiniJson::Value const tick = ParseLine(sink.lines[2]);
    CHECK(Str(tick, "k") == "tick");
    CHECK(Num(tick, "seq") == 1);
    CHECK(Flag(tick, "key"));
    CHECK(recorder.stats().dropped == 1);
}

static void TestRefusedRotatedHeader()
{
    MemSink sink;
    Limits limits;
    limits.segmentBytes = 100;
    // 1: hdr, 2: tick 1, 3: hdr of segment 2 refused, 4: its retry refused,
    // 5: the retry that lands, 6: gap, 7: tick 3.
    sink.refuseAt = {3, 4};
    Recorder recorder(Header(), limits, sink);
    Run run;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    Tick(recorder, run, facts, 2001); // tick 2's record is dropped: no header
    Tick(recorder, run, facts, 2002);

    CHECK(sink.segments.size() == 1);
    CHECK(sink.segments[0].first == 2);
    CHECK(Str(ParseLine(sink.lines[2]), "k") == "hdr");
    MiniJson::Value const secondHeader = ParseLine(sink.lines[2]);
    CHECK(Num(secondHeader, "seg") == 2);
    CHECK(Str(ParseLine(sink.lines[3]), "k") == "gap");
    MiniJson::Value const tick = ParseLine(sink.lines[4]);
    CHECK(Str(tick, "k") == "tick");
    CHECK(Num(tick, "seq") == 3);
    CHECK(Flag(tick, "key"));
    CHECK(recorder.stats().dropped == 3); // two headers, one dropped record
    // Every tick line in segment 2 follows its header.
    CHECK(sink.lines.size() == 5);
}

static void TestSameContentPlanDoesNotRotate()
{
    MemSink sink;
    Recorder recorder(Header(), Limits{}, sink);
    Run run;
    ai::DungeonPlan first;
    first.name = "p";
    first.map = 1;
    ai::DungeonPlan copy;
    copy.name = "p";
    copy.map = 1;
    run.plan = &first;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    run.plan = &copy;
    Tick(recorder, run, facts, 2001);

    // The change test is the cached hash, not the address: a copy of the same
    // plan is not a new plan.
    CHECK(sink.segments.empty());
    std::size_t headers = 0;
    for (const auto& line : sink.lines)
        if (Str(ParseLine(line), "k") == "hdr")
            ++headers;
    CHECK(headers == 1);
}

static void TestDoneTicksAndSegmentReset()
{
    MemSink sink;
    Limits limits;
    limits.keyframeEvery = 3;
    Recorder recorder(Header(), limits, sink);
    Run run;
    ai::DungeonPlan first;
    first.name = "first";
    ai::DungeonPlan second;
    second.name = "second";
    run.plan = &first;
    Facts facts;
    Tick(recorder, run, facts, 2000); // seq 1, running
    run.state = TortoiseBots::DungeonRun::State::Done;
    Tick(recorder, run, facts, 2001); // seq 2, the one Done record
    Tick(recorder, run, facts, 2002); // seq 3, skipped: both sides Done
    CHECK(recorder.stats().records == 2);

    // A new segment writes its own keyframe, even while the run is Done: the
    // done state belongs to the segment, not the capture.
    run.plan = &second;
    Tick(recorder, run, facts, 2003); // seq 4
    CHECK(sink.segments.size() == 1);
    CHECK(recorder.stats().records == 3);
    MiniJson::Value const last = ParseLine(sink.lines.back());
    CHECK(Str(last, "k") == "tick");
    CHECK(Num(last, "seq") == 4);
    CHECK(Flag(last, "key"));
    CHECK(Num(last, "t") == 2003);

    // ... and the next Done tick of that segment is skipped again.
    Tick(recorder, run, facts, 2004); // seq 5, skipped
    CHECK(recorder.stats().records == 3);
}

static void TestRefusedGapIsNotCounted()
{
    MemSink sink;
    // 1: hdr, 2: tick 1, 3: tick 2 refused, 4: its gap refused,
    // 5: the gap that lands, 6: tick 4.
    sink.refuseAt = {3, 4};
    Recorder recorder(Header(), Limits{}, sink);
    Run run;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    Tick(recorder, run, facts, 2001);
    Tick(recorder, run, facts, 2002);
    Tick(recorder, run, facts, 2003);

    CHECK(sink.lines.size() == 4);
    MiniJson::Value const gap = ParseLine(sink.lines[2]);
    CHECK(Str(gap, "k") == "gap");
    CHECK(Num(gap, "seq") == 4);
    // The refused gap itself is not a dropped record: tick 2 and tick 3 are.
    CHECK(Num(gap, "dropped") == 2);
    CHECK(Str(ParseLine(sink.lines[3]), "k") == "tick");
    CHECK(Num(ParseLine(sink.lines[3]), "seq") == 4);
}

static void TestEndGapSeq()
{
    MemSink sink;
    sink.refuseAt = {3}; // tick 2
    Recorder recorder(Header(), Limits{}, sink);
    Run run;
    Facts facts;
    Tick(recorder, run, facts, 2000);
    Tick(recorder, run, facts, 2001);
    recorder.End(3000, "stopped");

    CHECK(Str(ParseLine(sink.lines.back()), "k") == "end");
    MiniJson::Value const gap = ParseLine(sink.lines[sink.lines.size() - 2]);
    CHECK(Str(gap, "k") == "gap");
    CHECK(Num(gap, "seq") == 3); // the gap after the last tick is its own seq
    CHECK(Num(gap, "dropped") == 1);
    CHECK(recorder.stats().dropped == 1);
}

static void TestEndBeforeAnyTick()
{
    MemSink sink;
    Recorder recorder(Header(), Limits{}, sink);
    recorder.End(3000, "stopped");
    CHECK(sink.lines.empty());
    CHECK(recorder.Stopped());
}

static void TestSettingsMustBeAnObject()
{
    char const* bad[] = {"[1,2]", "\"text\"", "not json", "{} trailing"};
    for (char const* text : bad)
    {
        MemSink sink;
        HeaderInfo header = Header();
        header.settingsJson = text;
        Recorder recorder(header, Limits{}, sink);
        Run run;
        Facts facts;
        Tick(recorder, run, facts, 2000);
        MiniJson::Value const hdr = ParseLine(sink.lines[0]);
        const MiniJson::Value* settings = hdr.Find("settings");
        CHECK(settings && settings->kind == MiniJson::Value::Object);
        CHECK(settings->obj.empty());
    }
    {
        MemSink sink;
        HeaderInfo header = Header();
        header.settingsJson = "{\"segmentMiB\":32}";
        Recorder recorder(header, Limits{}, sink);
        Run run;
        Facts facts;
        Tick(recorder, run, facts, 2000);
        MiniJson::Value const hdr = ParseLine(sink.lines[0]);
        const MiniJson::Value* settings = hdr.Find("settings");
        CHECK(settings && settings->kind == MiniJson::Value::Object);
        CHECK(settings->Find("segmentMiB") != nullptr);
    }
}

static void TestCapUsExcludesTheSink()
{
    MemSink sink;
    sink.sleepMs = 12; // a slow disk must not count against the CPU budget
    Recorder recorder(Header(), Limits{}, sink);
    Run run;
    Facts facts;
    for (int i = 0; i < 20; ++i)
    {
        MemberFact member;
        member.name = "Member" + std::to_string(i);
        member.yd = static_cast<float>(i);
        facts.members.push_back(member);
    }
    Tick(recorder, run, facts, 2000);

    MiniJson::Value const tick = ParseLine(sink.lines[1]);
    CHECK(Num(tick, "capUs") > 0);
    CHECK(Num(tick, "capUs") < 8000);
    CHECK(recorder.stats().lastCapUs < 8000);
}

int main()
{
    TestHeaderThenKeyframe();
    TestKeyframeEveryThree();
    TestSegmentRotation();
    TestStubThenKeyframe();
    TestOversizedKeyframeIsWritten();
    TestRefusedPushGap();
    TestRefusedFirstHeader();
    TestRefusedRotatedHeader();
    TestPlanChangeOpensSegment();
    TestSameContentPlanDoesNotRotate();
    TestGlueWriteIsRunDeltaOnly();
    TestDoneTicksAndSegmentReset();
    TestEndLine();
    TestRefusedGapIsNotCounted();
    TestEndGapSeq();
    TestEndBeforeAnyTick();
    TestSettingsMustBeAnObject();
    TestCapUsExcludesTheSink();
    std::cout << "test_decision_capture_recorder: ok\n";
    return 0;
}
