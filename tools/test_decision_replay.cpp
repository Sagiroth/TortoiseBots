// Standalone check for tools/DecisionReplay.h and the committed fixtures:
//   g++ -std=c++17 -O2 -Wall -Wextra tools/test_decision_replay.cpp -o /tmp/t && /tmp/t
//
// The synthetic scenario is recorded with the real recorder, cut into a
// fixture with the real extractor, and replayed with the real policy.

#include "DecisionReplay.h"
#include "DecisionReplayScenario.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;
using namespace TortoiseBots::DecisionReplay;

namespace
{

struct MemSink : DecisionCapture::Sink
{
    std::vector<std::pair<std::string, std::string>> lines;

    bool Push(std::string line) override
    {
        lines.emplace_back(std::move(line), "scenario");
        return true;
    }

    bool NewSegment(std::uint32_t, std::uint64_t) override
    {
        return true;
    }
};

std::filesystem::path gRoot;
std::filesystem::path gBaseFixture;
std::string gPlanPath;

bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << text;
    return true;
}

bool LoadJson(const std::filesystem::path& path, MiniJson::Value& value)
{
    std::string text, err;
    if (!ReadWholeFile(path.string(), text, err) || !MiniJson::Parse(text, value, err))
        return false;
    return true;
}

std::filesystem::path Variant(const std::string& name)
{
    return gRoot / "base" / (name + ".json");
}

bool WriteVariant(const std::string& name, MiniJson::Value root)
{
    return WriteText(Variant(name), MiniJson::Raw(root));
}

std::string ReplaceOnce(std::string text, const std::string& from, const std::string& to)
{
    std::size_t const at = text.find(from);
    if (at == std::string::npos)
        return text;
    text.replace(at, from.size(), to);
    return text;
}

std::string RemoveLine(std::string text, const std::string& line)
{
    std::size_t const at = text.find(line);
    if (at == std::string::npos)
        return text;
    std::size_t end = text.find('\n', at);
    if (end == std::string::npos)
        end = text.size();
    else
        ++end;
    text.erase(at, end - at);
    return text;
}

const TickRec& TickAt(const Capture& capture, std::uint64_t seq)
{
    for (const TickRec& tick : capture.ticks)
        if (tick.seq == seq)
            return tick;
    CHECK(false && "tick not found");
    return capture.ticks.front();
}

bool FindResult(const Report& report, std::uint64_t seq, TickResult& out)
{
    for (const TickResult& tick : report.ticks)
        if (tick.seq == seq)
        {
            out = tick;
            return true;
        }
    return false;
}

void TestRecordAndExtract()
{
    MemSink sink;
    RecordScenario(sink);
    Capture capture;
    std::string err;
    CHECK(LoadLines(sink.lines, capture, err));
    CHECK(capture.ticks.size() == kScenarioTicks);
    CHECK(capture.captureId == "20261102T161939Z-synthetic-Fionaa");
    CHECK(capture.plan.name == "synthetic");
    CHECK(capture.tacticsSet);

    ExtractOptions options;
    options.fromSeq = 1;
    options.toSeq = kScenarioTicks;
    options.name = "synthetic-pull";
    options.outDir = (gRoot / "base").string();
    options.segment = "scenario";
    options.status = "fixed";
    std::string path;
    CHECK(Extract(capture, options, path, err));
    CHECK(path == gBaseFixture.string());

    std::string text;
    CHECK(ReadWholeFile(path, text, err));
    CHECK(text.size() <= 1024u * 1024u);
    MiniJson::Value root;
    CHECK(MiniJson::Parse(text, root, err));
    const MiniJson::Value* ticks = root.Find("ticks");
    CHECK(ticks && ticks->kind == MiniJson::Value::Array && ticks->arr.size() == kScenarioTicks);
    CHECK(ticks->arr.front().Find("key") && ticks->arr.front().Find("key")->b);
    CHECK(ticks->arr[1].Find("key") && !ticks->arr[1].Find("key")->b);
}

void TestSegmentFile()
{
    MemSink sink;
    RecordScenario(sink);
    std::string text;
    for (const auto& line : sink.lines)
        text += line.first + "\n";
    std::filesystem::path const segment = gRoot / "base" / "seg-0001.jsonl";
    CHECK(WriteText(segment, text));

    Capture capture;
    std::string err;
    CHECK(LoadSegments({segment.string()}, capture, err));
    CHECK(capture.ticks.size() == kScenarioTicks);
    CHECK(capture.captureId == "20261102T161939Z-synthetic-Fionaa");
    CHECK(capture.ticks.front().key);
    CHECK(!capture.ticks.back().key);
}

std::string ScenarioText()
{
    MemSink sink;
    RecordScenario(sink);
    std::string text;
    for (const auto& line : sink.lines)
        text += line.first + "\n";
    return text;
}

std::string FirstLine(const std::string& text)
{
    return text.substr(0, text.find('\n'));
}

// Replaces the line holding `marker` with `replacement` (dropped when empty).
std::string ReplaceLine(std::string text, const std::string& marker, const std::string& replacement)
{
    std::size_t const at = text.find(marker);
    if (at == std::string::npos)
        return text;
    std::size_t const begin = text.rfind('\n', at);
    std::size_t const start = begin == std::string::npos ? 0 : begin + 1;
    std::size_t end = text.find('\n', at);
    end = end == std::string::npos ? text.size() : end + 1;
    text.replace(start, end - start, replacement.empty() ? std::string() : replacement + "\n");
    return text;
}

const char* const kScenarioCapture = "\"capture\":\"20261102T161939Z-synthetic-Fionaa\"";

void TestSegmentsLoadInHeaderOrder()
{
    std::string const text = ScenarioText();
    std::string const header = FirstLine(text);
    CHECK(header.find("\"k\":\"hdr\"") != std::string::npos);
    std::string const second = ReplaceOnce(header, "\"seg\":1,", "\"seg\":2,");
    CHECK(second != header);

    // The path order is the reverse of the segment order.
    std::filesystem::path const dir = gRoot / "order";
    CHECK(WriteText(dir / "a-later.jsonl", second + "\n"));
    CHECK(WriteText(dir / "z-first.jsonl", text));
    Capture capture;
    std::string err;
    CHECK(LoadSegments({(dir / "a-later.jsonl").string(), (dir / "z-first.jsonl").string()}, capture, err));
    CHECK(capture.notes.front().compare(0, 10, "hdr seg 1 ") == 0);
    CHECK(capture.ticks.size() == kScenarioTicks);
}

void TestSegmentsOfTwoCapturesRefused()
{
    std::string const text = ScenarioText();
    std::string const other = ReplaceOnce(text, kScenarioCapture, "\"capture\":\"20261102T170000Z-other-Fionaa\"");
    CHECK(other != text);
    std::filesystem::path const dir = gRoot / "two-captures";
    CHECK(WriteText(dir / "seg-0001.jsonl", text));
    CHECK(WriteText(dir / "seg-0002.jsonl", ReplaceOnce(other, "\"seg\":1,", "\"seg\":2,")));
    Capture capture;
    std::string err;
    CHECK(!LoadSegments({(dir / "seg-0001.jsonl").string(), (dir / "seg-0002.jsonl").string()}, capture, err));
    CHECK(err.find("another capture") != std::string::npos);
    CHECK(err.find("seg-0002.jsonl") != std::string::npos);
}

void TestMissingSegmentNamesIt()
{
    std::string const text = ScenarioText();
    std::filesystem::path const dir = gRoot / "missing-segment";
    CHECK(WriteText(dir / "seg-0001.jsonl", text));
    CHECK(WriteText(dir / "seg-0003.jsonl", ReplaceOnce(FirstLine(text), "\"seg\":1,", "\"seg\":3,") + "\n"));
    Capture capture;
    std::string err;
    CHECK(!LoadSegments({(dir / "seg-0001.jsonl").string(), (dir / "seg-0003.jsonl").string()}, capture, err));
    CHECK(err.find("segment 2 is missing") != std::string::npos);
}

void TestMissingTickNamesItsSeq()
{
    std::string const text = ScenarioText();
    std::filesystem::path const dir = gRoot / "hole";
    CHECK(WriteText(dir / "dropped.jsonl", ReplaceLine(text, "\"k\":\"tick\",\"seq\":5,", "")));
    Capture capture;
    std::string err;
    CHECK(!LoadSegments({(dir / "dropped.jsonl").string()}, capture, err));
    CHECK(err.find("tick seq 5 is missing") != std::string::npos);

    // The same hole, announced by a gap record for the seq after it, loads.
    CHECK(WriteText(dir / "declared.jsonl",
                    ReplaceLine(text, "\"k\":\"tick\",\"seq\":5,", "{\"k\":\"gap\",\"seq\":6,\"dropped\":1}")));
    Capture declared;
    CHECK(LoadSegments({(dir / "declared.jsonl").string()}, declared, err));
    CHECK(declared.ticks.size() == kScenarioTicks - 1);
    bool sawGap = false;
    for (const std::string& note : declared.notes)
        sawGap = sawGap || note == "gap seq 6 dropped 1";
    CHECK(sawGap);
}

void TestExtractNameAndOverwrite()
{
    MemSink sink;
    RecordScenario(sink);
    Capture capture;
    std::string err;
    CHECK(LoadLines(sink.lines, capture, err));

    std::filesystem::path const guard = gRoot / "guard";
    ExtractOptions options;
    options.fromSeq = 1;
    options.toSeq = kScenarioTicks;
    options.outDir = guard.string();
    options.segment = "scenario";
    std::string path;
    for (const char* name : {"../escape", ".hidden", "a/b", "sp ace", ""})
    {
        options.name = name;
        CHECK(!Extract(capture, options, path, err));
        CHECK(err.find("name") != std::string::npos);
    }
    CHECK(!std::filesystem::exists(gRoot / "escape.json"));

    options.name = "guard-pull";
    CHECK(Extract(capture, options, path, err));
    CHECK(path == (guard / "guard-pull.json").string());
    std::string before, after;
    CHECK(ReadWholeFile(path, before, err));

    options.status = "fixed";
    CHECK(!Extract(capture, options, path, err));
    CHECK(err.find("already exists") != std::string::npos);
    CHECK(ReadWholeFile(path, after, err));
    CHECK(after == before);

    options.overwrite = true;
    CHECK(Extract(capture, options, path, err));
    CHECK(ReadWholeFile(path, after, err));
    CHECK(after != before);
}

void TestIndependentReplay()
{
    Fixture fixture;
    std::string err;
    CHECK(LoadFixture(gBaseFixture.string(), fixture, err));
    fixture.status = "fixed";
    fixture.reproduce = "all";
    Report const report = Replay(fixture);
    CHECK(report.pass);
    CHECK(report.failures.empty());
    CHECK(report.ticks.size() == kScenarioTicks);
    for (const TickResult& tick : report.ticks)
        CHECK(tick.reproduced);
}

void TestChainReplay()
{
    Fixture fixture;
    std::string err;
    CHECK(LoadFixture(gBaseFixture.string(), fixture, err));
    fixture.status = "fixed";
    fixture.reproduce = "all";
    fixture.mode = "chain";
    Report const report = Replay(fixture);
    CHECK(report.pass);
    for (const TickResult& tick : report.ticks)
        CHECK(tick.reproduced);
}

void TestChainNeedsTheGlueDelta()
{
    Fixture fixture;
    std::string err;
    CHECK(LoadFixture(gBaseFixture.string(), fixture, err));
    fixture.status = "fixed";
    fixture.reproduce = "all";
    fixture.mode = "chain";
    Options options;
    options.chainApplyGlue = false;
    Report const report = Replay(fixture, options);
    for (const TickResult& tick : report.ticks)
        if (tick.seq < kScenarioGlueDeltaSeq)
            CHECK(tick.reproduced);
    TickResult glue;
    CHECK(FindResult(report, kScenarioGlueDeltaSeq, glue));
    CHECK(!glue.reproduced);
    CHECK(glue.diverged);
    bool sawLeadWalkReset = false;
    for (const std::string& diff : glue.diffs)
        if (diff.find("leadWalkReset") != std::string::npos)
            sawLeadWalkReset = true;
    CHECK(sawLeadWalkReset);
}

void TestTamperedOrderNamesItsTick()
{
    MiniJson::Value root;
    CHECK(LoadJson(gBaseFixture, root));
    MiniJson::Value* ticks = nullptr;
    for (auto& entry : root.obj)
        if (entry.first == "ticks")
            ticks = &entry.second;
    CHECK(ticks && ticks->kind == MiniJson::Value::Array);
    std::uint64_t const tampered = 23;
    for (MiniJson::Value& tick : ticks->arr)
    {
        if (NumOr(tick, "seq", 0) != tampered)
            continue;
        for (auto& entry : tick.obj)
            if (entry.first == "out")
                for (auto& field : entry.second.obj)
                    if (field.first == "order")
                        field.second = VString("tampered order");
    }
    CHECK(WriteVariant("tampered", root));

    Fixture fixture;
    std::string err;
    CHECK(LoadFixture(Variant("tampered").string(), fixture, err));
    fixture.status = "fixed";
    // No expectation: before-first-mark must still mean every tick.
    Report const report = Replay(fixture);
    CHECK(!report.pass);
    bool named = false;
    for (const std::string& failure : report.failures)
        if (failure.find("seq " + std::to_string(tampered)) != std::string::npos)
            named = true;
    CHECK(named);
    for (const TickResult& tick : report.ticks)
        if (tick.seq != tampered)
            CHECK(tick.reproduced);
}

void TestFixtureStatus()
{
    Fixture fixture;
    std::string err;
    CHECK(LoadFixture(gBaseFixture.string(), fixture, err));
    std::uint64_t const seq = 20;
    std::string const order = TickAt(fixture.cap, seq).out.order;
    CHECK(order.empty());

    Expect failing;
    failing.seq = seq;
    failing.orderNot = order;
    failing.why = "the recorded order is the bug";

    fixture.expect.clear();
    fixture.expect.push_back(failing);
    fixture.status = "open";
    Report open = Replay(fixture);
    CHECK(open.xfail);
    CHECK(!open.xpass);
    CHECK(open.pass);
    CHECK(!open.failures.empty());

    fixture.status = "fixed";
    Report fixed = Replay(fixture);
    CHECK(!fixed.pass);
    CHECK(!fixed.xfail);

    fixture.expect.clear();
    Expect runField;
    runField.seq = kScenarioPullSeq;
    runField.run = {{"step", "2"}};
    fixture.expect.push_back(runField);
    fixture.status = "fixed";
    Report exact = Replay(fixture);
    CHECK(exact.pass);
    fixture.expect.back().run = {{"step", "999"}};
    Report wrong = Replay(fixture);
    CHECK(!wrong.pass);

    fixture.expect.clear();
    Expect satisfied = failing;
    satisfied.orderNot = "an order that never happens";
    fixture.expect.push_back(satisfied);
    fixture.status = "open";
    Report xpass = Replay(fixture);
    CHECK(xpass.xpass);
    CHECK(!xpass.pass);
    bool flip = false;
    for (const std::string& failure : xpass.failures)
        if (failure.find("flip status to fixed") != std::string::npos)
            flip = true;
    CHECK(flip);
}

void TestSchemaMismatchNamesTheDiff()
{
    std::string err;
    std::string schemaText = ReplaceOnce(kDecisionSchemaText, "  total: std::uint32_t", "  total: std::string");
    CHECK(schemaText.find("total: std::string") != std::string::npos);
    CHECK(WriteText(gRoot / "base" / "schemas" / "aaaaaaaaaaaaaaaa.txt", schemaText));

    MiniJson::Value root;
    CHECK(LoadJson(gBaseFixture, root));
    for (auto& entry : root.obj)
        if (entry.first == "schema")
            entry.second = VString("aaaaaaaaaaaaaaaa");
    CHECK(WriteVariant("schema-bad", root));

    Fixture fixture;
    CHECK(LoadFixture(Variant("schema-bad").string(), fixture, err));
    Report const report = Replay(fixture);
    CHECK(!report.pass);
    CHECK(!report.failures.empty());
    bool diff = false, upgrade = false;
    for (const std::string& failure : report.failures)
    {
        if (failure.find("~Facts.total: std::string -> std::uint32_t") != std::string::npos)
            diff = true;
        if (failure.find("upgrade") != std::string::npos)
            upgrade = true;
    }
    CHECK(diff);
    CHECK(upgrade);

    MiniJson::Value missing;
    CHECK(LoadJson(gBaseFixture, missing));
    for (auto& entry : missing.obj)
        if (entry.first == "schema")
            entry.second = VString("bbbbbbbbbbbbbbbb");
    CHECK(WriteVariant("schema-missing", missing));
    Fixture missingFixture;
    CHECK(LoadFixture(Variant("schema-missing").string(), missingFixture, err));
    Report const missingReport = Replay(missingFixture);
    CHECK(!missingReport.pass);
    CHECK(!missingReport.failures.empty());
    CHECK(missingReport.failures.front().find("missing") != std::string::npos);
}

void TestPlanHashMismatch()
{
    MiniJson::Value plan;
    CHECK(LoadJson(gPlanPath, plan));
    MiniJson::Value* planValue = nullptr;
    for (auto& entry : plan.obj)
        if (entry.first == "plan")
            planValue = &entry.second;
    CHECK(planValue);
    ai::DungeonPlan decoded;
    std::string err;
    CHECK(DecisionCapture::DecodePlan(*planValue, decoded, err));
    ai::DungeonPlan changed = decoded;
    changed.name = "edited";
    // A plan that decodes and hashes cleanly: only the fixture's planHash can catch it.
    planValue->obj.clear();
    *planValue = ParseText(DecisionCapture::EncodePlan(changed));
    std::filesystem::path const edited = gRoot / "base" / "edited-plan.json";
    CHECK(WriteText(edited, MiniJson::Raw(plan)));

    MiniJson::Value root;
    CHECK(LoadJson(gBaseFixture, root));
    for (auto& entry : root.obj)
        if (entry.first == "plan")
            entry.second = VString(edited.string());
    CHECK(WriteVariant("plan-bad", root));

    Fixture fixture;
    CHECK(!LoadFixture(Variant("plan-bad").string(), fixture, err));
    CHECK(err.find("planHash") != std::string::npos);
}

void TestUpgrade()
{
    std::string oldSchema = RemoveLine(kDecisionSchemaText, "  total: std::uint32_t");
    CHECK(oldSchema != kDecisionSchemaText);
    CHECK(WriteText(gRoot / "base" / "schemas" / "cccccccccccccccc.txt", oldSchema));

    MiniJson::Value root;
    CHECK(LoadJson(gBaseFixture, root));
    for (auto& entry : root.obj)
        if (entry.first == "schema")
            entry.second = VString("cccccccccccccccc");
    CHECK(WriteVariant("upgrade-good", root));

    std::string err, summary;
    CHECK(UpgradeFixture(Variant("upgrade-good").string(), err, summary));
    CHECK(summary.find("schema cccccccccccccccc") != std::string::npos);

    Fixture upgraded;
    CHECK(LoadFixture(Variant("upgrade-good").string(), upgraded, err));
    CHECK(upgraded.schema == std::string(kDecisionSchemaHash));
    const MiniJson::Value* history = upgraded.root.Find("history");
    CHECK(history && history->kind == MiniJson::Value::Array && !history->arr.empty());
    upgraded.status = "fixed";
    Report const report = Replay(upgraded);
    CHECK(report.pass);

    std::string retyped = ReplaceOnce(kDecisionSchemaText, "  total: std::uint32_t", "  total: std::string");
    CHECK(retyped != kDecisionSchemaText);
    CHECK(WriteText(gRoot / "base" / "schemas" / "dddddddddddddddd.txt", retyped));
    MiniJson::Value bad;
    CHECK(LoadJson(gBaseFixture, bad));
    for (auto& entry : bad.obj)
        if (entry.first == "schema")
            entry.second = VString("dddddddddddddddd");
    CHECK(WriteVariant("upgrade-bad", bad));
    std::string badErr, badSummary;
    CHECK(!UpgradeFixture(Variant("upgrade-bad").string(), badErr, badSummary));
    CHECK(badErr.find("retyped") != std::string::npos);

    MiniJson::Value unchanged;
    CHECK(LoadJson(Variant("upgrade-bad"), unchanged));
    const MiniJson::Value* schema = unchanged.Find("schema");
    CHECK(schema && schema->text == "dddddddddddddddd");
}

void TestWhy()
{
    Fixture fixture;
    std::string err;
    CHECK(LoadFixture(gBaseFixture.string(), fixture, err));
    std::vector<std::string> fields;
    CHECK(Why(fixture, kScenarioPullSeq, fields, err));
    bool units = false, doors = false;
    for (const std::string& field : fields)
    {
        if (field == "units")
            units = true;
        if (field == "doors")
            doors = true;
    }
    CHECK(units);
    CHECK(!doors);
}

void ScanPrivacy(const MiniJson::Value& value, const std::string& where)
{
    if (value.kind == MiniJson::Value::String)
    {
        std::string lower = value.text;
        for (char& c : lower)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        bool bad = lower.find("password") != std::string::npos || lower.find("passwd") != std::string::npos ||
                   lower.find("secret") != std::string::npos || lower.find("token") != std::string::npos;
        if (!bad)
        {
            std::size_t const begin = lower.find("begin ");
            if (begin != std::string::npos && lower.find(" key", begin + 6) != std::string::npos)
                bad = true;
        }
        if (bad)
        {
            std::cerr << "privacy: " << where << ": " << value.text << "\n";
            CHECK(false);
        }
    }
    for (const MiniJson::Value& element : value.arr)
        ScanPrivacy(element, where);
    for (const auto& entry : value.obj)
        ScanPrivacy(entry.second, where);
}

void TestCommittedFixtures()
{
    std::filesystem::path const dir("tools/fixtures/decisions");
    CHECK(std::filesystem::is_directory(dir));
    std::size_t fixtures = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        ++fixtures;
        CHECK(std::filesystem::file_size(entry.path()) <= 1024u * 1024u);
        Fixture fixture;
        std::string err;
        CHECK(LoadFixture(entry.path().string(), fixture, err));
        CHECK(fixture.cap.ticks.size() <= 300);
        Report const report = Replay(fixture);
        for (const std::string& failure : report.failures)
            std::cerr << entry.path().filename().string() << ": " << failure << "\n";
        CHECK(report.pass);

        MiniJson::Value root;
        CHECK(LoadJson(entry.path(), root));
        ScanPrivacy(root, entry.path().filename().string());
    }
    CHECK(fixtures >= 1);

    std::filesystem::path const plans = dir / "plans";
    CHECK(std::filesystem::is_directory(plans));
    for (const auto& entry : std::filesystem::directory_iterator(plans))
    {
        if (!entry.is_regular_file())
            continue;
        CHECK(std::filesystem::file_size(entry.path()) <= 4u * 1024u * 1024u);
        MiniJson::Value root;
        CHECK(LoadJson(entry.path(), root));
        ScanPrivacy(root, entry.path().filename().string());
    }
}

} // namespace

int main()
{
    gRoot = std::filesystem::temp_directory_path() / ("tb-decision-replay-" + std::to_string(getpid()));
    std::filesystem::remove_all(gRoot);
    std::filesystem::create_directories(gRoot / "base");
    gBaseFixture = gRoot / "base" / "synthetic-pull.json";

    TestRecordAndExtract();
    TestSegmentFile();
    TestSegmentsLoadInHeaderOrder();
    TestSegmentsOfTwoCapturesRefused();
    TestMissingSegmentNamesIt();
    TestMissingTickNamesItsSeq();
    TestExtractNameAndOverwrite();
    {
        Fixture fixture;
        std::string err;
        CHECK(LoadFixture(gBaseFixture.string(), fixture, err));
        gPlanPath = fixture.planPath;
    }

    TestIndependentReplay();
    TestChainReplay();
    TestChainNeedsTheGlueDelta();
    TestTamperedOrderNamesItsTick();
    TestFixtureStatus();
    TestSchemaMismatchNamesTheDiff();
    TestPlanHashMismatch();
    TestUpgrade();
    TestWhy();
    TestCommittedFixtures();

    std::filesystem::remove_all(gRoot);
    std::cout << "test_decision_replay: ok\n";
    return 0;
}
