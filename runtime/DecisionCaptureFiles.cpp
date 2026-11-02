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

bool NumberValue(MiniJson::Value const* value, std::uint64_t& out)
{
    if (!value || value->kind != MiniJson::Value::Number || value->text.empty())
        return false;
    out = std::strtoull(value->text.c_str(), nullptr, 10);
    return true;
}

struct IndexSummary
{
    bool ok = false;
    bool ended = false;
    std::uint64_t startedUnixMs = 0;
    std::uint64_t bytes = 0; // of the segments that still exist
};

IndexSummary ReadIndex(std::string const& path)
{
    IndexSummary summary;
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
        return summary;
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MiniJson::Value root;
    std::string err;
    if (!MiniJson::Parse(text, root, err) || root.kind != MiniJson::Value::Object || !root.Find("id"))
        return summary;
    MiniJson::Value const* ended = root.Find("ended");
    summary.ended = !ended || ended->kind != MiniJson::Value::Null;
    if (MiniJson::Value const* started = root.Find("startedUnixMs"))
        NumberValue(started, summary.startedUnixMs);
    if (MiniJson::Value const* segments = root.Find("segments"))
        if (segments->kind == MiniJson::Value::Array)
            for (MiniJson::Value const& segment : segments->arr)
            {
                MiniJson::Value const* deleted = segment.Find("deleted");
                if (deleted && deleted->kind == MiniJson::Value::Bool && deleted->b)
                    continue;
                if (MiniJson::Value const* bytes = segment.Find("bytes"))
                {
                    std::uint64_t count = 0;
                    if (NumberValue(bytes, count))
                        summary.bytes += count;
                }
            }
    summary.ok = true;
    return summary;
}

// TotalMiB: the oldest ended captures go until the folder fits. A capture
// without an index (ours, just made) and a running capture are never touched.
void ApplyTotalRetention(std::string const& dir, std::string const& keepId, std::uint64_t limit)
{
    struct Ended
    {
        std::string path;
        std::uint64_t startedUnixMs = 0;
        std::uint64_t bytes = 0;
    };
    std::vector<Ended> ended;
    std::uint64_t total = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_directory())
            continue;
        std::string const id = it->path().filename().string();
        if (id == keepId)
            continue;
        IndexSummary const summary = ReadIndex((it->path() / "index.json").string());
        if (!summary.ok)
            continue;
        total += summary.bytes;
        if (summary.ended)
            ended.push_back({it->path().string(), summary.startedUnixMs, summary.bytes});
    }
    std::sort(ended.begin(), ended.end(),
        [](Ended const& a, Ended const& b) { return a.startedUnixMs < b.startedUnixMs; });
    for (Ended const& capture : ended)
    {
        if (total <= limit)
            break;
        std::error_code removeEc;
        std::filesystem::remove_all(capture.path, removeEc);
        if (removeEc)
            continue;
        total -= capture.bytes;
    }
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

FileSink::FileSink(std::string root, IndexInfo info, FileLimits limits)
    : root_(std::move(root)), info_(std::move(info)), limits_(limits)
{
}

FileSink::~FileSink()
{
    Detach();
}

std::unique_ptr<FileSink> FileSink::Open(std::string const& dir, std::string const& captureId,
    IndexInfo const& info, FileLimits const& limits, std::string& err)
{
    std::filesystem::path const root = std::filesystem::path(dir) / captureId;
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        err = "cannot create " + root.string() + ": " + ec.message();
        return nullptr;
    }
    ApplyTotalRetention(dir, captureId, limits.totalBytes);
    std::unique_ptr<FileSink> sink(new FileSink(root.string(), info, limits));
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
    {
        ++dropped_;
        return false;
    }
    Segment& segment = segments_[openSegment_];
    segment.bytes += size;
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
    return StartSegment(seg, firstSeq, err);
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
    dropped_ = std::max(dropped_, recorder.stats().dropped);
    endReason_ = reason;
    endedUnixMs_ = nowUnixMs;
    ended_ = true;
    WriteIndex();
    EndWriter();
}

void FileSink::Poll()
{
    for (auto it = finishing_.begin(); it != finishing_.end();)
    {
        if ((*it)->Finished())
        {
            (*it)->Reap();
            it = finishing_.erase(it);
        }
        else
            ++it;
    }
}

void FileSink::Detach()
{
    if (writer_)
    {
        writer_->Detach();
        writer_.reset();
    }
    for (std::unique_ptr<RunTrace::RunTraceWriter>& writer : finishing_)
        writer->Detach();
    finishing_.clear();
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
    index += std::to_string(dropped_);
    index += '}';
    std::string err;
    RunTrace::WriteFileAtomic(SegmentPath("index.json"), index, err);
}

void FileSink::ApplyCaptureRetention(std::size_t keep)
{
    std::uint64_t total = 0;
    for (Segment const& segment : segments_)
        if (!segment.deleted)
            total += segment.bytes;
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
    writer_->PushEnd(RunTrace::EndJob{});
    finishing_.push_back(std::move(writer_));
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
