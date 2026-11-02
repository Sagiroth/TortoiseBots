// Standalone check for runtime/DecisionCaptureFiles.{h,cpp} (decision capture
// C3): the capture folder, segment rotation, retention and index.json, driven
// through the real recorder, plus the service's off path.
//   g++ -std=c++17 -O2 -Wall -Wextra tools/test_decision_capture_files.cpp
//       runtime/RunTraceWriter.cpp runtime/DecisionCaptureFiles.cpp -pthread -o /tmp/t
//   /tmp/t [tmpdir]
#include "../runtime/DecisionCapture.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
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
using TortoiseBots::DungeonRun::Facts;
using TortoiseBots::DungeonRun::Run;

namespace
{

constexpr std::uint64_t kStart = 1762000000000ull; // 2025-11-01T12:26:40Z
constexpr std::uint64_t kTickMs = 1000;

std::filesystem::path Root()
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / "tb-dcap-files-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

std::string ReadFile(std::string const& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> ReadLines(std::string const& path)
{
    std::vector<std::string> lines;
    std::ifstream in(path, std::ios::in | std::ios::binary);
    for (std::string line; std::getline(in, line);)
        lines.push_back(line);
    return lines;
}

// Atomic writes land a moment later on the writer thread.
bool ReadJson(std::string const& path, MiniJson::Value& out)
{
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        if (std::filesystem::exists(path))
        {
            std::string err;
            if (MiniJson::Parse(ReadFile(path), out, err) && out.kind == MiniJson::Value::Object)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void WaitWriters(FileSink& sink)
{
    for (int attempt = 0; attempt < 400 && sink.FinishingWriters() > 0; ++attempt)
    {
        sink.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(sink.FinishingWriters() == 0);
}

// The final index is queued before the writer is let go; a detached writer
// finishes it a moment later.
MiniJson::Value WaitIndex(std::string const& dir, std::string const& id)
{
    std::string const path = (std::filesystem::path(dir) / id / "index.json").string();
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        MiniJson::Value value;
        std::string err;
        if (std::filesystem::exists(path) && MiniJson::Parse(ReadFile(path), value, err) &&
            value.kind == MiniJson::Value::Object)
        {
            MiniJson::Value const* ended = value.Find("ended");
            if (ended && ended->kind == MiniJson::Value::String)
                return value;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cerr << "index never ended: " << path << "\n";
    std::exit(1);
}

std::unique_ptr<MiniJson::Value> IndexObj(std::string const& dir, std::string const& id)
{
    auto value = std::make_unique<MiniJson::Value>();
    bool const ok = ReadJson((std::filesystem::path(dir) / id / "index.json").string(), *value);
    CHECK(ok);
    return value;
}

std::uint64_t Number(MiniJson::Value const& object, std::string const& key)
{
    MiniJson::Value const* value = object.Find(key);
    CHECK(value && value->kind == MiniJson::Value::Number);
    return std::strtoull(value->text.c_str(), nullptr, 10);
}

struct Harness
{
    std::unique_ptr<FileSink> sink;
    std::unique_ptr<DecisionCapture::Recorder> recorder;
    Run run;
    Facts facts;
    std::string dir;
    std::string id;
    std::uint64_t now = kStart;
    std::uint64_t lastSeq = 0;

    void Tick(std::string const& order = "walk")
    {
        ++lastSeq;
        ++run.step;
        now += kTickMs;
        recorder->BeforeTick(run, facts, 1000, now);
        recorder->AfterTick(run, order);
    }

    void Finish(std::string const& reason = "stopped")
    {
        sink->Finish(*recorder, now + kTickMs, reason);
        WaitWriters(*sink);
    }
};

Harness MakeCapture(std::string const& dir, std::string const& id, std::uint64_t startedUnixMs,
    std::uint64_t segmentBytes, std::uint64_t captureBytes = 512ull * 1024ull * 1024ull)
{
    Harness h;
    h.dir = dir;
    h.id = id;
    FileLimits fileLimits;
    fileLimits.segmentBytes = 1ull * 1024ull * 1024ull;
    fileLimits.captureBytes = captureBytes;
    fileLimits.totalBytes = 4096ull * 1024ull * 1024ull;
    DecisionCapture::Limits recorderLimits;
    recorderLimits.segmentBytes = segmentBytes;
    recorderLimits.recordBytes = 512u * 1024u;
    recorderLimits.keyframeEvery = 60;

    IndexInfo info;
    info.captureId = id;
    info.commander = "Fionaa";
    info.plan = "zul_gurub";
    info.startedUnixMs = startedUnixMs;
    std::string err;
    h.sink = FileSink::Open(dir, id, info, fileLimits, err);
    if (!h.sink)
    {
        std::cerr << "open failed: " << err << "\n";
        std::exit(1);
    }
    DecisionCapture::HeaderInfo header;
    header.captureId = id;
    header.commander = info.commander;
    header.module = "test";
    header.settingsJson = "{}";
    header.startedUnixMs = startedUnixMs;
    h.recorder = std::make_unique<DecisionCapture::Recorder>(header, recorderLimits, *h.sink);
    return h;
}

void TestIds()
{
    std::string const dir = (Root() / "ids").string();
    std::filesystem::create_directories(dir);
    std::string const id = FileSink::MakeCaptureId(dir, kStart, "zul_gurub", "Fionaa");
    CHECK(id == "20251101T122640Z-zul_gurub-Fionaa");

    std::string const odd = FileSink::MakeCaptureId(dir, kStart, "zul'gurub (r2)", "F\u00edonaa");
    CHECK(odd == "20251101T122640Z-zul_gurub__r2_-F__onaa");

    std::regex const pattern(
        "^[0-9]{8}T[0-9]{6}Z-[A-Za-z0-9_]{1,48}-[A-Za-z0-9_]{1,24}(-[0-9]{1,3})?$");
    CHECK(std::regex_match(id, pattern));
    CHECK(std::regex_match(odd, pattern));

    std::string const longPlan(60, 'p');
    std::string const longName(30, 'n');
    std::string const cut = FileSink::MakeCaptureId(dir, kStart, longPlan, longName);
    CHECK(cut == "20251101T122640Z-" + std::string(48, 'p') + "-" + std::string(24, 'n'));
    CHECK(std::regex_match(cut, pattern));

    std::filesystem::create_directories(std::filesystem::path(dir) / id);
    std::string const clash = FileSink::MakeCaptureId(dir, kStart, "zul_gurub", "Fionaa");
    CHECK(clash == id + "-2");
    CHECK(std::regex_match(clash, pattern));
    std::filesystem::create_directories(std::filesystem::path(dir) / clash);
    CHECK(FileSink::MakeCaptureId(dir, kStart, "zul_gurub", "Fionaa") == id + "-3");
}

void TestSegmentsAndIndex()
{
    std::string const dir = (Root() / "segments").string();
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1);

    // The writer thread wrote the open index by now.
    auto opened = IndexObj(dir, h.id);
    CHECK(opened->Find("format") && opened->Find("format")->text == "tb-decision-capture");
    CHECK(Number(*opened, "v") == 1);
    CHECK(opened->Find("id")->text == h.id);
    CHECK(opened->Find("commander")->text == "Fionaa");
    CHECK(opened->Find("plan")->text == "zul_gurub");
    CHECK(opened->Find("started")->text == "2025-11-01T12:26:40Z");
    CHECK(Number(*opened, "startedUnixMs") == kStart);
    CHECK(opened->Find("ended") && opened->Find("ended")->kind == MiniJson::Value::Null);
    CHECK(opened->Find("endReason")->text.empty());
    CHECK(opened->Find("segments")->kind == MiniJson::Value::Array);
    CHECK(opened->Find("segments")->arr.size() == 1);

    h.Tick();
    h.Tick();
    h.Tick();
    h.Finish();

    std::filesystem::path const folder = std::filesystem::path(dir) / h.id;
    CHECK(std::filesystem::exists(folder / "seg-0001.jsonl"));
    CHECK(std::filesystem::exists(folder / "seg-0002.jsonl"));
    for (int seg = 1; seg <= 3; ++seg)
    {
        std::vector<std::string> const lines =
            ReadLines((folder / ("seg-000" + std::to_string(seg) + ".jsonl")).string());
        CHECK(!lines.empty());
        CHECK(lines[0].find("{\"k\":\"hdr\",") == 0);
        MiniJson::Value header;
        std::string err;
        CHECK(MiniJson::Parse(lines[0], header, err));
        CHECK(Number(header, "seg") == static_cast<std::uint64_t>(seg));
        CHECK(header.Find("capture")->text == h.id);
    }

    auto finished = IndexObj(dir, h.id);
    if (!finished->Find("ended") || finished->Find("ended")->kind != MiniJson::Value::String)
        std::cerr << "DEBUG raw index: " << ReadFile((folder / "index.json").string()) << "\n";
    CHECK(finished->Find("ended") && finished->Find("ended")->kind == MiniJson::Value::String);
    CHECK(finished->Find("ended")->text == "2025-11-01T12:26:44Z");
    CHECK(finished->Find("endReason")->text == "stopped");
    CHECK(Number(*finished, "dropped") == 0);
    MiniJson::Value const& segments = *finished->Find("segments");
    CHECK(segments.arr.size() == 3);
    CHECK(Number(segments.arr[0], "firstSeq") == 1);
    CHECK(Number(segments.arr[0], "lastSeq") >= 1);
    CHECK(segments.arr[2].Find("deleted")->kind == MiniJson::Value::Bool);
    CHECK(!segments.arr[2].Find("deleted")->b);

    std::vector<std::string> const last = ReadLines((folder / "seg-0003.jsonl").string());
    bool sawEnd = false;
    for (std::string const& line : last)
        sawEnd = sawEnd || line.find("{\"k\":\"end\",") == 0;
    CHECK(sawEnd);
}

void TestCaptureRetention()
{
    std::string const dir = (Root() / "capture-retention").string();
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1, 1);
    h.Tick();
    h.Tick();
    h.Tick();
    h.Finish();

    std::filesystem::path const folder = std::filesystem::path(dir) / h.id;
    auto index = IndexObj(dir, h.id);
    MiniJson::Value const& segments = *index->Find("segments");
    CHECK(segments.arr.size() == 3);
    CHECK(segments.arr[0].Find("deleted")->b);
    CHECK(segments.arr[1].Find("deleted")->b);
    CHECK(!segments.arr[2].Find("deleted")->b);
    CHECK(!std::filesystem::exists(folder / "seg-0001.jsonl"));
    CHECK(!std::filesystem::exists(folder / "seg-0002.jsonl"));
    CHECK(std::filesystem::exists(folder / "seg-0003.jsonl"));
}

Harness EndedCapture(std::string const& dir, std::string const& id, std::uint64_t startedUnixMs)
{
    Harness h = MakeCapture(dir, id, startedUnixMs, 1ull * 1024ull * 1024ull);
    h.Tick();
    h.Finish("stopped");
    return h;
}

void TestTotalRetention()
{
    std::string const dir = (Root() / "total-retention").string();
    std::filesystem::create_directories(dir);
    FileLimits limits;
    limits.totalBytes = 1;

    // Ended before the new capture opens: the oldest one goes.
    EndedCapture(dir, "20251101T122640Z-old-Fionaa", kStart);
    // Running while the new capture opens: kept.
    std::string const active = "20251101T122641Z-active-Fionaa";
    Harness running = MakeCapture(dir, active, kStart + kTickMs, 1ull * 1024ull * 1024ull);
    running.Tick();

    IndexInfo info;
    info.captureId = "20251101T122642Z-new-Fionaa";
    info.commander = "Fionaa";
    info.plan = "zul_gurub";
    info.startedUnixMs = kStart + 2 * kTickMs;
    std::string err;
    std::unique_ptr<FileSink> sink = FileSink::Open(dir, info.captureId, info, limits, err);
    CHECK(sink != nullptr);

    CHECK(!std::filesystem::exists(std::filesystem::path(dir) / "20251101T122640Z-old-Fionaa"));
    MiniJson::Value kept;
    CHECK(ReadJson((std::filesystem::path(dir) / active / "index.json").string(), kept));
    MiniJson::Value fresh;
    CHECK(ReadJson((std::filesystem::path(dir) / info.captureId / "index.json").string(), fresh));
    running.Finish();
}

void TestOffCreatesNothing()
{
    std::string const dir = (Root() / "off").string();
    DecisionCaptureSettings settings;
    settings.enabled = false;
    settings.dir = dir;
    DecisionCaptureRegistry registry(settings);
    CHECK(registry.Begin(7, "Fionaa", "zul_gurub", kStart).empty());
    CHECK(!registry.Active());
    CHECK(!std::filesystem::exists(dir));
    CHECK(registry.lastError().empty());

    DecisionCaptureSettings on = settings;
    on.enabled = true;
    DecisionCaptureRegistry live(on);
    std::string const id = live.Begin(7, "Fionaa", "zul_gurub", kStart);
    CHECK(!id.empty());
    CHECK(live.Active());
    MiniJson::Value opened;
    CHECK(ReadJson((std::filesystem::path(dir) / id / "index.json").string(), opened));
    live.End(7, "test");
    CHECK(!live.Active());
    MiniJson::Value const index = WaitIndex(dir, id);
    CHECK(index.Find("ended")->kind == MiniJson::Value::String);
    CHECK(index.Find("endReason")->text == "test");
}

} // namespace

int main()
{
    TestIds();
    TestSegmentsAndIndex();
    TestCaptureRetention();
    TestTotalRetention();
    TestOffCreatesNothing();
    std::cout << "decision capture files: all checks passed\n";
    return 0;
}
