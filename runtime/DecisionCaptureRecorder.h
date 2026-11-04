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
// keyframe. A refused header is retried before the next record: no record is
// written into a segment the sink has not taken a header for.
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

// How many records may pass between two keyframes written over the size cap.
// A full snapshot that never fits would otherwise be a stub at every keyframe
// and the segment could never be decoded from its own file.
constexpr std::uint64_t kOversizedKeyframeEvery = 60;

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
    std::uint64_t oversized = 0; // keyframes written over the size cap
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
        header_.settingsJson = CheckedSettings(std::move(header_.settingsJson));
    }

    void BeforeTick(const DungeonRun::Run& run, const DungeonRun::Facts& facts, std::uint32_t dt,
                    std::uint64_t nowUnixMs)
    {
        if (stopped_)
            return;
        ++tickSeq_;
        capUs_ = 0;
        auto clock = std::chrono::steady_clock::now();
        pendingFacts_ = EncodeFields(facts);
        AddEncodeMicros(clock);
        clock = std::chrono::steady_clock::now();
        pendingRun_ = EncodeFields(run);
        AddEncodeMicros(clock);
        pendingDt_ = dt;
        pendingNow_ = nowUnixMs;
        pendingState_ = run.state;
        pendingPlan_ = run.plan;
        pendingTactics_ = run.liveTactics;
        pending_ = true;

        std::uint64_t const nextSeq = tickSeq_;
        bool const planMoved = segmentOpen_ && PlanOrTacticsMoved();
        bool const overBytes = headerAccepted_ && limits_.segmentBytes > 0 && segmentBytes_ > limits_.segmentBytes;
        if (!segmentOpen_)
            OpenSegment(1, nextSeq);
        else if (headerAccepted_ && (planMoved || overBytes))
            OpenSegment(segment_ + 1, nextSeq);
        else if (headerPending_ && planMoved)
            RefreshHeader();
    }

    void AfterTick(const DungeonRun::Run& run, const std::string& order)
    {
        if (stopped_ || !pending_)
            return;
        pending_ = false;
        auto const clock = std::chrono::steady_clock::now();
        FieldMap const postRun = EncodeFields(run);
        AddEncodeMicros(clock);
        stats_.lastCapUs = capUs_;

        // A finished run ticks on; one `Done` record says so and the rest are
        // skipped. A new segment writes its own (its first record is a keyframe).
        bool const done = pendingState_ == DungeonRun::State::Done && run.state == DungeonRun::State::Done;
        if (done && doneWritten_)
        {
            KeepBase(postRun);
            return;
        }

        // No record lands in a segment whose header the sink has not taken.
        if (headerPending_ && !PushHeader())
        {
            ++stats_.dropped;
            ++gapDropped_;
            nextKey_ = true;
            KeepBase(postRun);
            return;
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
        line += ",\"capUs\":";
        line += std::to_string(stats_.lastCapUs);
        line += "}";

        bool const over = limits_.recordBytes > 0 && line.size() > limits_.recordBytes;
        bool const writeOversized = over && key &&
                                    (!haveOversized_ || tickIndex_ - lastOversizedAt_ >= kOversizedKeyframeEvery);
        if (over && !writeOversized)
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
        if (writeOversized)
        {
            ++stats_.oversized;
            lastOversizedAt_ = tickIndex_;
            haveOversized_ = true;
        }
        ++tickIndex_;
        ++stats_.records;
        if (key)
            ++stats_.keyframes;
        nextKey_ = false;
        if (done)
            doneWritten_ = true;
        KeepBase(postRun);
    }

    void End(std::uint64_t nowUnixMs, const std::string& reason)
    {
        if (stopped_)
            return;
        stopped_ = true;
        if (tickSeq_ == 0)
            return; // nothing was ever recorded: no `end` without a segment
        if (headerPending_ && !PushHeader())
            return; // no `end` into a segment without its header
        if (gapPending_)
            PushGap(tickSeq_ + 1);

        std::string line = "{\"k\":\"end\",\"t\":";
        line += std::to_string(nowUnixMs);
        line += ",\"reason\":";
        line += JsonString(reason);
        line += ",\"records\":";
        line += std::to_string(stats_.records);
        line += ",\"dropped\":";
        line += std::to_string(stats_.dropped);
        line += "}";
        PushLine(std::move(line));
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
    // The header's settings are a JSON object; anything else would corrupt
    // every line of the file, so a bad value degrades to `{}`.
    static std::string CheckedSettings(std::string text)
    {
        if (text.empty())
            return "{}";
        MiniJson::Value value;
        std::string error;
        if (!MiniJson::Parse(text, value, error) || value.kind != MiniJson::Value::Object)
            return "{}";
        return text;
    }

    // Only the encode work counts: the sink push and the line building are not
    // the capture's per-tick cost on the world thread's CPU budget.
    void AddEncodeMicros(std::chrono::steady_clock::time_point start)
    {
        auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
        if (us <= 0)
            return;
        std::uint64_t const total = static_cast<std::uint64_t>(capUs_) + static_cast<std::uint64_t>(us);
        capUs_ = total > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(total);
    }

    bool PlanOrTacticsMoved() const
    {
        std::uint32_t const map = pendingPlan_ ? pendingPlan_->map : 0;
        return CachedPlanHash(pendingPlan_) != headerPlanHash_ ||
               CachedTacticsHash(pendingTactics_, map) != headerTacticsHash_;
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

    bool PushLine(std::string line)
    {
        std::size_t const size = line.size();
        if (!sink_.Push(std::move(line)))
        {
            ++stats_.dropped;
            ++gapDropped_;
            gapPending_ = true;
            return false;
        }
        stats_.bytes += size;
        segmentBytes_ += size;
        return true;
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
        return PushLine(std::move(line));
    }

    bool PushGap(std::uint64_t seq)
    {
        std::string gap = "{\"k\":\"gap\",\"seq\":";
        gap += std::to_string(seq);
        gap += ",\"dropped\":";
        gap += std::to_string(gapDropped_);
        gap += "}";
        gapPending_ = false;
        std::uint64_t const pendingDrops = gapDropped_;
        gapDropped_ = 0; // the gap line's own refusal is not a record drop
        std::size_t const size = gap.size();
        if (!sink_.Push(std::move(gap)))
        {
            ++stats_.dropped;
            gapDropped_ = pendingDrops;
            gapPending_ = true;
            return false;
        }
        stats_.bytes += size;
        segmentBytes_ += size;
        ++stats_.gaps;
        return true;
    }

    bool PushHeader()
    {
        if (!headerPending_)
            return true;
        if (!PushLine(pendingHeader_))
            return false;
        pendingHeader_.clear();
        headerPending_ = false;
        headerAccepted_ = true;
        return true;
    }

    std::string BuildHeader(std::uint32_t seg) const
    {
        std::uint32_t const map = pendingPlan_ ? pendingPlan_->map : 0;
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
        // The whole plan, not run.plan's name+hash: the replay tool rebuilds the
        // policy inputs from this file alone, without the game's plan tables.
        line += pendingPlan_ ? EncodePlan(*pendingPlan_) : std::string("null");
        line += ",\"tactics\":";
        line += CachedTacticsText(pendingTactics_, map);
        line += "}";
        return line;
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
        segmentOpen_ = true;
        std::uint32_t const map = pendingPlan_ ? pendingPlan_->map : 0;
        headerPlanHash_ = CachedPlanHash(pendingPlan_);
        headerTacticsHash_ = CachedTacticsHash(pendingTactics_, map);
        doneWritten_ = false;   // a new segment writes its own Done tick
        haveOversized_ = false; // ... and its own keyframe, over the cap or not
        nextKey_ = true;
        pendingHeader_ = BuildHeader(seg);
        headerPending_ = true;
        headerAccepted_ = false;
        PushHeader();
    }

    // A header the sink refused carries the plan of the tick it was built for;
    // a reload before the retry rebuilds it rather than writing a stale one.
    void RefreshHeader()
    {
        std::uint32_t const map = pendingPlan_ ? pendingPlan_->map : 0;
        headerPlanHash_ = CachedPlanHash(pendingPlan_);
        headerTacticsHash_ = CachedTacticsHash(pendingTactics_, map);
        pendingHeader_ = BuildHeader(segment_);
    }

    HeaderInfo header_;
    Limits limits_;
    Sink& sink_;
    Stats stats_;

    std::uint32_t segment_ = 0;
    bool segmentOpen_ = false;
    std::uint64_t tickSeq_ = 0;   // ticks seen, written or not: the record's seq
    std::uint64_t tickIndex_ = 0; // records written, for the keyframe cadence
    std::uint64_t segmentBytes_ = 0;
    std::string headerPlanHash_;
    std::string headerTacticsHash_;
    std::string pendingHeader_;
    bool headerPending_ = false;
    bool headerAccepted_ = false;

    FieldMap baseFacts_;
    FieldMap baseRun_;
    bool haveBase_ = false;
    bool nextKey_ = true;
    bool doneWritten_ = false;
    bool gapPending_ = false;
    std::uint64_t gapDropped_ = 0;
    bool haveOversized_ = false;
    std::uint64_t lastOversizedAt_ = 0;

    FieldMap pendingFacts_;
    FieldMap pendingRun_;
    std::uint32_t pendingDt_ = 0;
    std::uint64_t pendingNow_ = 0;
    DungeonRun::State pendingState_ = DungeonRun::State::Running;
    const ai::DungeonPlan* pendingPlan_ = nullptr;
    const ai::BossTacticTable* pendingTactics_ = nullptr;
    bool pending_ = false;
    std::uint32_t capUs_ = 0;

    bool stopped_ = false;
};

} // namespace DecisionCapture
} // namespace TortoiseBots
