#pragma once

// Decision capture, file side: one folder per capture, holding index.json and
// seg-NNNN.jsonl segments. The recorder (runtime/DecisionCaptureRecorder.h)
// builds the lines; this file stores them. Every segment is written through a
// RunTraceWriter (bounded queue, its own thread), so after Open no write runs on
// the world thread: Push only queues, and rotation (NewSegment), Finish and the
// retention sweeps are the capture-open work the spec allows there.
//
// Layout: <dir>/<captureId>/index.json, <dir>/<captureId>/seg-0001.jsonl, ...
// Retention: CaptureMiB trims the oldest closed segment of the capture at every
// rotation; TotalMiB trims the oldest ended capture at every capture open.
// Never the open segment, never a running capture.
//
// The index holds what was known at the last write (open, rotation, finish):
// the open segment's counts lag its file by design.

#include "DecisionCaptureRecorder.h"
#include "RunTraceWriter.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TortoiseBots
{

struct IndexInfo
{
    std::string captureId;
    std::string commander;
    std::string plan;
    std::uint32_t map = 0;
    std::uint64_t startedUnixMs = 0;
    std::string schema = DecisionCapture::kDecisionSchemaHash;
};

// The capture's own caps, already in bytes.
struct FileLimits
{
    std::uint64_t segmentBytes = 32ull * 1024ull * 1024ull;
    std::uint64_t captureBytes = 512ull * 1024ull * 1024ull;
    std::uint64_t totalBytes = 4096ull * 1024ull * 1024ull;
    std::size_t recordBytes = 512u * 1024u;
    std::uint32_t keyframeEvery = 60;
};

class FileSink : public DecisionCapture::Sink
{
public:
    ~FileSink() override;

    // Creates <dir>/<captureId>, applies TotalMiB retention, opens seg-0001 and
    // writes index.json. Null with `err` filled when the folder or the file
    // cannot be made.
    static std::unique_ptr<FileSink> Open(std::string const& dir, std::string const& captureId,
        IndexInfo const& info, FileLimits const& limits, std::string& err);

    bool Push(std::string line) override;
    bool NewSegment(std::uint32_t seg, std::uint64_t firstSeq) override;

    // The plan's map is only known on the first tick; the index is rewritten
    // when it changes (once per capture in practice).
    void SetMap(std::uint32_t map);

    // The recorder writes its `end` line through this sink first, then the
    // final index is queued and the writer ended (no summary file).
    void Finish(DecisionCapture::Recorder& recorder, std::uint64_t nowUnixMs, std::string const& reason);

    // Reaps writers whose end marker was handled. World thread.
    void Poll();
    // Shutdown: lets every writer go without waiting for the disk.
    void Detach();

    // Writers still finishing (tests, Shutdown's decision to log).
    std::size_t FinishingWriters() const { return finishing_.size() + (writer_ ? 1 : 0); }

    // <UTC yyyymmddThhmmssZ>-<plan>-<commander>, everything outside
    // [A-Za-z0-9_] replaced by _, plan cut at 48 and commander at 24; -2, -3
    // ... when the folder exists.
    static std::string MakeCaptureId(std::string const& dir, std::uint64_t nowUnixMs,
        std::string const& plan, std::string const& commander);

private:
    struct Segment
    {
        std::string file;
        std::uint64_t firstSeq = 0;
        std::uint64_t lastSeq = 0;
        std::uint64_t records = 0;
        std::uint64_t bytes = 0;
        bool deleted = false;
    };

    FileSink(std::string root, IndexInfo info, FileLimits limits);

    bool StartSegment(std::uint32_t seg, std::uint64_t firstSeq, std::string& err);
    void WriteIndex();
    void ApplyCaptureRetention(std::size_t keep);
    void EndWriter();

    std::string SegmentPath(std::string const& file) const;

    std::string root_;
    IndexInfo info_;
    FileLimits limits_;
    std::unique_ptr<RunTrace::RunTraceWriter> writer_;
    std::vector<std::unique_ptr<RunTrace::RunTraceWriter>> finishing_;
    std::vector<Segment> segments_;
    std::size_t openSegment_ = 0;
    std::uint64_t dropped_ = 0;
    bool ended_ = false;
    std::string endReason_;
    std::uint64_t endedUnixMs_ = 0;
};

} // namespace TortoiseBots
