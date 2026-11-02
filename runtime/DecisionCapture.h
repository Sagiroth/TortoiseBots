#pragma once

// Decision capture service: while AiPlayerbot.DecisionCapture.Enabled is on,
// every running commander's run-leader tick is written to its own folder of
// JSONL records (runtime/DecisionCaptureFiles.h, docs/server/decision-capture.md),
// so a live failure can be cut into a replay fixture later.
//
// Hooks (commands/Commander.cpp): Begin at `.bot run start`, BeforeTick and
// AfterTick around the DungeonRun::Tick call, End where a run slot goes away.
// A commander without a capture pays one map lookup per hook.
//
// Threading: every entry point is the world thread (the bots' AI, the command
// queue and Update() all are). Encodes happen there, next to the policy tick;
// every disk write happens on the segment writers' threads through a bounded
// queue. Nothing here reads the database or scans the world.
//
// The registry is header-only (the file side included) so the file-segment
// test drives the same path without the game; SettingsFromConfig and the
// singleton live in DecisionCapture.cpp.

#include "DecisionCaptureFiles.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots
{

struct DecisionCaptureSettings
{
    bool enabled = false;
    // Absolute already: LogsDir joined with AiPlayerbot.DecisionCapture.Dir.
    std::string dir;
    std::string module; // the module version written in every segment header
    std::uint32_t segmentMiB = 32;
    std::uint32_t captureMiB = 512;
    std::uint32_t totalMiB = 4096;
    std::uint32_t recordKiB = 512;
    std::uint32_t keyframeEvery = 60;
};

// Reads AiPlayerbot.DecisionCapture.* once, at construction (config load).
DecisionCaptureSettings DecisionCaptureSettingsFromConfig();

class DecisionCaptureRegistry
{
public:
    // The one full measurement of the capture folder (ScanCaptureFolders by
    // default); tests inject a scanner to record when and where it runs.
    using CaptureFolderScan = std::function<std::vector<CaptureFolderState>(std::string const& root)>;

    explicit DecisionCaptureRegistry(DecisionCaptureSettings settings, CaptureFolderScan scan = {});
    ~DecisionCaptureRegistry();

    // "" when off or when the folder cannot be made (`lastError()` says why),
    // otherwise the capture id. A capture this commander still has ends first.
    std::string Begin(std::uint64_t commanderRaw, std::string const& commander,
        std::string const& planName, std::uint64_t nowUnixMs);
    void BeforeTick(std::uint64_t commanderRaw, DungeonRun::Run const& run, DungeonRun::Facts const& facts,
        std::uint32_t dt);
    void AfterTick(std::uint64_t commanderRaw, DungeonRun::Run const& run, std::string const& order);
    void End(std::uint64_t commanderRaw, std::string const& reason);
    void EndAll(std::string const& reason);
    // Reaps writers whose end marker was handled and applies the total cap;
    // world thread.
    void Update();
    // Server shutdown: ends every capture, then lets busy writers go.
    void Shutdown();

    // The module's log (TB_LOG_BASIC in DecisionCapture.cpp); empty in tests.
    void SetLog(CaptureLog log) { log_ = std::move(log); }

    bool Active() const { return !captures_.empty(); }
    // A finished capture whose writers are still flushing is not active, but
    // its sink must stay alive and be polled.
    bool HasWork() const { return !captures_.empty() || !retired_.empty() || totalCapPending_; }
    std::string const& lastError() const { return lastError_; }
    DecisionCaptureSettings const& settings() const { return settings_; }

private:
    struct Capture
    {
        std::unique_ptr<FileSink> sink;
        std::unique_ptr<DecisionCapture::Recorder> recorder;
        std::uint32_t map = 0;
    };

    // What the total cap knows about one folder on disk without measuring it
    // again: its bytes, when it started, and whether it may be deleted.
    struct FolderRecord
    {
        std::uint64_t bytes = 0;
        std::uint64_t startedUnixMs = 0;
        bool deletable = false;
    };

    std::string SettingsJson() const;
    void SinkSettings(FileLimits& file, DecisionCapture::Limits& recorder) const;
    void EnforceTotalCap(bool stopRunning);
    // Folds the start scan in once its thread is done; false while it runs.
    bool ApplyFolderScan();
    void Retire(std::unique_ptr<FileSink> sink);

    DecisionCaptureSettings settings_;
    std::unordered_map<std::uint64_t, Capture> captures_;
    // Finished captures whose writers still flush: their sinks must outlive
    // End() so Poll() can collect the final counts.
    std::vector<std::unique_ptr<FileSink>> retired_;
    // Every folder the cap knows, measured at service start and kept from the
    // segment writers after that; the sweep never walks the tree.
    std::unordered_map<std::string, FolderRecord> folders_;
    std::future<std::vector<CaptureFolderState>> scan_;
    bool totalCapPending_ = false;
    bool shuttingDown_ = false;
    CaptureLog log_;
    std::string lastError_;
};

inline std::uint64_t DecisionCaptureNowUnixMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

inline std::uint64_t DecisionCaptureMiB(std::uint32_t mib)
{
    return static_cast<std::uint64_t>(mib) * 1024ull * 1024ull;
}

inline DecisionCaptureRegistry::DecisionCaptureRegistry(DecisionCaptureSettings settings, CaptureFolderScan scan)
    : settings_(std::move(settings))
{
    // The only full measurement of the capture folder: off this thread, once,
    // because the world thread must not walk the tree. Update() folds the
    // result in when it is ready.
    if (!settings_.enabled || settings_.dir.empty() || settings_.totalMiB == 0)
        return;
    if (!scan)
        scan = CaptureFolderScan(&ScanCaptureFolders);
    std::string root = settings_.dir;
    scan_ = std::async(std::launch::async,
        [scan = std::move(scan), root = std::move(root)] { return scan(root); });
}

inline DecisionCaptureRegistry::~DecisionCaptureRegistry()
{
    // An injected scanner may be a caller's local; it must not outlive this.
    if (scan_.valid())
        scan_.wait();
}

inline bool DecisionCaptureRegistry::ApplyFolderScan()
{
    if (!scan_.valid())
        return true;
    if (scan_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return false;
    for (CaptureFolderState const& folder : scan_.get())
    {
        // A capture opened while the scan ran already owns its entry.
        folders_.emplace(folder.id, FolderRecord{folder.bytes, folder.startedUnixMs, folder.deletable});
    }
    return true;
}

inline std::string DecisionCaptureRegistry::SettingsJson() const
{
    return "{\"segmentMiB\":" + std::to_string(settings_.segmentMiB) + ",\"captureMiB\":" +
           std::to_string(settings_.captureMiB) + ",\"totalMiB\":" + std::to_string(settings_.totalMiB) +
           ",\"recordKiB\":" + std::to_string(settings_.recordKiB) + ",\"keyframeEvery\":" +
           std::to_string(settings_.keyframeEvery) + "}";
}

inline void DecisionCaptureRegistry::SinkSettings(FileLimits& file, DecisionCapture::Limits& recorder) const
{
    file.segmentBytes = DecisionCaptureMiB(settings_.segmentMiB);
    file.captureBytes = DecisionCaptureMiB(settings_.captureMiB);
    file.recordBytes = static_cast<std::size_t>(settings_.recordKiB) * 1024u;
    file.keyframeEvery = settings_.keyframeEvery;

    recorder.recordBytes = file.recordBytes;
    recorder.keyframeEvery = file.keyframeEvery;
    recorder.segmentBytes = file.segmentBytes;
}

inline std::string DecisionCaptureRegistry::Begin(std::uint64_t commanderRaw, std::string const& commander,
    std::string const& planName, std::uint64_t nowUnixMs)
{
    lastError_.clear();
    End(commanderRaw, "restarted");
    if (!settings_.enabled)
        return std::string();
    if (settings_.dir.empty())
    {
        lastError_ = "AiPlayerbot.DecisionCapture.Dir is empty";
        return std::string();
    }

    FileLimits fileLimits;
    DecisionCapture::Limits recorderLimits;
    SinkSettings(fileLimits, recorderLimits);

    std::string const captureId = FileSink::MakeCaptureId(settings_.dir, nowUnixMs, planName, commander);
    IndexInfo info;
    info.captureId = captureId;
    info.commander = commander;
    info.plan = planName;
    info.startedUnixMs = nowUnixMs;

    std::string err;
    std::unique_ptr<FileSink> sink = FileSink::Open(settings_.dir, captureId, info, fileLimits, err, log_);
    if (!sink)
    {
        lastError_ = err;
        return std::string();
    }
    sink->SetTotalCapHook([this] { totalCapPending_ = true; });
    // From here the cap knows this capture's folder from its writer counters,
    // never from a second walk of the tree.
    folders_.emplace(captureId, FolderRecord{0, nowUnixMs, false});

    DecisionCapture::HeaderInfo header;
    header.captureId = captureId;
    header.commander = commander;
    header.module = settings_.module;
    header.settingsJson = SettingsJson();
    header.startedUnixMs = nowUnixMs;

    Capture capture;
    capture.sink = std::move(sink);
    capture.recorder = std::make_unique<DecisionCapture::Recorder>(
        std::move(header), recorderLimits, *capture.sink);
    captures_.emplace(commanderRaw, std::move(capture));
    // At capture open only ended captures may go; a running one is never cut
    // for a capture that just started.
    EnforceTotalCap(false);
    return captureId;
}

inline void DecisionCaptureRegistry::BeforeTick(std::uint64_t commanderRaw, DungeonRun::Run const& run,
    DungeonRun::Facts const& facts, std::uint32_t dt)
{
    auto it = captures_.find(commanderRaw);
    if (it == captures_.end())
        return;
    Capture& capture = it->second;
    std::uint32_t const map = run.plan ? run.plan->map : 0;
    if (capture.map != map)
    {
        capture.map = map;
        capture.sink->SetMap(map);
    }
    capture.recorder->BeforeTick(run, facts, dt, DecisionCaptureNowUnixMs());
}

inline void DecisionCaptureRegistry::AfterTick(std::uint64_t commanderRaw, DungeonRun::Run const& run,
    std::string const& order)
{
    auto it = captures_.find(commanderRaw);
    if (it == captures_.end())
        return;
    it->second.recorder->AfterTick(run, order);
}

inline void DecisionCaptureRegistry::End(std::uint64_t commanderRaw, std::string const& reason)
{
    auto it = captures_.find(commanderRaw);
    if (it == captures_.end())
        return;
    it->second.sink->Finish(*it->second.recorder, DecisionCaptureNowUnixMs(), reason);
    Retire(std::move(it->second.sink));
    captures_.erase(it);
}

inline void DecisionCaptureRegistry::Retire(std::unique_ptr<FileSink> sink)
{
    if (sink)
        retired_.push_back(std::move(sink));
}

inline void DecisionCaptureRegistry::EndAll(std::string const& reason)
{
    std::vector<std::uint64_t> keys;
    keys.reserve(captures_.size());
    for (auto const& entry : captures_)
        keys.push_back(entry.first);
    for (std::uint64_t key : keys)
        End(key, reason);
}

inline void DecisionCaptureRegistry::Update()
{
    for (auto& entry : captures_)
        entry.second.sink->Poll();
    // A capture the total cap stopped has already written its end record; its
    // recorder and sink leave the active map here.
    for (auto it = captures_.begin(); it != captures_.end();)
    {
        if (it->second.sink->Ended())
        {
            Retire(std::move(it->second.sink));
            it = captures_.erase(it);
        }
        else
            ++it;
    }
    for (auto it = retired_.begin(); it != retired_.end();)
    {
        (*it)->Poll();
        // The final index's numbers, taken from the sink that wrote them.
        FolderRecord& folder = folders_[(*it)->CaptureId()];
        folder.bytes = (*it)->Bytes();
        folder.startedUnixMs = (*it)->StartedUnixMs();
        folder.deletable = (*it)->Ended();
        if ((*it)->Done())
            it = retired_.erase(it);
        else
            ++it;
    }
    if (totalCapPending_ && !shuttingDown_)
    {
        // A finished capture whose writer still flushes is not deletable yet;
        // wait for it rather than stop a running capture in its place.
        bool flushing = false;
        for (auto const& sink : retired_)
            if (!sink->Done())
            {
                flushing = true;
                break;
            }
        if (!flushing)
        {
            totalCapPending_ = false;
            EnforceTotalCap(true);
        }
    }
}

inline void DecisionCaptureRegistry::Shutdown()
{
    // The `end` record and the "incomplete" index leave at once; writers that
    // already flushed are reaped (their index turns "complete"), the rest are
    // detached, so shutdown never waits for the disk.
    shuttingDown_ = true;
    EndAll("server shutdown");
    for (std::unique_ptr<FileSink>& sink : retired_)
    {
        sink->Poll();
        sink->Detach();
    }
    retired_.clear();
    totalCapPending_ = false;
}

// TotalMiB over the capture folders: the sweep runs on the world thread but
// never measures the tree. The one full measurement happens at service start
// (ScanCaptureFolders on its own thread); after that a running capture's bytes
// are the segment writers' reaped counters and a finished capture's are its
// final index's. Ended captures nobody owns any more go oldest first; if the
// running ones alone are over the cap, the largest is stopped (never its files
// deleted).
inline void DecisionCaptureRegistry::EnforceTotalCap(bool stopRunning)
{
    if (!settings_.enabled || settings_.dir.empty() || settings_.totalMiB == 0)
        return;
    if (!ApplyFolderScan())
    {
        // The start measurement still runs; Update sweeps again once it lands.
        totalCapPending_ = true;
        return;
    }
    std::uint64_t const limit = DecisionCaptureMiB(settings_.totalMiB);

    for (auto& entry : captures_)
    {
        FileSink* sink = entry.second.sink.get();
        FolderRecord& folder = folders_[sink->CaptureId()];
        folder.bytes = sink->Bytes();
        folder.startedUnixMs = sink->StartedUnixMs();
        folder.deletable = false;
    }

    std::uint64_t total = 0;
    for (auto const& entry : folders_)
        total += entry.second.bytes;
    if (total <= limit)
        return;

    auto owned = [this](std::string const& id)
    {
        for (auto const& entry : captures_)
            if (entry.second.sink->CaptureId() == id)
                return true;
        for (auto const& sink : retired_)
            if (sink->CaptureId() == id)
                return true;
        return false;
    };

    std::vector<std::pair<std::string, FolderRecord const*>> ended;
    for (auto const& entry : folders_)
        if (entry.second.deletable && !owned(entry.first))
            ended.emplace_back(entry.first, &entry.second);
    std::sort(ended.begin(), ended.end(),
        [](std::pair<std::string, FolderRecord const*> const& a,
            std::pair<std::string, FolderRecord const*> const& b)
        { return a.second->startedUnixMs < b.second->startedUnixMs; });
    for (auto const& candidate : ended)
    {
        if (total <= limit)
            break;
        // Checks the folder and its index again: anything that changed since
        // the start measurement, or was never ours, stays.
        if (!RemoveCaptureFolder(settings_.dir, candidate.first))
            continue;
        total = total > candidate.second->bytes ? total - candidate.second->bytes : 0;
        folders_.erase(candidate.first);
    }
    if (total <= limit || !stopRunning)
        return;

    FileSink* largest = nullptr;
    DecisionCapture::Recorder* largestRecorder = nullptr;
    std::uint64_t largestBytes = 0;
    for (auto& entry : captures_)
    {
        FileSink* sink = entry.second.sink.get();
        if (sink->Ended())
            continue;
        std::uint64_t const bytes = sink->Bytes();
        if (bytes > largestBytes)
        {
            largestBytes = bytes;
            largest = sink;
            largestRecorder = entry.second.recorder.get();
        }
    }
    if (!largest)
        return;
    largest->Finish(*largestRecorder, DecisionCaptureNowUnixMs(), "stopped: total cap");
    if (log_)
        log_("decision capture " + largest->CaptureId() + ": stopped: total cap (" +
             std::to_string(largestBytes) + " bytes of " + std::to_string(total) + " in " +
             settings_.dir + ", limit " + std::to_string(limit) + ")");
}

class DecisionCaptureService
{
public:
    static DecisionCaptureService& Instance();

    std::string Begin(std::uint64_t commanderRaw, std::string const& commander,
        std::string const& planName, std::uint64_t nowUnixMs)
    {
        return registry_.Begin(commanderRaw, commander, planName, nowUnixMs);
    }
    void BeforeTick(std::uint64_t commanderRaw, DungeonRun::Run const& run, DungeonRun::Facts const& facts,
        std::uint32_t dt)
    {
        registry_.BeforeTick(commanderRaw, run, facts, dt);
    }
    void AfterTick(std::uint64_t commanderRaw, DungeonRun::Run const& run, std::string const& order)
    {
        registry_.AfterTick(commanderRaw, run, order);
    }
    void End(std::uint64_t commanderRaw, std::string const& reason) { registry_.End(commanderRaw, reason); }
    void EndAll(std::string const& reason) { registry_.EndAll(reason); }
    void Update()
    {
        if (registry_.HasWork())
            registry_.Update();
    }
    void Shutdown()
    {
        registry_.Shutdown();
    }
    std::string const& lastError() const { return registry_.lastError(); }

private:
    DecisionCaptureService();

    DecisionCaptureRegistry registry_;
};

} // namespace TortoiseBots
