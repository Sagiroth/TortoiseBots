#include "DecisionCaptureFiles.h"

#include "MiniJson.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace
{

// UTC yyyymmddThhmmssZ for a file name, UTC 2026-11-02T16:19:39Z for the index.
std::string UtcStamp(std::uint64_t unixMs, bool readable)
{
    std::time_t const t = static_cast<std::time_t>(unixMs / 1000u);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    if (readable)
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
            tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    else
        std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
            tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// [A-Za-z0-9_] kept, every other byte one '_', cut to `limit` characters.
std::string Sanitise(std::string const& text, std::size_t limit)
{
    std::string out;
    out.reserve(std::min(text.size(), limit));
    for (unsigned char c : text)
    {
        if (out.size() >= limit)
            break;
        bool const keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        out.push_back(keep ? static_cast<char>(c) : '_');
    }
    if (out.empty())
        out = "_";
    return out;
}

// Digits only: a JSON number like "1.0", "-1" or "1e3" is not a version or a
// millisecond count.
bool DigitValue(MiniJson::Value const* value, std::uint64_t& out)
{
    if (!value || value->kind != MiniJson::Value::Number || value->text.empty())
        return false;
    std::uint64_t parsed = 0;
    for (char c : value->text)
    {
        if (c < '0' || c > '9')
            return false;
        std::uint64_t const digit = static_cast<std::uint64_t>(c - '0');
        if (parsed > (UINT64_MAX - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    out = parsed;
    return true;
}

// UTC yyyy-mm-ddThh:mm:ssZ, the only stamp the index writer produces.
bool ValidUtcStamp(std::string const& text)
{
    auto field = [&text](std::size_t pos, std::size_t count, unsigned low, unsigned high)
    {
        unsigned value = 0;
        for (std::size_t i = pos; i < pos + count; ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return false;
            value = value * 10 + static_cast<unsigned>(text[i] - '0');
        }
        return value >= low && value <= high;
    };
    return text.size() == 20 && text[4] == '-' && text[7] == '-' && text[10] == 'T' && text[13] == ':' &&
        text[16] == ':' && text[19] == 'Z' && field(0, 4, 1, 9999) && field(5, 2, 1, 12) &&
        field(8, 2, 1, 31) && field(11, 2, 0, 23) && field(14, 2, 0, 59) && field(17, 2, 0, 59);
}

// A plain capture folder directly under `root`: never a symlink, never a path
// that resolves outside the root, whatever the name says.
bool PlainFolder(std::string const& root, std::string const& id, std::filesystem::path& path)
{
    if (id.empty() || id == "." || id == ".." || id.find_first_of("/\\") != std::string::npos)
        return false;
    path = std::filesystem::path(root) / id;
    std::error_code ec;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(path, ec)) || ec)
        return false;
    std::filesystem::path const canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec)
        return false;
    std::filesystem::path const canonicalPath = std::filesystem::weakly_canonical(path, ec);
    return !ec && canonicalPath.parent_path() == canonicalRoot;
}

bool IsEndedStatus(std::string const& status)
{
    return status == "complete" || status == "incomplete" || status == "failed" || status == "stopped";
}

// The record's own seq, for the index: only tick and stub lines carry one.
bool RecordSeq(std::string const& line, std::uint64_t& seq)
{
    bool const record = line.rfind("{\"k\":\"tick\",", 0) == 0 || line.rfind("{\"k\":\"stub\",", 0) == 0;
    if (!record)
        return false;
    std::size_t const pos = line.find("\"seq\":");
    if (pos == std::string::npos)
        return false;
    seq = std::strtoull(line.c_str() + pos + 6, nullptr, 10);
    return true;
}

} // namespace

CaptureIndexSummary ReadCaptureIndex(std::string const& folder, std::string const& expectedId)
{
    CaptureIndexSummary summary;
    std::filesystem::path const path = std::filesystem::path(folder) / "index.json";
    std::error_code ec;
    // A symlinked index points anywhere; only the file in the folder counts.
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, ec)) || ec)
        return summary;
    std::ifstream in(path.string(), std::ios::in | std::ios::binary);
    if (!in.is_open())
        return summary;
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MiniJson::Value root;
    std::string err;
    if (!MiniJson::Parse(text, root, err) || root.kind != MiniJson::Value::Object)
        return summary;

    MiniJson::Value const* format = root.Find("format");
    MiniJson::Value const* version = root.Find("v");
    MiniJson::Value const* id = root.Find("id");
    MiniJson::Value const* ended = root.Find("ended");
    MiniJson::Value const* status = root.Find("status");
    std::uint64_t parsedVersion = 0;
    if (!format || format->kind != MiniJson::Value::String || format->text != "tb-decision-capture")
        return summary;
    if (!DigitValue(version, parsedVersion) || parsedVersion != 1)
        return summary;
    if (!id || id->kind != MiniJson::Value::String || id->text != expectedId)
        return summary;
    if (!ended || ended->kind != MiniJson::Value::String || !ValidUtcStamp(ended->text))
        return summary;
    if (!status || status->kind != MiniJson::Value::String || !IsEndedStatus(status->text))
        return summary;

    if (MiniJson::Value const* started = root.Find("startedUnixMs"))
        DigitValue(started, summary.startedUnixMs);
    summary.deletable = true;
    return summary;
}

std::uint64_t DirectoryBytes(std::string const& path)
{
    std::uint64_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code fileEc;
        if (it->is_symlink(fileEc) || fileEc)
            continue;
        if (!it->is_regular_file(fileEc) || fileEc)
            continue;
        std::uintmax_t const size = it->file_size(fileEc);
        if (!fileEc)
            total += size;
    }
    return total;
}

std::vector<CaptureFolderState> ScanCaptureFolders(std::string const& root)
{
    std::vector<CaptureFolderState> folders;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(std::filesystem::path(root), ec), end; !ec && it != end;
         it.increment(ec))
    {
        std::string const id = it->path().filename().string();
        std::filesystem::path path;
        if (!PlainFolder(root, id, path))
            continue;
        CaptureIndexSummary const index = ReadCaptureIndex(path.string(), id);
        CaptureFolderState folder;
        folder.id = id;
        folder.bytes = DirectoryBytes(path.string());
        folder.deletable = index.deletable;
        folder.startedUnixMs = index.startedUnixMs;
        folders.push_back(std::move(folder));
    }
    return folders;
}

bool RemoveCaptureFolder(std::string const& root, std::string const& id)
{
    std::filesystem::path path;
    if (!PlainFolder(root, id, path))
        return false;
    if (!ReadCaptureIndex(path.string(), id).deletable)
        return false;
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
}

FileSink::FileSink(std::string root, IndexInfo info, FileLimits limits, CaptureLog log)
    : root_(std::move(root)), info_(std::move(info)), limits_(limits), log_(std::move(log))
{
}

FileSink::~FileSink()
{
    Detach();
}

std::unique_ptr<FileSink> FileSink::Open(std::string const& dir, std::string const& captureId,
    IndexInfo const& info, FileLimits const& limits, std::string& err, CaptureLog log)
{
    std::filesystem::path const root = std::filesystem::path(dir) / captureId;
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        err = "cannot create " + root.string() + ": " + ec.message();
        return nullptr;
    }
    std::unique_ptr<FileSink> sink(new FileSink(root.string(), info, limits, std::move(log)));
    if (!sink->StartSegment(1, 1, err))
        return nullptr;
    return sink;
}

bool FileSink::Push(std::string line)
{
    if (!writer_)
        return false;
    std::uint64_t const size = line.size();
    std::uint64_t seq = 0;
    bool const record = RecordSeq(line, seq);
    if (!writer_->Push(std::move(line)))
        return false;
    Segment& segment = segments_[openSegment_];
    segment.bytes += size;
    ++segment.lines;
    if (record)
    {
        ++segment.records;
        if (seq > segment.lastSeq)
            segment.lastSeq = seq;
    }
    return true;
}

bool FileSink::NewSegment(std::uint32_t seg, std::uint64_t firstSeq)
{
    std::string err;
    if (!failSegmentOpenForTest_ && StartSegment(seg, firstSeq, err))
        return true;
    // The recorder stops on a refused rotation. The current writer is already
    // closed, so the capture is over: say "failed" rather than let the final
    // index call a truncated capture complete.
    if (err.empty())
        err = "test failure";
    failed_ = true;
    LogFailureOnce("segment " + std::to_string(seg) + " not opened: " + err);
    return false;
}

void FileSink::SetMap(std::uint32_t map)
{
    if (info_.map == map)
        return;
    info_.map = map;
    WriteIndex();
}

void FileSink::Finish(DecisionCapture::Recorder& recorder, std::uint64_t nowUnixMs, std::string const& reason)
{
    if (ended_)
        return;
    recorder.End(nowUnixMs, reason);
    recorderDropped_ = recorder.stats().dropped;
    endReason_ = reason;
    endedUnixMs_ = nowUnixMs;
    ended_ = true;
    // The writers have not flushed yet: this says so until Poll() has reaped
    // every one of them.
    status_ = failed_ ? "failed" : "incomplete";
    WriteIndex();
    EndWriter();
    if (totalCapHook_)
        totalCapHook_();
}

void FileSink::Poll()
{
    bool changed = false;
    for (auto it = finishing_.begin(); it != finishing_.end();)
    {
        if (!it->writer->Finished())
        {
            ++it;
            continue;
        }
        std::string const failure = it->writer->Failure();
        RunTrace::WriterStats const stats = it->writer->Reap();
        ApplyWriterResult(*it, stats, failure);
        it = finishing_.erase(it);
        changed = true;
    }
    if (finalized_)
        return;
    if (ended_ && !writer_ && finishing_.empty())
    {
        status_ = failed_ ? "failed" : "complete";
        finalized_ = true;
        changed = true;
    }
    if (changed)
        WriteIndex();
}

void FileSink::Detach()
{
    if (writer_)
    {
        writer_->Detach();
        writer_.reset();
    }
    for (Finishing& finishing : finishing_)
        finishing.writer->Detach();
    finishing_.clear();
}

void FileSink::SetFailFinalFlushForTest(bool fail)
{
    if (writer_)
        writer_->SetFailFinalFlushForTest(fail);
    for (Finishing& finishing : finishing_)
        finishing.writer->SetFailFinalFlushForTest(fail);
}

// The writer's own counters are what reached the disk: its bytes include the
// newlines, and the lines it lost are the tail of what Push accepted (the
// writer never drops from the middle of its queue).
void FileSink::ApplyWriterResult(Finishing& finishing, RunTrace::WriterStats const& stats,
    std::string const& failure)
{
    if (finishing.segment < segments_.size())
    {
        Segment& segment = segments_[finishing.segment];
        std::uint64_t const lost = segment.lines > stats.records ? segment.lines - stats.records : 0;
        segment.bytes = stats.bytes;
        segment.records = segment.records > lost ? segment.records - lost : 0;
        writerDropped_ += lost;
    }
    if (!stats.failed && failure.empty())
        return;
    failed_ = true;
    if (status_ == "running" || status_ == "incomplete")
        status_ = "failed";
    LogFailureOnce(failure.empty() ? "segment writer failed" : failure);
}

void FileSink::LogFailureOnce(std::string const& message)
{
    if (failureLogged_ || !log_)
        return;
    failureLogged_ = true;
    log_("decision capture " + info_.captureId + ": " + message);
}

bool FileSink::StartSegment(std::uint32_t seg, std::uint64_t firstSeq, std::string& err)
{
    if (seg > 1)
    {
        EndWriter();
        // The segment just closed may go; the one about to open (0 bytes) is
        // never a candidate.
        ApplyCaptureRetention(segments_.size());
    }
    char name[32];
    std::snprintf(name, sizeof(name), "seg-%04u.jsonl", seg);
    Segment segment;
    segment.file = name;
    segment.firstSeq = firstSeq;
    segments_.push_back(segment);
    openSegment_ = segments_.size() - 1;

    // The writer's cap sits above the recorder's rotation point, so the queue
    // takes a whole record at the segment boundary.
    std::uint64_t const maxBytes = limits_.segmentBytes + limits_.recordBytes + 64ull * 1024ull;
    std::unique_ptr<RunTrace::RunTraceWriter> writer = std::make_unique<RunTrace::RunTraceWriter>();
    if (!writer->Open(SegmentPath(name), maxBytes, 256, 32ull * 1024ull * 1024ull, err))
    {
        segments_.pop_back();
        return false;
    }
    writer_ = std::move(writer);
    WriteIndex();
    if (seg > 1 && totalCapHook_)
        totalCapHook_();
    return true;
}

// Written here, on the world thread, not through the segment writers' queues:
// two writer threads of one capture would race on the same path and the older
// index could land last. It is a few hundred bytes at capture open, at each
// rotation and at the end, never per tick.
void FileSink::WriteIndex()
{
    std::string index = "{\"format\":\"tb-decision-capture\",\"v\":1,\"id\":";
    index += DecisionCapture::JsonString(info_.captureId);
    index += ",\"commander\":";
    index += DecisionCapture::JsonString(info_.commander);
    index += ",\"plan\":";
    index += DecisionCapture::JsonString(info_.plan);
    index += ",\"map\":";
    index += std::to_string(info_.map);
    index += ",\"started\":\"";
    index += UtcStamp(info_.startedUnixMs, true);
    index += "\",\"startedUnixMs\":";
    index += std::to_string(info_.startedUnixMs);
    index += ",\"ended\":";
    index += ended_ ? "\"" + UtcStamp(endedUnixMs_, true) + "\"" : "null";
    index += ",\"endReason\":";
    index += DecisionCapture::JsonString(endReason_);
    index += ",\"status\":";
    index += DecisionCapture::JsonString(status_);
    index += ",\"schema\":\"";
    index += info_.schema;
    index += "\",\"segments\":[";
    for (std::size_t i = 0; i < segments_.size(); ++i)
    {
        Segment const& segment = segments_[i];
        if (i)
            index += ',';
        index += "{\"file\":";
        index += DecisionCapture::JsonString(segment.file);
        index += ",\"firstSeq\":";
        index += std::to_string(segment.firstSeq);
        index += ",\"lastSeq\":";
        index += std::to_string(segment.lastSeq);
        index += ",\"records\":";
        index += std::to_string(segment.records);
        index += ",\"bytes\":";
        index += std::to_string(segment.bytes);
        index += ",\"deleted\":";
        index += segment.deleted ? "true" : "false";
        index += '}';
    }
    index += "],\"dropped\":";
    index += std::to_string(recorderDropped_ + writerDropped_);
    index += '}';
    std::string err;
    RunTrace::WriteFileAtomic(SegmentPath("index.json"), index, err);
}

std::uint64_t FileSink::Bytes() const
{
    std::uint64_t total = 0;
    for (Segment const& segment : segments_)
        if (!segment.deleted)
            total += segment.bytes;
    return total;
}

void FileSink::ApplyCaptureRetention(std::size_t keep)
{
    std::uint64_t total = Bytes();
    for (std::size_t i = 0; i < segments_.size() && total > limits_.captureBytes; ++i)
    {
        if (i == keep || segments_[i].deleted)
            continue;
        std::error_code ec;
        std::filesystem::remove(SegmentPath(segments_[i].file), ec);
        if (ec)
            continue;
        total -= segments_[i].bytes;
        segments_[i].deleted = true;
    }
}

void FileSink::EndWriter()
{
    if (!writer_)
        return;
    // No end line and no summary: the segment just stops. The capture's `end`
    // record went out through the recorder before this.
    Finishing finishing;
    finishing.segment = openSegment_;
    finishing.writer = std::move(writer_);
    finishing.writer->PushEnd(RunTrace::EndJob{});
    finishing_.push_back(std::move(finishing));
    writer_ = nullptr;
}

std::string FileSink::SegmentPath(std::string const& file) const
{
    return (std::filesystem::path(root_) / file).string();
}

std::string FileSink::MakeCaptureId(std::string const& dir, std::uint64_t nowUnixMs,
    std::string const& plan, std::string const& commander)
{
    std::string const base = UtcStamp(nowUnixMs, false) + "-" + Sanitise(plan, 48) + "-" + Sanitise(commander, 24);
    std::error_code ec;
    std::string id = base;
    for (unsigned attempt = 2; attempt < 1000 && std::filesystem::exists(std::filesystem::path(dir) / id, ec);
         ++attempt)
        id = base + "-" + std::to_string(attempt);
    return id;
}

} // namespace TortoiseBots
