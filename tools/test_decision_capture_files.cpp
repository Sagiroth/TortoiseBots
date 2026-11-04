// Standalone check for runtime/DecisionCaptureFiles.{h,cpp} (decision capture
// C3): the capture folder, segment rotation, retention and index.json, driven
// through the real recorder, plus the service's off path.
//   g++ -std=c++17 -O2 -Wall -Wextra tools/test_decision_capture_files.cpp
//       runtime/RunTraceWriter.cpp runtime/DecisionCaptureFiles.cpp -pthread -o /tmp/t
//   /tmp/t [tmpdir]
#include "../runtime/DecisionCapture.h"
#include "../runtime/DecisionCaptureLimits.h"
#include "DecisionReplay.h"
#include "DecisionReplayScenario.h"

#include <algorithm>
#include <atomic>
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

void WriteDummy(std::string const& path, std::size_t bytes)
{
    std::string const chunk(1024 * 1024, 'x');
    std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
    CHECK(out.is_open());
    while (bytes > 0)
    {
        std::size_t const n = std::min(bytes, chunk.size());
        out.write(chunk.data(), static_cast<std::streamsize>(n));
        bytes -= n;
    }
    CHECK(static_cast<bool>(out));
}

void WriteText(std::string const& path, std::string const& text)
{
    std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
    CHECK(out.is_open());
    out << text;
    CHECK(static_cast<bool>(out));
}

// A hand-written index.json that looks like a capture of ours, so a foreign
// folder can be built beside the service.
std::string IndexText(std::string const& id, std::string const& endedJson, std::string const& status,
    std::string const& format = "tb-decision-capture", std::string const& version = "1")
{
    return "{\"format\":\"" + format + "\",\"v\":" + version + ",\"id\":\"" + id +
        "\",\"commander\":\"Fionaa\",\"plan\":\"old\",\"map\":309,"
        "\"started\":\"2025-11-01T12:00:00Z\",\"startedUnixMs\":1761999000000,\"ended\":" + endedJson +
        ",\"endReason\":\"stopped\",\"status\":\"" + status + "\",\"segments\":[],\"dropped\":0}";
}

DecisionCaptureSettings SmallSettings(std::string const& dir, std::uint32_t totalMiB)
{
    DecisionCaptureSettings settings;
    settings.enabled = true;
    settings.dir = dir;
    settings.module = "test";
    settings.segmentMiB = 1;
    settings.captureMiB = 1;
    settings.totalMiB = totalMiB;
    settings.recordKiB = 512;
    settings.keyframeEvery = 60;
    return settings;
}

void WaitRegistryIdle(DecisionCaptureRegistry& registry)
{
    for (int attempt = 0; attempt < 400 && registry.HasWork(); ++attempt)
    {
        registry.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!registry.HasWork());
}

// Polls the registry until the capture's index says it ended (the total cap
// runs in Update, after the finished writers have flushed).
void WaitCaptureEnded(DecisionCaptureRegistry& registry, std::string const& dir, std::string const& id)
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
                return;
        }
        registry.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cerr << "capture never ended: " << id << "\n";
    std::exit(1);
}

// Starts the service over `dir` and ends one of its own captures, so the total
// cap runs once over what the start measurement found.
void SweepOnce(std::string const& dir)
{
    DecisionCaptureRegistry registry(SmallSettings(dir, 1));
    std::string const id = registry.Begin(1, "Fionaa", "own", kStart);
    CHECK(!id.empty());
    registry.End(1, "stopped");
    WaitRegistryIdle(registry);
    registry.Shutdown();
}

Facts BigFacts(std::size_t members)
{
    Facts facts;
    facts.members.resize(members);
    for (std::size_t i = 0; i < members; ++i)
    {
        DungeonRun::MemberFact& member = facts.members[i];
        member.name = "Bot" + std::to_string(i);
        member.yd = static_cast<float>(i);
        member.x = static_cast<float>(i);
        member.y = static_cast<float>(i);
        member.z = static_cast<float>(i);
    }
    return facts;
}

// Ticks until the capture opens seg-0002; the members vector makes every
// record large enough that a MiB is a few dozen ticks.
bool PumpToRotation(DecisionCaptureRegistry& registry, std::uint64_t key, std::string const& dir,
    std::string const& id, int maxTicks)
{
    Run run;
    Facts facts = BigFacts(400);
    std::filesystem::path const second = std::filesystem::path(dir) / id / "seg-0002.jsonl";
    for (int i = 0; i < maxTicks; ++i)
    {
        ++run.step;
        facts.members[0].yd = static_cast<float>(i);
        registry.BeforeTick(key, run, facts, 1000);
        registry.AfterTick(key, run, "walk");
        if (std::filesystem::exists(second))
            return true;
    }
    return false;
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
    std::uint64_t segmentBytes, std::uint64_t captureBytes = 512ull * 1024ull * 1024ull,
    CaptureLog log = {})
{
    Harness h;
    h.dir = dir;
    h.id = id;
    FileLimits fileLimits;
    fileLimits.segmentBytes = 1ull * 1024ull * 1024ull;
    fileLimits.captureBytes = captureBytes;
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
    h.sink = FileSink::Open(dir, id, info, fileLimits, err, std::move(log));
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
    CHECK(finished->Find("status")->text == "complete");
    CHECK(Number(*finished, "dropped") == 0);
    MiniJson::Value const& segments = *finished->Find("segments");
    CHECK(segments.arr.size() == 3);
    CHECK(Number(segments.arr[0], "firstSeq") == 1);
    CHECK(Number(segments.arr[0], "lastSeq") >= 1);
    CHECK(Number(segments.arr[0], "records") >= 1);
    CHECK(Number(segments.arr[0], "bytes") > 0);
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
    CHECK(index->Find("status")->text == "complete");
    MiniJson::Value const& segments = *index->Find("segments");
    CHECK(segments.arr.size() == 3);
    CHECK(segments.arr[0].Find("deleted")->b);
    CHECK(segments.arr[1].Find("deleted")->b);
    CHECK(!segments.arr[2].Find("deleted")->b);
    CHECK(!std::filesystem::exists(folder / "seg-0001.jsonl"));
    CHECK(!std::filesystem::exists(folder / "seg-0002.jsonl"));
    CHECK(std::filesystem::exists(folder / "seg-0003.jsonl"));
}

void TestTotalRetentionDeletesFinishedAtRotation()
{
    std::string const dir = (Root() / "total-rotation").string();
    // The ended capture is on disk before the service starts, so the one start
    // measurement sees it; the sweep only reads the numbers from then on.
    std::string const oldId = "20251101T120000Z-old-Fionaa";
    {
        Harness old = MakeCapture(dir, oldId, kStart, 1);
        old.Tick();
        old.Finish();
        WriteDummy((std::filesystem::path(dir) / oldId / "blob.bin").string(), 3u * 1024u * 1024u);
    }
    DecisionCaptureRegistry registry(SmallSettings(dir, 2));

    std::string const activeId = registry.Begin(1, "Fionaa", "active", kStart + kTickMs);
    CHECK(!activeId.empty());
    CHECK(PumpToRotation(registry, 1, dir, activeId, 400));
    // The rotation queues the sweep; it runs once its writer has flushed.
    for (int attempt = 0; attempt < 400 && std::filesystem::exists(std::filesystem::path(dir) / oldId); ++attempt)
    {
        registry.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CHECK(!std::filesystem::exists(std::filesystem::path(dir) / oldId));
    CHECK(std::filesystem::exists(std::filesystem::path(dir) / activeId));
    registry.Shutdown();
}

void TestTotalRetentionStopsLargestAtFinish()
{
    std::string const dir = (Root() / "total-finish").string();
    // A folder retention may never touch (no index) still counts, so the
    // running captures alone are over the cap.
    std::filesystem::create_directories(std::filesystem::path(dir) / "stale");
    WriteDummy((std::filesystem::path(dir) / "stale" / "blob.bin").string(), 2u * 1024u * 1024u);

    DecisionCaptureRegistry registry(SmallSettings(dir, 1));

    std::string const largestId = registry.Begin(1, "Fionaa", "largest", kStart);
    std::string const otherId = registry.Begin(2, "Fionaa", "other", kStart + kTickMs);
    CHECK(!largestId.empty());
    CHECK(!otherId.empty());
    // Real bytes for the first capture and no update in between, so the sweep
    // that stops it is the one the finish queues.
    CHECK(PumpToRotation(registry, 1, dir, largestId, 400));

    registry.End(2, "stopped");
    auto other = IndexObj(dir, otherId);
    CHECK(other->Find("endReason")->text == "stopped");

    WaitCaptureEnded(registry, dir, largestId);
    auto largest = IndexObj(dir, largestId);
    CHECK(largest->Find("ended")->kind == MiniJson::Value::String);
    CHECK(largest->Find("endReason")->text == "stopped: total cap");
    CHECK(std::filesystem::exists(std::filesystem::path(dir) / "stale"));
    registry.Shutdown();
}

void TestRetentionDeletesOnlyAFinishedCapture()
{
    std::string const dir = (Root() / "retention-control").string();
    std::string const id = "20251101T120000Z-old-Fionaa";
    std::filesystem::path const folder = std::filesystem::path(dir) / id;
    std::filesystem::create_directories(folder);
    WriteText((folder / "index.json").string(), IndexText(id, "\"2025-11-01T12:00:00Z\"", "complete"));
    WriteDummy((folder / "blob.bin").string(), 2u * 1024u * 1024u);

    SweepOnce(dir);
    CHECK(!std::filesystem::exists(folder));
}

// Fail closed: a folder whose index is not provably ours and finished is never
// deleted, however much space it takes.
void TestRetentionKeepsForeignFolders()
{
    std::vector<std::pair<std::string, std::string>> cases;
    cases.emplace_back("look-alike-format",
        IndexText("look-alike-format", "\"2025-11-01T12:00:00Z\"", "complete", "tb-capture-foreign"));
    cases.emplace_back("version-2",
        IndexText("version-2", "\"2025-11-01T12:00:00Z\"", "complete", "tb-decision-capture", "2"));
    cases.emplace_back("id-mismatch", IndexText("other-capture", "\"2025-11-01T12:00:00Z\"", "complete"));
    cases.emplace_back("bad-stamp", IndexText("bad-stamp", "\"2025-13-01T12:00:00Z\"", "complete"));
    cases.emplace_back("numeric-ended", IndexText("numeric-ended", "1761999000000", "complete"));
    cases.emplace_back("null-ended", IndexText("null-ended", "null", "complete"));
    cases.emplace_back("status-running", IndexText("status-running", "\"2025-11-01T12:00:00Z\"", "running"));
    cases.emplace_back("status-missing",
        "{\"format\":\"tb-decision-capture\",\"v\":1,\"id\":\"status-missing\","
        "\"ended\":\"2025-11-01T12:00:00Z\"}");
    cases.emplace_back("no-index", "");

    std::filesystem::path const base = Root();
    for (auto const& item : cases)
    {
        std::string const dir = (base / ("foreign-" + item.first)).string();
        std::filesystem::path const folder = std::filesystem::path(dir) / item.first;
        std::filesystem::create_directories(folder);
        if (!item.second.empty())
            WriteText((folder / "index.json").string(), item.second);
        WriteDummy((folder / "blob.bin").string(), 2u * 1024u * 1024u);

        SweepOnce(dir);
        CHECK(std::filesystem::exists(folder));
        CHECK(std::filesystem::exists(folder / "blob.bin"));
    }
}

void TestRetentionKeepsSymlinks()
{
    std::filesystem::path const base = Root();
    std::string const dir = (base / "retention-symlinks").string();
    std::filesystem::create_directories(dir);

    // index.json itself is a symlink: the folder is not provably ours.
    std::string const linkedIndex = "linked-index";
    std::filesystem::path const indexTarget = base / "index-target.json";
    WriteText(indexTarget.string(), IndexText(linkedIndex, "\"2025-11-01T12:00:00Z\"", "complete"));
    std::filesystem::path const indexFolder = std::filesystem::path(dir) / linkedIndex;
    std::filesystem::create_directories(indexFolder);
    WriteDummy((indexFolder / "blob.bin").string(), 2u * 1024u * 1024u);
    std::filesystem::create_symlink(indexTarget, indexFolder / "index.json");

    // The folder entry is a symlink: never followed, never counted, never
    // deleted, and the folder it points at stays.
    std::string const linkedFolder = "linked-folder";
    std::filesystem::path const folderTarget = base / "folder-target";
    std::filesystem::create_directories(folderTarget);
    WriteText(
        (folderTarget / "index.json").string(), IndexText(linkedFolder, "\"2025-11-01T12:00:00Z\"", "complete"));
    WriteDummy((folderTarget / "blob.bin").string(), 2u * 1024u * 1024u);
    std::filesystem::create_directory_symlink(folderTarget, std::filesystem::path(dir) / linkedFolder);

    SweepOnce(dir);

    CHECK(std::filesystem::exists(indexFolder / "index.json"));
    CHECK(std::filesystem::exists(indexTarget));
    CHECK(std::filesystem::is_symlink(std::filesystem::path(dir) / linkedFolder));
    CHECK(std::filesystem::exists(folderTarget / "index.json"));

    // Whatever the name says, a removal never leaves the root.
    CHECK(!RemoveCaptureFolder(dir, "../folder-target"));
    CHECK(!RemoveCaptureFolder(dir, linkedFolder));
    CHECK(std::filesystem::exists(folderTarget / "index.json"));
}

// The sweep runs on the world thread; the tree is measured once, on its own
// thread, at service start.
void TestSweepMeasuresOffWorldThread()
{
    std::string const dir = (Root() / "sweep-thread").string();
    std::string const oldId = "20251101T120000Z-old-Fionaa";
    {
        Harness old = MakeCapture(dir, oldId, kStart, 1);
        old.Tick();
        old.Finish();
        WriteDummy((std::filesystem::path(dir) / oldId / "blob.bin").string(), 2u * 1024u * 1024u);
    }

    std::thread::id const world = std::this_thread::get_id();
    std::atomic<int> scans{0};
    std::atomic<int> scansOffWorld{0};
    auto counting = [&](std::string const& root)
    {
        ++scans;
        if (std::this_thread::get_id() != world)
            ++scansOffWorld;
        return ScanCaptureFolders(root);
    };

    DecisionCaptureRegistry registry(SmallSettings(dir, 1), counting);
    std::string const id = registry.Begin(1, "Fionaa", "live", kStart);
    CHECK(!id.empty());
    registry.End(1, "stopped");
    WaitRegistryIdle(registry);

    // One measurement, off the world thread; the sweeps only used the numbers
    // it left behind (and still deleted the finished capture).
    CHECK(scans.load() == 1);
    CHECK(scansOffWorld.load() == 1);
    CHECK(!std::filesystem::exists(std::filesystem::path(dir) / oldId));
    registry.Shutdown();
}

void TestWriterFailureStatus()
{
    std::string const dir = (Root() / "writer-failure").string();
    int logs = 0;
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1ull * 1024ull * 1024ull,
        512ull * 1024ull * 1024ull, [&logs](std::string const&) { ++logs; });
    h.Tick();
    h.Tick();
    h.sink->SetFailFinalFlushForTest(true);
    h.Finish();

    auto index = IndexObj(dir, h.id);
    CHECK(index->Find("status")->text == "failed");
    MiniJson::Value const& segments = *index->Find("segments");
    CHECK(Number(segments.arr[0], "bytes") > 0);
    CHECK(Number(segments.arr[0], "records") >= 2);
    CHECK(logs == 1);
}

void TestShutdownLeavesIncomplete()
{
    std::string const dir = (Root() / "shutdown").string();
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1ull * 1024ull * 1024ull);
    h.Tick();
    h.Tick();
    h.sink->Finish(*h.recorder, h.now + kTickMs, "server shutdown");

    auto index = IndexObj(dir, h.id);
    CHECK(index->Find("ended")->kind == MiniJson::Value::String);
    CHECK(index->Find("status")->text == "incomplete");

    // The writers are let go; nothing rewrites the index after them.
    h.sink->Detach();
    auto after = IndexObj(dir, h.id);
    CHECK(after->Find("status")->text == "incomplete");
}

// A rotation whose segment cannot be opened ends the capture; the index must
// say failed, not complete, and the failure is logged once.
void TestRotationOpenFailureMarksFailed()
{
    std::string const dir = (Root() / "rotation-failure").string();
    int logs = 0;
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1, 512ull * 1024ull * 1024ull,
        [&logs](std::string const&) { ++logs; });
    h.sink->SetFailSegmentOpenForTest(true);
    h.Tick(); // the first record fills a one-byte segment
    h.Tick(); // rotation: the next file cannot be opened, the recorder stops
    h.Tick(); // ignored
    CHECK(h.recorder->Stopped());
    h.Finish();

    auto index = IndexObj(dir, h.id);
    CHECK(index->Find("status")->text == "failed");
    CHECK(logs == 1);
    CHECK(!std::filesystem::exists(std::filesystem::path(dir) / h.id / "seg-0002.jsonl"));
    std::vector<std::string> const lines = ReadLines((std::filesystem::path(dir) / h.id / "seg-0001.jsonl").string());
    CHECK(!lines.empty());
    CHECK(lines.back().find("\"k\":\"end\"") == std::string::npos);
}

// The first index write fails: Open reports it and leaves no folder behind, so
// no unindexable folder is ever created.
void TestIndexWriteFailureAtOpen()
{
    std::string const dir = (Root() / "index-open-failure").string();
    std::filesystem::create_directories(dir);
    std::string const id = "20251101T122640Z-zul_gurub-Fionaa";
    IndexInfo info;
    info.captureId = id;
    info.commander = "Fionaa";
    info.plan = "zul_gurub";
    info.startedUnixMs = kStart;
    FileLimits fileLimits;

    FileSink::SetFailIndexWriteForTest(true);
    std::string err;
    std::unique_ptr<FileSink> sink =
        FileSink::Open(dir, id, info, fileLimits, err, [](std::string const&) {});
    FileSink::SetFailIndexWriteForTest(false);

    CHECK(sink == nullptr);
    CHECK(!err.empty());
    CHECK(!std::filesystem::exists(std::filesystem::path(dir) / id));
}

// The rotation's index write fails and never recovers: the recorder stops at
// once (nothing piles up unindexed), the failure is logged once, the orphan
// segment stays an empty inert file, and the folder proves nothing to
// retention: no ended index ever lands, so it is not deletable and never
// removed.
void TestIndexWriteFailureAtRotation()
{
    std::string const dir = (Root() / "index-rotation-failure").string();
    int logs = 0;
    std::vector<std::string> messages;
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1, 512ull * 1024ull * 1024ull,
        [&logs, &messages](std::string const& message)
        {
            ++logs;
            messages.push_back(message);
        });
    FileSink::SetFailIndexWriteForTest(true);
    h.Tick(); // the first record fills a one-byte segment
    h.Tick(); // rotation: the new segment's index cannot be written, the recorder stops
    h.Tick(); // ignored
    CHECK(h.recorder->Stopped());
    CHECK(h.sink->IndexFailed());
    h.Finish();
    FileSink::SetFailIndexWriteForTest(false);

    CHECK(h.sink->Status() == "failed");
    CHECK(logs == 1);
    CHECK(messages[0].find("index.json") != std::string::npos);
    std::filesystem::path const folder = std::filesystem::path(dir) / h.id;
    CHECK(std::filesystem::exists(folder));
    // The orphan rotation holds no records: the recorder stopped before any
    // header or tick could reach it.
    CHECK(ReadLines((folder / "seg-0002.jsonl").string()).empty());
    // On disk is still the open-time index: running, never ended.
    auto index = IndexObj(dir, h.id);
    CHECK(index->Find("status")->text == "running");
    CHECK(!index->Find("ended") || index->Find("ended")->kind != MiniJson::Value::String);
    CHECK(!ReadCaptureIndex(folder.string(), h.id).deletable);
    CHECK(!RemoveCaptureFolder(dir, h.id));
    CHECK(std::filesystem::exists(folder));
}

// The final index writes fail: the capture still ends failed (never complete),
// the failure is logged once, and retention cannot mistake the folder for a
// healthy finished capture.
void TestIndexWriteFailureAtFinish()
{
    std::string const dir = (Root() / "index-finish-failure").string();
    int logs = 0;
    std::vector<std::string> messages;
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1ull * 1024ull * 1024ull,
        512ull * 1024ull * 1024ull, [&logs, &messages](std::string const& message)
        {
            ++logs;
            messages.push_back(message);
        });
    h.Tick();
    FileSink::SetFailIndexWriteForTest(true);
    h.Finish();
    FileSink::SetFailIndexWriteForTest(false);

    CHECK(h.sink->IndexFailed());
    CHECK(h.sink->Status() == "failed");
    CHECK(logs == 1);
    CHECK(messages[0].find("index.json") != std::string::npos);
    auto index = IndexObj(dir, h.id);
    CHECK(!index->Find("ended") || index->Find("ended")->kind != MiniJson::Value::String);
    std::filesystem::path const folder = std::filesystem::path(dir) / h.id;
    CHECK(!ReadCaptureIndex(folder.string(), h.id).deletable);
    CHECK(!RemoveCaptureFolder(dir, h.id));
    CHECK(std::filesystem::exists(folder));
}

// A passing outage heals: the retry lands, segment writes resume, and the
// capture still finishes complete.
void TestIndexWriteTransientHeals()
{
    std::string const dir = (Root() / "index-transient").string();
    int logs = 0;
    Harness h = MakeCapture(dir, "20251101T122640Z-zul_gurub-Fionaa", kStart, 1ull * 1024ull * 1024ull,
        512ull * 1024ull * 1024ull, [&logs](std::string const&) { ++logs; });
    h.Tick();
    FileSink::SetFailIndexWriteForTest(true);
    h.sink->SetMap(309); // the map rewrite fails
    CHECK(h.sink->IndexFailed());
    FileSink::SetFailIndexWriteForTest(false);
    h.sink->RetryIndex();
    CHECK(!h.sink->IndexFailed());
    h.Tick();
    h.Finish();

    auto index = IndexObj(dir, h.id);
    CHECK(index->Find("status")->text == "complete");
    CHECK(Number(*index, "map") == 309);
    CHECK(logs == 1);
}

// Through the registry: a capture whose index keeps failing is stopped with a
// logged reason instead of accumulating segments. While nothing can be written
// its folder proves nothing to retention (kept, never deleted); once writes
// heal the final index lands failed and the folder rejoins normal collection.
void TestIndexWriteFailureStopsRegistryCapture()
{
    std::string const dir = (Root() / "index-registry-stop").string();
    std::filesystem::create_directories(dir);
    std::vector<std::string> messages;
    DecisionCaptureRegistry registry(SmallSettings(dir, 1));
    registry.SetLog([&messages](std::string const& message) { messages.push_back(message); });

    std::string const id = registry.Begin(1, "Fionaa", "active", kStart);
    CHECK(!id.empty());
    FileSink::SetFailIndexWriteForTest(true);
    CHECK(PumpToRotation(registry, 1, dir, id, 400));
    registry.Update();
    WaitRegistryIdle(registry);

    bool stopped = false;
    for (std::string const& message : messages)
        stopped = stopped || message.find("stopped: index write failed") != std::string::npos;
    CHECK(stopped);
    CHECK(!registry.Active());
    std::filesystem::path const folder = std::filesystem::path(dir) / id;
    CHECK(std::filesystem::exists(folder));
    CHECK(!ReadCaptureIndex(folder.string(), id).deletable);
    CHECK(!RemoveCaptureFolder(dir, id));
    FileSink::SetFailIndexWriteForTest(false);

    // A valid finished capture beside it, over the cap: the sweep must collect
    // that one and still never touch the folder that proves nothing.
    std::string const goodId = "20251101T120000Z-good-Fionaa";
    {
        Harness done = MakeCapture(dir, goodId, kStart, 1ull * 1024ull * 1024ull);
        done.Tick();
        done.Finish();
        WriteDummy((std::filesystem::path(dir) / goodId / "blob.bin").string(), 2u * 1024u * 1024u);
    }
    SweepOnce(dir);
    CHECK(!std::filesystem::exists(std::filesystem::path(dir) / goodId));
    CHECK(std::filesystem::exists(folder));
    registry.Shutdown();
}
// Every DecisionCapture limit clamps signed input low, never by wrapping a
// negative into a uint32 and landing on the high bound.
void TestLimitClamps()
{
    using namespace TortoiseBots::DecisionCapture;
    CHECK(ClampSegmentMiB(-1) == kMinSegmentMiB);
    CHECK(ClampSegmentMiB(0) == kMinSegmentMiB);
    CHECK(ClampSegmentMiB(32) == 32);
    CHECK(ClampSegmentMiB(100000) == kMaxSegmentMiB);

    CHECK(ClampCaptureMiB(-1, 32) == 32);
    CHECK(ClampCaptureMiB(16, 32) == 32);
    CHECK(ClampCaptureMiB(512, 32) == 512);
    CHECK(ClampCaptureMiB(100000, 32) == kMaxCaptureMiB);

    CHECK(ClampTotalMiB(-1, 512) == 512);
    CHECK(ClampTotalMiB(256, 512) == 512);
    CHECK(ClampTotalMiB(4096, 512) == 4096);
    CHECK(ClampTotalMiB(1000000, 512) == kMaxTotalMiB);

    CHECK(ClampRecordKiB(-1) == kMinRecordKiB);
    CHECK(ClampRecordKiB(512) == 512);
    CHECK(ClampRecordKiB(100000) == kMaxRecordKiB);

    CHECK(ClampKeyframeEvery(-1) == kMinKeyframeEvery);
    CHECK(ClampKeyframeEvery(60) == 60);
    CHECK(ClampKeyframeEvery(100000) == kMaxKeyframeEvery);
}

// The whole offline path: the shared scenario recorded through the real
// recorder and the real file sink into a temp folder, then the segment loaded,
// cut into a fixture and replayed exactly as `replay_decisions run` does.
void TestFileRoundTrip()
{
    std::string const dir = (Root() / "round-trip").string();
    std::filesystem::create_directories(dir);

    ai::DungeonPlan plan;
    ai::BossTacticTable tactics;
    DecisionReplay::BuildScenarioPlan(plan, tactics);

    DecisionCapture::HeaderInfo header = DecisionReplay::ScenarioHeader();
    std::string const id = FileSink::MakeCaptureId(dir, header.startedUnixMs, plan.name, header.commander);

    FileLimits fileLimits;
    fileLimits.segmentBytes = 32ull * 1024ull * 1024ull;
    fileLimits.recordBytes = 512u * 1024u;
    fileLimits.keyframeEvery = 60;
    DecisionCapture::Limits limits;
    limits.segmentBytes = fileLimits.segmentBytes;
    limits.recordBytes = fileLimits.recordBytes;
    limits.keyframeEvery = fileLimits.keyframeEvery;

    IndexInfo info;
    info.captureId = id;
    info.commander = header.commander;
    info.plan = plan.name;
    info.map = plan.map;
    info.startedUnixMs = header.startedUnixMs;
    std::string err;
    std::unique_ptr<FileSink> sink = FileSink::Open(dir, id, info, fileLimits, err);
    CHECK(sink != nullptr);
    DecisionCapture::Recorder recorder(header, limits, *sink);

    Run run = DungeonRun::Start(&plan);
    run.liveTactics = &tactics;
    for (std::uint64_t seq = 1; seq <= DecisionReplay::kScenarioTicks; ++seq)
    {
        Facts facts;
        DecisionReplay::ScenarioFacts(seq, facts);
        recorder.BeforeTick(run, facts, 1000, header.startedUnixMs + seq * 1000ull);
        std::string const order = DungeonRun::Tick(run, facts, 1000);
        recorder.AfterTick(run, order);
        if (seq == DecisionReplay::kScenarioGlueAfterSeq)
            run.leadWalkReset = true;
    }
    sink->Finish(recorder, header.startedUnixMs + (DecisionReplay::kScenarioTicks + 1) * 1000ull, "stopped");
    WaitWriters(*sink);

    std::string const segment = (std::filesystem::path(dir) / id / "seg-0001.jsonl").string();
    DecisionReplay::Capture capture;
    CHECK(DecisionReplay::LoadSegments({segment}, capture, err));
    CHECK(capture.ticks.size() == DecisionReplay::kScenarioTicks);

    DecisionReplay::ExtractOptions options;
    options.fromSeq = 1;
    options.toSeq = DecisionReplay::kScenarioTicks;
    options.name = "round-trip";
    options.status = "fixed";
    options.outDir = (std::filesystem::path(dir) / "fixtures").string();
    std::string fixturePath;
    CHECK(DecisionReplay::Extract(capture, options, fixturePath, err));

    DecisionReplay::Fixture fixture;
    CHECK(DecisionReplay::LoadFixture(fixturePath, fixture, err));
    DecisionReplay::Report const report = DecisionReplay::Replay(fixture);
    for (std::string const& failure : report.failures)
        std::cerr << failure << "\n";
    for (DecisionReplay::TickResult const& tick : report.ticks)
        if (!tick.reproduced)
        {
            std::cerr << "tick " << tick.seq << " did not reproduce\n";
            for (std::string const& diff : tick.diffs)
                std::cerr << "  " << diff << "\n";
        }
    CHECK(report.pass);
    CHECK(report.ticks.size() == DecisionReplay::kScenarioTicks);
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
    TestTotalRetentionDeletesFinishedAtRotation();
    TestTotalRetentionStopsLargestAtFinish();
    TestRetentionDeletesOnlyAFinishedCapture();
    TestRetentionKeepsForeignFolders();
    TestRetentionKeepsSymlinks();
    TestSweepMeasuresOffWorldThread();
    TestWriterFailureStatus();
    TestShutdownLeavesIncomplete();
    TestRotationOpenFailureMarksFailed();
    TestIndexWriteFailureAtOpen();
    TestIndexWriteFailureAtRotation();
    TestIndexWriteFailureAtFinish();
    TestIndexWriteTransientHeals();
    TestIndexWriteFailureStopsRegistryCapture();
    TestLimitClamps();
    TestFileRoundTrip();
    TestOffCreatesNothing();
    std::cout << "decision capture files: all checks passed\n";
    return 0;
}
