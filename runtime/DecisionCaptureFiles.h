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
// rotation. TotalMiB is the registry's sweep (DecisionCapture.h): the oldest
// ended captures go at every rotation and at the finish, and a folder still
// over the cap with running captures stops the largest of them. Never the open
// segment, never a running capture's files. A folder is deleted only when its
// index proves it is one of ours and finished (ReadCaptureIndex); a foreign or
// malformed folder, and anything behind a symlink, stays.
//
// The index is written at open and at every rotation with the counts accepted
// so far, and rewritten when a writer is reaped with the bytes and records that
// actually reached the disk. At the finish it first says "incomplete"; the last
// reaped writer turns it into "complete" or "failed". A shutdown that lets a
// writer go leaves "incomplete" (or "failed").

#include "DecisionCaptureRecorder.h"
#include "RunTraceWriter.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{

// The service's log (TB_LOG_BASIC); the file test passes its own or none.
using CaptureLog = std::function<void(std::string const&)>;

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
    std::size_t recordBytes = 512u * 1024u;
    std::uint32_t keyframeEvery = 60;
};

// What TotalMiB retention needs from a capture folder's index.json: whether the
// folder is provably one of ours and finished. Retention deletes nothing else.
struct CaptureIndexSummary
{
    bool deletable = false;
    std::uint64_t startedUnixMs = 0;
};
// Accepts <folder>/index.json only when it is a real file (never a symlink),
// its format and version are ours, `id` equals `expectedId`, `ended` is a UTC
// yyyy-mm-ddThh:mm:ssZ stamp and `status` is complete, incomplete, failed or
// stopped. A missing, foreign or malformed index (and a missing `id`) is not
// deletable.
CaptureIndexSummary ReadCaptureIndex(std::string const& folder, std::string const& expectedId);

// Every regular file under `path`, recursively (0 when it does not exist).
// Symlinks are never followed and never counted.
std::uint64_t DirectoryBytes(std::string const& path);

// One folder as the total cap sees it, measured off the world thread.
struct CaptureFolderState
{
    std::string id;
    std::uint64_t bytes = 0;
    std::uint64_t startedUnixMs = 0;
    bool deletable = false;
};
// One pass over `root`: every plain capture folder measured (segments and any
// stray files) and its index validated. Symlinks are skipped, never followed.
std::vector<CaptureFolderState> ScanCaptureFolders(std::string const& root);

// Deletes <root>/<id>, and only when it is still a plain folder directly under
// the root whose index passes ReadCaptureIndex; false otherwise (nothing
// removed), so a bad id can never reach outside the root.
bool RemoveCaptureFolder(std::string const& root, std::string const& id);

class FileSink : public DecisionCapture::Sink
{
public:
    ~FileSink() override;

    // Creates <dir>/<captureId>, opens seg-0001 and writes index.json. Null with
    // `err` filled when the folder or the file cannot be made.
    static std::unique_ptr<FileSink> Open(std::string const& dir, std::string const& captureId,
        IndexInfo const& info, FileLimits const& limits, std::string& err, CaptureLog log = {});

    bool Push(std::string line) override;
    bool NewSegment(std::uint32_t seg, std::uint64_t firstSeq) override;

    // The plan's map is only known on the first tick; the index is rewritten
    // when it changes (once per capture in practice).
    void SetMap(std::uint32_t map);

    // The recorder writes its `end` line through this sink first, then the
    // final index is queued and the writer ended (no summary file).
    void Finish(DecisionCapture::Recorder& recorder, std::uint64_t nowUnixMs, std::string const& reason);

    // Reaps writers whose end marker was handled, folds their actual counts
    // into the index and, once every writer is gone, writes the final status.
    // World thread.
    void Poll();
    // Shutdown: lets every writer go without waiting for the disk; the index
    // keeps "incomplete" (or "failed").
    void Detach();

    // The registry checks TotalMiB after a rotation and after the finish.
    void SetTotalCapHook(std::function<void()> hook) { totalCapHook_ = std::move(hook); }

    // Test seam: make the final flush of every segment writer fail.
    void SetFailFinalFlushForTest(bool fail);

    // Writers still finishing (tests, Shutdown's decision to log).
    std::size_t FinishingWriters() const { return finishing_.size() + (writer_ ? 1 : 0); }
    bool Ended() const { return ended_; }
    // Every writer has ended and the final index is written.
    bool Done() const { return ended_ && !writer_ && finishing_.empty(); }
    std::string const& CaptureId() const { return info_.captureId; }
    std::string const& Root() const { return root_; }
    std::uint64_t StartedUnixMs() const { return info_.startedUnixMs; }
    // Bytes the segment writers accepted or reaped and that are still on disk:
    // the total cap's per-capture number, never a directory walk.
    std::uint64_t Bytes() const;

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
        std::uint64_t records = 0; // tick and stub lines accepted so far
        std::uint64_t lines = 0;   // every line accepted so far, the header included
        std::uint64_t bytes = 0;
        bool deleted = false;
    };

    struct Finishing
    {
        std::unique_ptr<RunTrace::RunTraceWriter> writer;
        std::size_t segment = 0;
    };

    FileSink(std::string root, IndexInfo info, FileLimits limits, CaptureLog log);

    bool StartSegment(std::uint32_t seg, std::uint64_t firstSeq, std::string& err);
    void WriteIndex();
    void ApplyCaptureRetention(std::size_t keep);
    void EndWriter();
    void ApplyWriterResult(Finishing& finishing, RunTrace::WriterStats const& stats, std::string const& failure);
    void LogFailureOnce(std::string const& message);

    std::string SegmentPath(std::string const& file) const;

    std::string root_;
    IndexInfo info_;
    FileLimits limits_;
    CaptureLog log_;
    std::function<void()> totalCapHook_;
    std::unique_ptr<RunTrace::RunTraceWriter> writer_;
    std::vector<Finishing> finishing_;
    std::vector<Segment> segments_;
    std::size_t openSegment_ = 0;
    std::uint64_t recorderDropped_ = 0;
    std::uint64_t writerDropped_ = 0; // accepted lines the writer later lost
    bool ended_ = false;
    bool failed_ = false;
    bool failureLogged_ = false;
    bool finalized_ = false;
    std::string status_ = "running";
    std::string endReason_;
    std::uint64_t endedUnixMs_ = 0;
};

} // namespace TortoiseBots
