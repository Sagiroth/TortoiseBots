#pragma once

// The decision capture's record builder: one JSON line per run-leader tick,
// with a header per segment, keyframes, deltas and the size caps. Header-only
// and standard library only; the file side (segments, retention, index.json)
// and the Commander hook live in runtime/DecisionCapture*.h/.cpp.
//
// Everything runs on the world thread: encode the two snapshots, build the
// line, hand it to the Sink. The Sink owns the disk (a RunTraceWriter queue and
// its own thread), so a slow disk never blocks a tick. A refused Push is a
// drop: counted, and a `gap` line goes out before the next record, which is a
// keyframe.
//
// Record grammar (kRecordVersion 1):
//   hdr  once per segment: schema, capture, plan, tactics, settings
//   tick seq from 1; `facts`/`run` are deltas against the previous record
//        (all fields on a keyframe), `post` against its own pre-tick Run
//   stub seq,t,bytes: a record over the size cap; the next tick is a keyframe
//   gap  seq,dropped: the sink refused lines; the next tick is a keyframe
//   end  t,reason,records,dropped

#include "DecisionCaptureCodec.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace TortoiseBots
{
namespace DecisionCapture
{

struct Limits
{
    std::size_t recordBytes = 512u * 1024u;
    std::uint32_t keyframeEvery = 60;
    std::uint64_t segmentBytes = 32ull * 1024ull * 1024ull;
};

struct HeaderInfo
{
    std::string captureId;
    std::string commander;
    std::string module;
    std::string settingsJson; // a JSON object, written verbatim in the header
    std::uint64_t startedUnixMs = 0;
};

class Sink
{
public:
    virtual ~Sink() = default;

    // false: the line was dropped (the recorder counts it and writes a gap).
    virtual bool Push(std::string line) = 0;

    // A new file is open; false: the capture stops and the owner logs it.
    virtual bool NewSegment(std::uint32_t seg, std::uint64_t firstSeq) = 0;
};

struct Stats
{
    std::uint64_t records = 0; // tick and stub lines written
    std::uint64_t keyframes = 0;
    std::uint64_t stubs = 0;
    std::uint64_t gaps = 0;
    std::uint64_t dropped = 0;
    std::uint64_t bytes = 0;   // accepted by the sink, headers included
    std::uint32_t segments = 0; // the segment in use
    std::uint32_t lastCapUs = 0; // encode time of the last tick
};

class Recorder
{
public:
    Recorder(HeaderInfo header, Limits limits, Sink& sink)
        : header_(std::move(header)), limits_(limits), sink_(sink)
    {
    }

    void BeforeTick(const DungeonRun::Run& run, const DungeonRun::Facts& facts, std::uint32_t dt,
                    std::uint64_t nowUnixMs)
    {
        if (stopped_)
            return;
        start_ = std::chrono::steady_clock::now();
        ++tickSeq_;
        pendingFacts_ = EncodeFields(facts);
        pendingRun_ = EncodeFields(run);
        pendingDt_ = dt;
        pendingNow_ = nowUnixMs;
        pendingState_ = run.state;
        pendingPlan_ = run.plan;
        pendingTactics_ = run.liveTactics;
        pending_ = true;

        std::uint64_t const nextSeq = tickSeq_;
        bool const planMoved = headerWritten_ && (run.plan != headerPlan_ || run.liveTactics != headerTactics_);
        bool const overBytes = headerWritten_ && limits_.segmentBytes > 0 && segmentBytes_ > limits_.segmentBytes;
        if (!headerWritten_)
            OpenSegment(1, nextSeq);
        else if (planMoved || overBytes)
            OpenSegment(segment_ + 1, nextSeq);
    }

    void AfterTick(const DungeonRun::Run& run, const std::string& order)
    {
        if (stopped_ || !pending_)
            return;
        pending_ = false;
        FieldMap const postRun = EncodeFields(run);

        // A finished run ticks on; one `Done` record says so and the rest are skipped.
        if (pendingState_ == DungeonRun::State::Done && run.state == DungeonRun::State::Done)
        {
            if (!doneWritten_)
            {
                doneWritten_ = true;
            }
            else
            {
                KeepBase(postRun);
                return;
            }
        }

        bool const key = nextKey_ || !haveBase_ ||
                         (limits_.keyframeEvery > 0 && tickIndex_ % limits_.keyframeEvery == 0);
        std::uint64_t const seq = tickSeq_;

        std::string line;
        line.reserve(16384);
        line += "{\"k\":\"tick\",\"seq\":";
        line += std::to_string(seq);
        line += ",\"t\":";
        line += std::to_string(pendingNow_);
        line += ",\"dt\":";
        line += std::to_string(pendingDt_);
        line += key ? ",\"key\":true,\"facts\":" : ",\"key\":false,\"facts\":";
        line += key ? FullObject(pendingFacts_) : DeltaObject(baseFacts_, pendingFacts_);
        line += ",\"run\":";
        line += key ? FullObject(pendingRun_) : DeltaObject(baseRun_, pendingRun_);
        line += ",\"out\":";
        line += OutObject(run, order);
        line += ",\"post\":";
        line += DeltaObject(pendingRun_, postRun);
        stats_.lastCapUs = CapMicros(start_);
        line += ",\"capUs\":";
        line += std::to_string(stats_.lastCapUs);
        line += "}";

        if (limits_.recordBytes > 0 && line.size() > limits_.recordBytes)
        {
            std::string stub = "{\"k\":\"stub\",\"seq\":";
            stub += std::to_string(seq);
            stub += ",\"t\":";
            stub += std::to_string(pendingNow_);
            stub += ",\"bytes\":";
            stub += std::to_string(line.size());
            stub += "}";
            if (PushRecord(std::move(stub), seq))
            {
                ++stats_.stubs;
                ++stats_.records;
                ++tickIndex_;
            }
            nextKey_ = true;
            KeepBase(postRun);
            return;
        }

        if (!PushRecord(std::move(line), seq))
        {
            nextKey_ = true;
            KeepBase(postRun);
            return;
        }
        ++tickIndex_;
        ++stats_.records;
        if (key)
            ++stats_.keyframes;
        nextKey_ = false;
        KeepBase(postRun);
    }

    void End(std::uint64_t nowUnixMs, const std::string& reason)
    {
        if (stopped_)
            return;
        std::string line = "{\"k\":\"end\",\"t\":";
        line += std::to_string(nowUnixMs);
        line += ",\"reason\":";
        line += JsonString(reason);
        line += ",\"records\":";
        line += std::to_string(stats_.records);
        line += ",\"dropped\":";
        line += std::to_string(stats_.dropped);
        line += "}";
        if (gapPending_)
            PushGap(tickSeq_);
        PushLine(line);
        stopped_ = true;
    }

    const Stats& stats() const
    {
        return stats_;
    }

    bool Stopped() const
    {
        return stopped_;
    }

private:
    static std::uint32_t CapMicros(std::chrono::steady_clock::time_point start)
    {
        auto const us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
                .count();
        if (us <= 0)
            return 0;
        if (us > 0xFFFFFFFFll)
            return 0xFFFFFFFFu;
        return static_cast<std::uint32_t>(us);
    }

    static std::string OutObject(const DungeonRun::Run& run, const std::string& order)
    {
        std::string out = "{\"order\":";
        out += JsonString(order);
        out += ",\"state\":";
        out += std::to_string(static_cast<int>(run.state));
        out += ",\"phase\":";
        out += std::to_string(static_cast<int>(run.phase));
        out += ",\"step\":";
        out += std::to_string(run.step);
        out += ",\"note\":";
        out += JsonString(run.note);
        out += ",\"say\":";
        out += JsonString(run.say);
        out += "}";
        return out;
    }

    void KeepBase(const FieldMap& postRun)
    {
        baseRun_ = postRun;
        baseFacts_ = pendingFacts_;
        haveBase_ = true;
    }

    bool PushLine(const std::string& line)
    {
        if (sink_.Push(line))
        {
            stats_.bytes += line.size();
            segmentBytes_ += line.size();
            return true;
        }
        ++stats_.dropped;
        ++gapDropped_;
        gapPending_ = true;
        return false;
    }

    // Push a tick or stub line, with the pending gap line first.
    bool PushRecord(std::string line, std::uint64_t seq)
    {
        if (gapPending_ && !PushGap(seq))
        {
            // The gap could not go out: drop this record too rather than let it
            // land without its gap. The gap stays pending for the next one.
            ++stats_.dropped;
            ++gapDropped_;
            return false;
        }
        return PushLine(line);
    }

    bool PushGap(std::uint64_t seq)
    {
        std::string gap = "{\"k\":\"gap\",\"seq\":";
        gap += std::to_string(seq);
        gap += ",\"dropped\":";
        gap += std::to_string(gapDropped_);
        gap += "}";
        gapPending_ = false;
        if (!PushLine(gap))
            return false;
        gapDropped_ = 0;
        ++stats_.gaps;
        return true;
    }

    void OpenSegment(std::uint32_t seg, std::uint64_t firstSeq)
    {
        if (seg > 1 && !sink_.NewSegment(seg, firstSeq))
        {
            stopped_ = true;
            return;
        }
        segment_ = seg;
        segmentBytes_ = 0;
        stats_.segments = seg;
        headerPlan_ = pendingPlan_;
        headerTactics_ = pendingTactics_;
        headerWritten_ = true;
        nextKey_ = true;

        std::string line = "{\"k\":\"hdr\",\"v\":";
        line += std::to_string(kRecordVersion);
        line += ",\"schema\":";
        line += JsonString(kDecisionSchemaHash);
        line += ",\"capture\":";
        line += JsonString(header_.captureId);
        line += ",\"seg\":";
        line += std::to_string(seg);
        line += ",\"module\":";
        line += JsonString(header_.module);
        line += ",\"commander\":";
        line += JsonString(header_.commander);
        line += ",\"startedUnixMs\":";
        line += std::to_string(header_.startedUnixMs);
        line += ",\"settings\":";
        line += header_.settingsJson.empty() ? "{}" : header_.settingsJson;
        line += ",\"plan\":";
        line += EncodePlanPointer(pendingPlan_);
        line += ",\"tactics\":";
        line += EncodeTactics(pendingTactics_, pendingPlan_ ? pendingPlan_->map : 0);
        line += "}";
        PushLine(line);
    }

    HeaderInfo header_;
    Limits limits_;
    Sink& sink_;
    Stats stats_;

    std::uint32_t segment_ = 0;
    std::uint64_t tickSeq_ = 0;  // ticks seen, written or not: the record's seq
    std::uint64_t tickIndex_ = 0; // records written, for the keyframe cadence
    std::uint64_t segmentBytes_ = 0;
    bool headerWritten_ = false;
    const ai::DungeonPlan* headerPlan_ = nullptr;
    const ai::BossTacticTable* headerTactics_ = nullptr;

    FieldMap baseFacts_;
    FieldMap baseRun_;
    bool haveBase_ = false;
    bool nextKey_ = true;
    bool doneWritten_ = false;
    bool gapPending_ = false;
    std::uint64_t gapDropped_ = 0;

    FieldMap pendingFacts_;
    FieldMap pendingRun_;
    std::uint32_t pendingDt_ = 0;
    std::uint64_t pendingNow_ = 0;
    DungeonRun::State pendingState_ = DungeonRun::State::Running;
    const ai::DungeonPlan* pendingPlan_ = nullptr;
    const ai::BossTacticTable* pendingTactics_ = nullptr;
    bool pending_ = false;
    std::chrono::steady_clock::time_point start_{};

    bool stopped_ = false;
};

} // namespace DecisionCapture
} // namespace TortoiseBots
