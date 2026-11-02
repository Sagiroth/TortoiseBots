// Standalone check for runtime/DecisionCaptureRecorder.h (decision capture C1):
//   g++ -std=c++17 -Wall -Wextra tools/test_decision_capture_recorder.cpp -o /tmp/t && /tmp/t
//
// A memory sink records every line; the recorder's segment, keyframe, stub and
// gap rules are read back from the JSON.
#include "../runtime/DecisionCaptureRecorder.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
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

struct MemSink : Sink
{
    std::vector<std::string> lines;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> segments;
    int refuseAt = -1;
    int pushes = 0;

    bool Push(std::string line) override
    {
        ++pushes;
        if (pushes == refuseAt)
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
    // A keyframe that does not fit becomes a stub; the next tick is a
    // keyframe again and fits once the run is smaller.
    limits.recordBytes = 20000;
    Recorder recorder(Header(), limits, sink);
    Run run;
    ai::DungeonPlan plan;
    plan.name = "p";
    run.plan = &plan;
    Facts facts;
    for (int i = 0; i < 40; ++i)
    {
        MemberFact member;
        member.name = "Member" + std::to_string(i);
        member.yd = static_cast<float>(i);
        member.x = static_cast<float>(i);
        facts.members.push_back(member);
    }
    Tick(recorder, run, facts, 2000);
    facts.members.clear();
    Tick(recorder, run, facts, 2001);
    Tick(recorder, run, facts, 2002);

    CHECK(sink.lines.size() == 4);
    MiniJson::Value const stub = ParseLine(sink.lines[1]);
    CHECK(Str(stub, "k") == "stub");
    CHECK(Num(stub, "seq") == 1);
    CHECK(Num(stub, "bytes") > 20000);
    CHECK(recorder.stats().stubs == 1);
    MiniJson::Value const second = ParseLine(sink.lines[2]);
    CHECK(Str(second, "k") == "tick");
    CHECK(Num(second, "seq") == 2);
    CHECK(Flag(second, "key"));
    MiniJson::Value const third = ParseLine(sink.lines[3]);
    CHECK(Str(third, "k") == "tick");
    CHECK(!Flag(third, "key"));
}

static void TestRefusedPushGap()
{
    MemSink sink;
    Limits limits;
    Recorder recorder(Header(), limits, sink);
    Run run;
    Facts facts;
    // 1: hdr, 2: tick 1, 3: tick 2 (refused), 4: gap, 5: tick 3.
    sink.refuseAt = 3;
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

int main()
{
    TestHeaderThenKeyframe();
    TestKeyframeEveryThree();
    TestSegmentRotation();
    TestStubThenKeyframe();
    TestRefusedPushGap();
    TestPlanChangeOpensSegment();
    TestGlueWriteIsRunDeltaOnly();
    TestEndLine();
    std::cout << "test_decision_capture_recorder: ok\n";
    return 0;
}
