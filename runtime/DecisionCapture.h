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

#include <chrono>
#include <memory>
#include <string>
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
    explicit DecisionCaptureRegistry(DecisionCaptureSettings settings) : settings_(std::move(settings)) {}

    // "" when off or when the folder cannot be made (`lastError()` says why),
    // otherwise the capture id. A capture this commander still has ends first.
    std::string Begin(std::uint64_t commanderRaw, std::string const& commander,
        std::string const& planName, std::uint64_t nowUnixMs);
    void BeforeTick(std::uint64_t commanderRaw, DungeonRun::Run const& run, DungeonRun::Facts const& facts,
        std::uint32_t dt);
    void AfterTick(std::uint64_t commanderRaw, DungeonRun::Run const& run, std::string const& order);
    void End(std::uint64_t commanderRaw, std::string const& reason);
    void EndAll(std::string const& reason);
    // Reaps writers whose end marker was handled; world thread.
    void Update();
    // Server shutdown: ends every capture, then lets busy writers go.
    void Shutdown();

    bool Active() const { return !captures_.empty(); }
    std::string const& lastError() const { return lastError_; }
    DecisionCaptureSettings const& settings() const { return settings_; }

private:
    struct Capture
    {
        std::unique_ptr<FileSink> sink;
        std::unique_ptr<DecisionCapture::Recorder> recorder;
        std::uint32_t map = 0;
    };

    std::string SettingsJson() const;
    void SinkSettings(FileLimits& file, DecisionCapture::Limits& recorder) const;

    DecisionCaptureSettings settings_;
    std::unordered_map<std::uint64_t, Capture> captures_;
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
    file.totalBytes = DecisionCaptureMiB(settings_.totalMiB);
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
    std::unique_ptr<FileSink> sink = FileSink::Open(settings_.dir, captureId, info, fileLimits, err);
    if (!sink)
    {
        lastError_ = err;
        return std::string();
    }

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
    captures_.erase(it);
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
}

inline void DecisionCaptureRegistry::Shutdown()
{
    // The `end` record and the final index leave at once; a writer still busy
    // is detached, so shutdown never waits for the disk.
    EndAll("server shutdown");
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
        if (registry_.Active())
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
