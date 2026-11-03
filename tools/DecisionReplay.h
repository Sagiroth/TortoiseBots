#pragma once

// The replay side of the decision capture: load a capture's segment files or
// an extracted fixture, rebuild each tick's Facts and Run, run the real
// DungeonRun::Tick and compare. Header-only and standard library only, so the
// tool and its test build without the game.
//
// A fixture is a window of ticks with the plan beside it (plans/<hash>.json,
// content-addressed) and, while its schema is old, the schema text it was
// captured with (schemas/<hash>.txt). `open` fixtures fail as XFAIL, `fixed`
// ones must pass; an `open` fixture that passes is XPASS and CI red.

#include "../runtime/DecisionCaptureCodec.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace DecisionReplay
{

using DecisionCapture::FieldMap;
using DecisionCapture::FullObject;
using DecisionCapture::DeltaObject;
using DecisionCapture::ApplyDelta;
using DecisionCapture::JsonString;
using DecisionCapture::kDecisionSchemaHash;
using DecisionCapture::kDecisionSchemaText;

// -- small JSON DOM helpers ---------------------------------------------

inline MiniJson::Value VObject()
{
    MiniJson::Value v;
    v.kind = MiniJson::Value::Object;
    return v;
}

inline MiniJson::Value VArray()
{
    MiniJson::Value v;
    v.kind = MiniJson::Value::Array;
    return v;
}

inline MiniJson::Value VString(std::string text)
{
    MiniJson::Value v;
    v.kind = MiniJson::Value::String;
    v.text = std::move(text);
    return v;
}

inline MiniJson::Value VNumber(std::uint64_t number)
{
    MiniJson::Value v;
    v.kind = MiniJson::Value::Number;
    v.text = std::to_string(number);
    return v;
}

inline MiniJson::Value VBool(bool value)
{
    MiniJson::Value v;
    v.kind = MiniJson::Value::Bool;
    v.b = value;
    return v;
}

inline MiniJson::Value VNull()
{
    return MiniJson::Value();
}

inline void Set(MiniJson::Value& object, const std::string& key, MiniJson::Value value)
{
    object.obj.emplace_back(key, std::move(value));
}

inline const MiniJson::Value* FindStr(const MiniJson::Value& object, const char* key)
{
    const MiniJson::Value* found = object.Find(key);
    return found && found->kind == MiniJson::Value::String ? found : nullptr;
}

inline const MiniJson::Value* FindNum(const MiniJson::Value& object, const char* key)
{
    const MiniJson::Value* found = object.Find(key);
    return found && found->kind == MiniJson::Value::Number ? found : nullptr;
}

inline std::uint64_t NumOr(const MiniJson::Value& object, const char* key, std::uint64_t fallback)
{
    const MiniJson::Value* found = FindNum(object, key);
    return found ? std::strtoull(found->text.c_str(), nullptr, 10) : fallback;
}

inline std::string StrOr(const MiniJson::Value& object, const char* key, const std::string& fallback)
{
    const MiniJson::Value* found = FindStr(object, key);
    return found ? found->text : fallback;
}

inline MiniJson::Value ParseText(std::string_view text)
{
    MiniJson::Value value;
    std::string error;
    MiniJson::Parse(text, value, error);
    return value;
}

inline FieldMap ToFieldMap(const MiniJson::Value& object)
{
    FieldMap out;
    if (object.kind == MiniJson::Value::Object)
        for (const auto& entry : object.obj)
            out.emplace_back(entry.first, MiniJson::Raw(entry.second));
    return out;
}

inline bool ReadWholeFile(const std::string& path, std::string& out, std::string& err)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        err = "cannot read " + path;
        return false;
    }
    std::ostringstream text;
    text << input.rdbuf();
    out = text.str();
    return true;
}

inline MiniJson::Value FromFields(const FieldMap& fields)
{
    return ParseText(FullObject(fields));
}

inline std::string Cut(std::string_view text, std::size_t max)
{
    if (text.size() <= max)
        return std::string(text);
    return std::string(text.substr(0, max)) + "...";
}

// -- capture records ----------------------------------------------------

struct TickOut
{
    std::string order, note, say;
    int state = 0, phase = 0;
    std::uint64_t step = 0;
};

struct TickRec
{
    std::uint64_t seq = 0, t = 0;
    std::uint32_t dt = 0;
    bool key = false;
    std::string kind; // "tick", or "undecodable" until the next keyframe
    FieldMap facts, preRun, postRun, glueDelta; // glueDelta is the record's "run" delta
    TickOut out;
};

struct Capture
{
    std::string schema, captureId;
    ai::DungeonPlan plan;
    ai::BossTacticTable tactics;
    bool tacticsSet = false;
    std::string planHash, tacticsHash; // set by the first header; later headers must match
    std::vector<TickRec> ticks;
    std::vector<std::string> notes;
};

// The seq of the last record loaded; a record may only follow it or skip ahead
// past a hole that a `gap` record announced.
struct SeqState
{
    bool have = false;
    std::uint64_t last = 0;
};

inline bool FollowSeq(SeqState& seqs, std::uint64_t seq, std::string& err)
{
    if (seqs.have && seq <= seqs.last)
    {
        err = "seq " + std::to_string(seq) + " repeats or goes back (after seq " + std::to_string(seqs.last) + ")";
        return false;
    }
    if (seqs.have && seq > seqs.last + 1)
    {
        err = "tick seq " + std::to_string(seqs.last + 1) + " is missing (seq " + std::to_string(seqs.last) +
              " then seq " + std::to_string(seq) + ", no gap record between)";
        return false;
    }
    seqs.have = true;
    seqs.last = seq;
    return true;
}

struct LoadState
{
    FieldMap facts, post;
    bool haveFacts = false, havePost = false;
    bool requireKey = false;
};

inline bool ParseOut(const MiniJson::Value& value, TickOut& out)
{
    if (value.kind != MiniJson::Value::Object)
        return false;
    out.order = StrOr(value, "order", "");
    out.note = StrOr(value, "note", "");
    out.say = StrOr(value, "say", "");
    out.state = static_cast<int>(NumOr(value, "state", 0));
    out.phase = static_cast<int>(NumOr(value, "phase", 0));
    out.step = NumOr(value, "step", 0);
    return true;
}

// One tick object of the record grammar (a capture line or a fixture tick).
inline bool ApplyTick(const MiniJson::Value& value, LoadState& state, TickRec& tick, std::string& err)
{
    if (value.kind != MiniJson::Value::Object)
    {
        err = "tick is not an object";
        return false;
    }
    tick = TickRec();
    tick.seq = NumOr(value, "seq", 0);
    tick.t = NumOr(value, "t", 0);
    tick.dt = static_cast<std::uint32_t>(NumOr(value, "dt", 0));
    const MiniJson::Value* key = value.Find("key");
    tick.key = key && key->kind == MiniJson::Value::Bool && key->b;
    const MiniJson::Value* out = value.Find("out");
    if (out)
        ParseOut(*out, tick.out);
    const MiniJson::Value* facts = value.Find("facts");
    const MiniJson::Value* run = value.Find("run");
    const MiniJson::Value* post = value.Find("post");
    if (!facts || !run || !post)
    {
        err = "tick " + std::to_string(tick.seq) + " has no facts, run or post";
        return false;
    }

    if (tick.key)
    {
        state.facts = ToFieldMap(*facts);
        state.post = ToFieldMap(*run);
        state.haveFacts = true;
        state.havePost = true;
        state.requireKey = false;
    }
    else if (state.requireKey || !state.haveFacts || !state.havePost)
    {
        tick.kind = "undecodable";
        err.clear();
        return true;
    }
    else
    {
        tick.glueDelta = ToFieldMap(*run);
        ApplyDelta(state.facts, *facts);
        FieldMap pre = state.post;
        ApplyDelta(pre, *run);
        FieldMap after = pre;
        ApplyDelta(after, *post);
        tick.facts = state.facts;
        tick.preRun = pre;
        tick.postRun = after;
        tick.kind = "tick";
        state.post = after;
        err.clear();
        return true;
    }

    // A keyframe: facts and run are full objects; post is still a delta.
    FieldMap pre = state.post;
    FieldMap after = pre;
    ApplyDelta(after, *post);
    tick.facts = state.facts;
    tick.preRun = pre;
    tick.postRun = after;
    tick.kind = "tick";
    state.post = after;
    err.clear();
    return true;
}

// One header line. Every header carries the whole plan and tactics, so each is
// decoded and hashed here. The first header fixes the capture's identity; a
// later header must carry the same capture id, plan and tactics, or it belongs
// to another capture.
inline bool ParseHeader(const MiniJson::Value& value, Capture& capture, bool& seenHeader, std::string& err)
{
    std::uint64_t const version = NumOr(value, "v", 0);
    if (version != 1)
    {
        err = "record version " + std::to_string(version) + " is not 1";
        return false;
    }
    std::string const schema = StrOr(value, "schema", "");
    if (schema.empty())
    {
        err = "header without a schema";
        return false;
    }
    bool const firstHeader = !seenHeader;
    if (!firstHeader && capture.schema != schema)
    {
        err = "schema changed inside one capture: " + capture.schema + " then " + schema;
        return false;
    }

    ai::DungeonPlan plan;
    std::string planHash;
    const MiniJson::Value* planValue = value.Find("plan");
    if (planValue && planValue->kind == MiniJson::Value::Object)
    {
        if (planValue->Find("body"))
        {
            std::string planError;
            if (!DecisionCapture::DecodePlan(*planValue, plan, planError))
            {
                err = "header plan: " + planError;
                return false;
            }
            planHash = DecisionCapture::PlanHash(plan);
        }
        else
            plan.name = StrOr(*planValue, "name", "");
    }

    // The first header decodes into the capture; a later one into a scratch table
    // (a table is not movable, so it is never copied over the capture's).
    ai::BossTacticTable scratch;
    ai::BossTacticTable& tactics = firstHeader ? capture.tactics : scratch;
    bool tacticsSet = false;
    std::string tacticsHash;
    const MiniJson::Value* tacticsValue = value.Find("tactics");
    if (tacticsValue)
    {
        std::string tacticsError;
        if (!DecisionCapture::DecodeTactics(*tacticsValue, tactics, tacticsSet, tacticsError))
        {
            err = "header tactics: " + tacticsError;
            return false;
        }
        tacticsHash = DecisionCapture::Hex64(
            DecisionCapture::Fnv1a64(DecisionCapture::EncodeTactics(tacticsSet ? &tactics : nullptr, plan.map)));
    }

    if (firstHeader)
    {
        seenHeader = true;
        capture.schema = schema;
        capture.captureId = StrOr(value, "capture", "");
        capture.plan = std::move(plan);
        capture.planHash = planHash;
        capture.tacticsSet = tacticsSet;
        capture.tacticsHash = tacticsHash;
        return true;
    }
    std::string const captureId = StrOr(value, "capture", "");
    if (captureId != capture.captureId)
    {
        err = "capture id " + captureId + " differs from the first segment's " + capture.captureId +
              "; the segment belongs to another capture";
        return false;
    }
    if (planHash != capture.planHash)
    {
        err = "plan differs from the first segment's plan (hash " + planHash + ", first segment " +
              capture.planHash + ")";
        return false;
    }
    if (tacticsHash != capture.tacticsHash)
    {
        err = "tactics differ from the first segment's tactics (hash " + tacticsHash + ", first segment " +
              capture.tacticsHash + ")";
        return false;
    }
    return true;
}

// Loads JSON lines of the record grammar. `source` names the file in errors.
inline bool LoadLines(const std::vector<std::pair<std::string, std::string>>& lines, Capture& out, std::string& err,
                      std::string const& source = std::string())
{
    LoadState state;
    SeqState seqs;
    bool seenHeader = false;
    bool haveSeg = false;
    std::uint64_t lastSeg = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        std::string const& line = lines[i].first;
        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;
        // Errors name the file the line came from (or the caller's source).
        std::string const where = source.empty() ? lines[i].second : source;
        std::string const prefix = where.empty() ? std::string() : where + ": ";
        MiniJson::Value value;
        std::string parseError;
        if (!MiniJson::Parse(line, value, parseError))
        {
            err = prefix + "line " + std::to_string(i + 1) + ": " + parseError;
            return false;
        }
        std::string const kind = StrOr(value, "k", "");
        if (kind == "hdr")
        {
            std::uint64_t const seg = NumOr(value, "seg", 0);
            if (haveSeg && seg != lastSeg + 1)
            {
                if (seg == lastSeg)
                    err = prefix + "segment " + std::to_string(seg) + " appears twice";
                else if (seg < lastSeg)
                    err = prefix + "segment " + std::to_string(seg) + " comes after segment " +
                          std::to_string(lastSeg);
                else
                    err = prefix + "segment " + std::to_string(lastSeg + 1) + " is missing (seg " +
                          std::to_string(lastSeg) + " then seg " + std::to_string(seg) + ")";
                return false;
            }
            if (!ParseHeader(value, out, seenHeader, err))
            {
                err = prefix + err;
                return false;
            }
            haveSeg = true;
            lastSeg = seg;
            state.requireKey = true;
            out.notes.push_back("hdr seg " + std::to_string(seg) + " schema " + out.schema);
        }
        else if (kind == "tick")
        {
            if (!FollowSeq(seqs, NumOr(value, "seq", 0), err))
            {
                err = prefix + err;
                return false;
            }
            TickRec tick;
            if (!ApplyTick(value, state, tick, err))
            {
                err = prefix + err;
                return false;
            }
            if (tick.kind == "undecodable")
                out.notes.push_back("tick " + std::to_string(tick.seq) + " undecodable (no keyframe yet)");
            out.ticks.push_back(std::move(tick));
        }
        else if (kind == "stub")
        {
            if (!FollowSeq(seqs, NumOr(value, "seq", 0), err))
            {
                err = prefix + err;
                return false;
            }
            state.requireKey = true;
            out.notes.push_back("stub seq " + std::to_string(NumOr(value, "seq", 0)) + " bytes " +
                                std::to_string(NumOr(value, "bytes", 0)));
        }
        else if (kind == "gap")
        {
            // The gap's seq is the next record's seq: the seqs before it were
            // dropped (and counted in `dropped`), so the hole is announced.
            std::uint64_t const seq = NumOr(value, "seq", 0);
            if (seq == 0 || (seqs.have && seq <= seqs.last))
            {
                err = prefix + "gap at seq " + std::to_string(seq) + " does not follow seq " +
                      std::to_string(seqs.last);
                return false;
            }
            seqs.have = true;
            seqs.last = seq - 1;
            state.requireKey = true;
            out.notes.push_back("gap seq " + std::to_string(seq) + " dropped " +
                                std::to_string(NumOr(value, "dropped", 0)));
        }
        else if (kind == "end")
        {
            out.notes.push_back("end reason " + StrOr(value, "reason", "") + " records " +
                                std::to_string(NumOr(value, "records", 0)) + " dropped " +
                                std::to_string(NumOr(value, "dropped", 0)));
        }
        else
        {
            err = prefix + "line " + std::to_string(i + 1) + ": unknown record kind \"" + kind + "\"";
            return false;
        }
    }
    if (!seenHeader)
    {
        err = source.empty() ? "no header line" : source + ": no header line";
        return false;
    }
    return true;
}

// Loads a capture's segment files. The order comes from each file's header
// (its `seg`), not from the path: the names are the caller's, the header is the
// capture's. Two files with the same segment number are an error.
inline bool LoadSegments(const std::vector<std::string>& files, Capture& out, std::string& err)
{
    struct Segment
    {
        std::uint64_t seg = 0;
        std::string path;
        std::vector<std::pair<std::string, std::string>> lines;
    };
    std::vector<Segment> segments;
    for (const std::string& file : files)
    {
        std::string text;
        std::string readError;
        if (!ReadWholeFile(file, text, readError))
        {
            err = readError;
            return false;
        }
        Segment segment;
        segment.path = file;
        std::string first;
        bool haveFirst = false;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string const line = text.substr(start, end - start);
            if (!haveFirst && line.find_first_not_of(" \t\r\n") != std::string::npos)
            {
                first = line;
                haveFirst = true;
            }
            segment.lines.emplace_back(line, file);
            if (end == text.size())
                break;
            start = end + 1;
        }
        if (!haveFirst)
        {
            err = file + ": no header line";
            return false;
        }
        MiniJson::Value value;
        std::string parseError;
        if (!MiniJson::Parse(first, value, parseError))
        {
            err = file + ": first line: " + parseError;
            return false;
        }
        if (StrOr(value, "k", "") != "hdr")
        {
            err = file + ": does not start with its header line";
            return false;
        }
        segment.seg = NumOr(value, "seg", 0);
        segments.push_back(std::move(segment));
    }

    std::stable_sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) {
        return a.seg != b.seg ? a.seg < b.seg : a.path < b.path;
    });
    for (std::size_t i = 1; i < segments.size(); ++i)
    {
        if (segments[i].seg == segments[i - 1].seg)
        {
            err = "segment " + std::to_string(segments[i].seg) + " is in both " + segments[i - 1].path + " and " +
                  segments[i].path;
            return false;
        }
    }

    std::vector<std::pair<std::string, std::string>> lines;
    for (const Segment& segment : segments)
        lines.insert(lines.end(), segment.lines.begin(), segment.lines.end());
    return LoadLines(lines, out, err);
}

// -- fixtures -----------------------------------------------------------

struct Expect
{
    std::uint64_t seq = 0;
    std::optional<std::string> order, orderPrefix, orderNot, orderContains, phase, noteContains;
    std::optional<std::uint64_t> step;
    std::vector<std::pair<std::string, std::string>> run;
    std::string why;
};

struct Fixture
{
    std::string name, schema, planHash, mode, reproduce, status;
    Capture cap;
    std::vector<Expect> expect;
    std::string path;      // the file it was loaded from
    std::string planPath;  // the plan file it uses
    MiniJson::Value root;  // the parsed file, kept for upgrade
};

struct TickResult
{
    std::uint64_t seq = 0;
    TickOut got;
    bool reproduced = false;
    std::vector<std::string> diffs;
    bool diverged = false;
    FieldMap post;
};

struct Report
{
    bool pass = false;
    bool xfail = false, xpass = false;
    std::vector<TickResult> ticks;
    std::vector<std::string> failures;
};

inline std::filesystem::path DirOf(const std::string& path)
{
    return std::filesystem::path(path).parent_path();
}

inline std::filesystem::path SchemaPathFor(const std::string& fixturePath, const std::string& hash)
{
    return DirOf(fixturePath) / "schemas" / (hash + ".txt");
}

inline bool LoadPlanFile(const Fixture& fixture, ai::DungeonPlan& plan, ai::BossTacticTable& tactics,
                         bool& tacticsSet, std::string& err)
{
    std::string text, readError;
    if (!ReadWholeFile(fixture.planPath, text, readError))
    {
        err = fixture.name + ": " + readError;
        return false;
    }
    MiniJson::Value root;
    std::string parseError;
    if (!MiniJson::Parse(text, root, parseError))
    {
        err = fixture.name + ": plan file " + fixture.planPath + ": " + parseError;
        return false;
    }
    const MiniJson::Value* planValue = root.Find("plan");
    if (!planValue || !DecisionCapture::DecodePlan(*planValue, plan, err))
    {
        err = fixture.name + ": plan file " + fixture.planPath + ": " + (err.empty() ? "no plan" : err);
        return false;
    }
    const MiniJson::Value* tacticsValue = root.Find("tactics");
    if (tacticsValue && !DecisionCapture::DecodeTactics(*tacticsValue, tactics, tacticsSet, err))
    {
        err = fixture.name + ": plan file " + fixture.planPath + ": tactics: " + err;
        return false;
    }
    std::string const hash = DecisionCapture::PlanHash(plan);
    if (hash != fixture.planHash)
    {
        err = fixture.name + ": plan file " + fixture.planPath + " hashes to " + hash + ", fixture planHash is " +
              fixture.planHash;
        return false;
    }
    return true;
}

inline bool ParseExpect(const MiniJson::Value& value, Expect& out, std::string& err)
{
    if (value.kind != MiniJson::Value::Object)
    {
        err = "expect entry is not an object";
        return false;
    }
    out = Expect();
    out.seq = NumOr(value, "seq", 0);
    if (!FindNum(value, "seq"))
    {
        err = "expect without seq";
        return false;
    }
    struct Entry
    {
        const char* name;
        std::optional<std::string>* field;
    };
    Entry const strings[] = {{"order", &out.order},
                             {"orderPrefix", &out.orderPrefix},
                             {"orderNot", &out.orderNot},
                             {"orderContains", &out.orderContains},
                             {"phase", &out.phase},
                             {"noteContains", &out.noteContains}};
    for (const Entry& entry : strings)
        if (const MiniJson::Value* found = value.Find(entry.name))
        {
            if (found->kind != MiniJson::Value::String)
            {
                err = std::string("expect ") + entry.name + " is not a string";
                return false;
            }
            *entry.field = found->text;
        }
    if (const MiniJson::Value* step = value.Find("step"))
    {
        if (step->kind != MiniJson::Value::Number)
        {
            err = "expect step is not a number";
            return false;
        }
        out.step = std::strtoull(step->text.c_str(), nullptr, 10);
    }
    if (const MiniJson::Value* run = value.Find("run"))
    {
        if (run->kind != MiniJson::Value::Object)
        {
            err = "expect run is not an object";
            return false;
        }
        for (const auto& field : run->obj)
            out.run.emplace_back(field.first, MiniJson::Raw(field.second));
    }
    out.why = StrOr(value, "why", "");
    return true;
}

inline bool LoadFixture(const std::string& path, Fixture& out, std::string& err)
{
    std::string text, readError;
    if (!ReadWholeFile(path, text, readError))
    {
        err = readError;
        return false;
    }
    MiniJson::Value root;
    std::string parseError;
    if (!MiniJson::Parse(text, root, parseError))
    {
        err = path + ": " + parseError;
        return false;
    }
    if (root.kind != MiniJson::Value::Object)
    {
        err = path + ": fixture is not an object";
        return false;
    }
    if (StrOr(root, "format", "") != "tb-decision-fixture")
    {
        err = path + ": format is not tb-decision-fixture";
        return false;
    }
    if (NumOr(root, "v", 0) != 1)
    {
        err = path + ": fixture version " + std::to_string(NumOr(root, "v", 0)) + " is not 1";
        return false;
    }

    // The caller passes a fresh Fixture: Capture's tactic table is not copyable.
    Fixture& fixture = out;
    fixture.path = path;
    fixture.root = root;
    fixture.name = StrOr(root, "name", std::filesystem::path(path).stem().string());
    fixture.schema = StrOr(root, "schema", "");
    fixture.planHash = StrOr(root, "planHash", "");
    fixture.mode = StrOr(root, "mode", "independent");
    fixture.reproduce = StrOr(root, "reproduce", "before-first-mark");
    fixture.status = StrOr(root, "status", "fixed");
    if (fixture.mode != "independent" && fixture.mode != "chain")
    {
        err = fixture.name + ": unknown replay mode \"" + fixture.mode + "\"";
        return false;
    }
    if (fixture.reproduce != "all" && fixture.reproduce != "before-first-mark" &&
        fixture.reproduce != "none")
    {
        err = fixture.name + ": unknown reproduce mode \"" + fixture.reproduce + "\"";
        return false;
    }

    std::string planRelative = StrOr(root, "plan", "plans/" + fixture.planHash + ".json");
    std::filesystem::path planPath(planRelative);
    fixture.planPath = planPath.is_absolute() ? planPath.lexically_normal().string()
                                              : (DirOf(path) / planPath).lexically_normal().string();
    std::string planError;
    if (!LoadPlanFile(fixture, fixture.cap.plan, fixture.cap.tactics, fixture.cap.tacticsSet, planError))
    {
        // A plan struct that changed under an old fixture is upgrade's job; the
        // schema mismatch is reported before any tick is decoded.
        if (fixture.schema == kDecisionSchemaHash)
        {
            err = planError;
            return false;
        }
        fixture.cap.notes.push_back("plan file not decoded under the old schema: " + planError);
    }

    fixture.cap.schema = fixture.schema;
    const MiniJson::Value* source = root.Find("source");
    fixture.cap.captureId = source ? StrOr(*source, "capture", "") : "";
    if (fixture.cap.captureId.empty())
        fixture.cap.captureId = fixture.name;

    LoadState state;
    const MiniJson::Value* ticks = root.Find("ticks");
    if (!ticks || ticks->kind != MiniJson::Value::Array)
    {
        err = fixture.name + ": no ticks array";
        return false;
    }
    for (const MiniJson::Value& value : ticks->arr)
    {
        TickRec tick;
        std::string tickError;
        if (!ApplyTick(value, state, tick, tickError))
        {
            err = fixture.name + ": " + tickError;
            return false;
        }
        if (tick.kind == "undecodable")
            fixture.cap.notes.push_back("tick " + std::to_string(tick.seq) + " undecodable (no keyframe yet)");
        fixture.cap.ticks.push_back(std::move(tick));
    }

    const MiniJson::Value* expect = root.Find("expect");
    if (expect)
    {
        if (expect->kind != MiniJson::Value::Array)
        {
            err = fixture.name + ": expect is not an array";
            return false;
        }
        for (const MiniJson::Value& value : expect->arr)
        {
            Expect parsed;
            std::string expectError;
            if (!ParseExpect(value, parsed, expectError))
            {
                err = fixture.name + ": " + expectError;
                return false;
            }
            fixture.expect.push_back(std::move(parsed));
        }
    }

    return true;
}

// -- schema text and its diff -------------------------------------------

struct SchemaModel
{
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> structs;
    std::map<std::string, std::vector<std::string>> enums;
};

inline std::string Trim(std::string_view text)
{
    std::size_t begin = 0, end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
        ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
        --end;
    return std::string(text.substr(begin, end - begin));
}

inline SchemaModel ParseSchema(std::string_view text)
{
    SchemaModel model;
    std::string current;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string const line = Trim(text.substr(start, end - start));
        if (!line.empty())
        {
            if (line.compare(0, 7, "struct ") == 0)
            {
                current = Trim(line.substr(7));
                model.structs[current];
            }
            else if (line.compare(0, 5, "enum ") == 0)
            {
                current.clear();
                std::string const body = Trim(line.substr(5));
                std::size_t colon = body.find(':');
                std::string const name = Trim(body.substr(0, colon));
                std::vector<std::string> values;
                if (colon != std::string::npos)
                {
                    std::string const list = body.substr(colon + 1);
                    std::size_t item = 0;
                    while (item <= list.size())
                    {
                        std::size_t comma = list.find(',', item);
                        if (comma == std::string::npos)
                            comma = list.size();
                        std::string const value = Trim(list.substr(item, comma - item));
                        if (!value.empty())
                            values.push_back(value);
                        if (comma == list.size())
                            break;
                        item = comma + 1;
                    }
                }
                model.enums[name] = values;
            }
            else if (!current.empty())
            {
                std::size_t colon = line.find(':');
                if (colon != std::string::npos)
                    model.structs[current].emplace_back(Trim(line.substr(0, colon)), Trim(line.substr(colon + 1)));
            }
        }
        if (end == text.size())
            break;
        start = end + 1;
    }
    return model;
}

inline const std::string* FieldType(const SchemaModel& model, const std::string& name, const std::string& field)
{
    auto it = model.structs.find(name);
    if (it == model.structs.end())
        return nullptr;
    for (const auto& entry : it->second)
        if (entry.first == field)
            return &entry.second;
    return nullptr;
}

inline std::string SchemaTextDiff(const SchemaModel& oldModel, const SchemaModel& newModel)
{
    std::vector<std::string> lines;
    std::set<std::string> names;
    for (const auto& entry : oldModel.structs)
        names.insert(entry.first);
    for (const auto& entry : newModel.structs)
        names.insert(entry.first);
    for (const std::string& name : names)
    {
        auto oldIt = oldModel.structs.find(name);
        auto newIt = newModel.structs.find(name);
        if (oldIt == oldModel.structs.end())
        {
            lines.push_back("+struct " + name);
            continue;
        }
        if (newIt == newModel.structs.end())
        {
            lines.push_back("-struct " + name);
            continue;
        }
        for (const auto& field : newIt->second)
        {
            const std::string* old = FieldType(oldModel, name, field.first);
            if (!old)
                lines.push_back("+" + name + "." + field.first + ": " + field.second);
            else if (*old != field.second)
                lines.push_back("~" + name + "." + field.first + ": " + *old + " -> " + field.second);
        }
        for (const auto& field : oldIt->second)
            if (!FieldType(newModel, name, field.first))
                lines.push_back("-" + name + "." + field.first);
    }
    std::set<std::string> enums;
    for (const auto& entry : oldModel.enums)
        enums.insert(entry.first);
    for (const auto& entry : newModel.enums)
        enums.insert(entry.first);
    for (const std::string& name : enums)
    {
        auto oldIt = oldModel.enums.find(name);
        auto newIt = newModel.enums.find(name);
        if (oldIt == oldModel.enums.end())
        {
            lines.push_back("enum " + name + " added");
            continue;
        }
        if (newIt == newModel.enums.end())
        {
            lines.push_back("enum " + name + " removed");
            continue;
        }
        if (oldIt->second == newIt->second)
            continue;
        bool appended = newIt->second.size() > oldIt->second.size() &&
                        std::equal(oldIt->second.begin(), oldIt->second.end(), newIt->second.begin());
        if (appended)
            lines.push_back("enum " + name + ": appended");
        else
            lines.push_back("enum " + name + " changed");
    }
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (i)
            joined += "; ";
        joined += lines[i];
    }
    return joined;
}

// -- replay -------------------------------------------------------------

inline bool DecodeTickFacts(const TickRec& tick, DungeonRun::Facts& facts, std::string& err)
{
    MiniJson::Value value = FromFields(tick.facts);
    return DecisionCapture::DecodeFields(value, facts, err);
}

inline bool DecodeTickRun(const TickRec& tick, const Capture& capture, DungeonRun::Run& run, std::string& err)
{
    MiniJson::Value value = FromFields(tick.preRun);
    return DecisionCapture::DecodeFields(value, run, &capture.plan,
                                         capture.tacticsSet ? &capture.tactics : nullptr, err);
}

inline TickOut OutOf(const DungeonRun::Run& run, const std::string& order)
{
    TickOut out;
    out.order = order;
    out.note = run.note;
    out.say = run.say;
    out.state = static_cast<int>(run.state);
    out.phase = static_cast<int>(run.phase);
    out.step = run.step;
    return out;
}

inline std::vector<std::string> FieldDiffs(const FieldMap& got, const FieldMap& want)
{
    auto find = [](const FieldMap& fields, const std::string& name) -> const std::string*
    {
        for (const auto& field : fields)
            if (field.first == name)
                return &field.second;
        return nullptr;
    };
    std::vector<std::string> diffs;
    for (const auto& field : want)
    {
        const std::string* have = find(got, field.first);
        if (!have)
            diffs.push_back(field.first + ": got (missing), want " + Cut(field.second, 200));
        else if (*have != field.second)
            diffs.push_back(field.first + ": got " + Cut(*have, 200) + ", want " + Cut(field.second, 200));
    }
    for (const auto& field : got)
        if (!find(want, field.first))
            diffs.push_back(field.first + ": got " + Cut(field.second, 200) + ", want (missing)");
    return diffs;
}

inline std::string PhaseText(int phase)
{
    return DungeonRun::PhaseName(static_cast<DungeonRun::Phase>(phase));
}

inline bool EvaluateExpect(const Expect& expect, const TickOut& got, const FieldMap& post, std::string& why)
{
    if (expect.order && got.order != *expect.order)
    {
        why = "order is \"" + Cut(got.order, 120) + "\", expected \"" + Cut(*expect.order, 120) + "\"";
        return false;
    }
    if (expect.orderPrefix && got.order.compare(0, expect.orderPrefix->size(), *expect.orderPrefix) != 0)
    {
        why = "order does not start with \"" + *expect.orderPrefix + "\"";
        return false;
    }
    if (expect.orderNot && got.order == *expect.orderNot)
    {
        why = "order is still \"" + Cut(got.order, 120) + "\"";
        return false;
    }
    if (expect.orderContains && got.order.find(*expect.orderContains) == std::string::npos)
    {
        why = "order does not contain \"" + *expect.orderContains + "\"";
        return false;
    }
    if (expect.phase && PhaseText(got.phase) != *expect.phase)
    {
        why = "phase is " + PhaseText(got.phase) + ", expected " + *expect.phase;
        return false;
    }
    if (expect.noteContains && got.note.find(*expect.noteContains) == std::string::npos)
    {
        why = "note does not contain \"" + *expect.noteContains + "\"";
        return false;
    }
    if (expect.step && got.step != *expect.step)
    {
        why = "step is " + std::to_string(got.step) + ", expected " + std::to_string(*expect.step);
        return false;
    }
    for (const auto& field : expect.run)
    {
        std::string const* raw = nullptr;
        for (const auto& entry : post)
            if (entry.first == field.first)
            {
                raw = &entry.second;
                break;
            }
        if (!raw)
        {
            why = "no run field \"" + field.first + "\"";
            return false;
        }
        if (*raw != field.second)
        {
            why = "run." + field.first + " is " + Cut(*raw, 200) + ", expected " + Cut(field.second, 200);
            return false;
        }
    }
    return true;
}

struct Options
{
    bool chainApplyGlue = true; // the test-only switch behind section 6.4 test 3
};

inline Report Replay(const Fixture& fixture, const Options& options = Options())
{
    Report report;
    if (fixture.schema != std::string(kDecisionSchemaHash))
    {
        std::string oldText, readError;
        if (!ReadWholeFile(SchemaPathFor(fixture.path, fixture.schema).string(), oldText, readError))
        {
            report.failures.push_back(fixture.name + ": schema " + fixture.schema +
                                      " is not known: schemas/" + fixture.schema + ".txt is missing");
            report.pass = false;
            return report;
        }
        SchemaModel const oldModel = ParseSchema(oldText);
        SchemaModel const newModel = ParseSchema(kDecisionSchemaText);
        std::string const diff = SchemaTextDiff(oldModel, newModel);
        report.failures.push_back(fixture.name + ": captured with schema " + fixture.schema + ", this build is " +
                                  kDecisionSchemaHash + ": " + diff + "; run: replay_decisions upgrade " +
                                  fixture.path);
        report.pass = false;
        return report;
    }

    std::uint64_t firstExpect = std::numeric_limits<std::uint64_t>::max();
    for (const Expect& expect : fixture.expect)
        firstExpect = std::min(firstExpect, expect.seq);
    bool const hasExpect = !fixture.expect.empty();

    DungeonRun::Run carried;
    bool carriedReady = false;
    bool diverged = false;

    for (const TickRec& tick : fixture.cap.ticks)
    {
        TickResult result;
        result.seq = tick.seq;
        if (tick.kind != "tick")
        {
            result.diffs.push_back("undecodable record");
            bool const checked = hasExpect ? tick.seq < firstExpect : true;
            bool const mustReproduce = fixture.reproduce == "all" ? true :
                                       fixture.reproduce == "before-first-mark" ? checked : false;
            if (mustReproduce && !(fixture.mode == "chain" && diverged))
                report.failures.push_back(fixture.name + ": seq " + std::to_string(tick.seq) +
                                          " is undecodable: no keyframe in reach");
            for (const Expect& expect : fixture.expect)
                if (expect.seq == tick.seq)
                    report.failures.push_back(fixture.name + ": seq " + std::to_string(tick.seq) +
                                              ": expectation failed: the tick is undecodable");
            report.ticks.push_back(result);
            continue;
        }

        DungeonRun::Facts facts;
        DungeonRun::Run run;
        std::string err;
        std::string order;
        if (fixture.mode == "chain" && carriedReady && !tick.key)
        {
            run = carried;
            if (options.chainApplyGlue && !tick.glueDelta.empty())
            {
                FieldMap fields = DecisionCapture::EncodeFields(run);
                ApplyDelta(fields, FromFields(tick.glueDelta));
                if (!DecisionCapture::DecodeFields(FromFields(fields), run, &fixture.cap.plan,
                                                   fixture.cap.tacticsSet ? &fixture.cap.tactics : nullptr, err))
                {
                    report.failures.push_back(fixture.name + ": seq " + std::to_string(tick.seq) +
                                              ": applying the glue delta: " + err);
                    report.pass = false;
                    return report;
                }
            }
            if (!DecodeTickFacts(tick, facts, err))
            {
                report.failures.push_back(fixture.name + ": seq " + std::to_string(tick.seq) + ": facts: " + err);
                report.pass = false;
                return report;
            }
            order = DungeonRun::Tick(run, facts, tick.dt);
            carried = run;
        }
        else
        {
            if (!DecodeTickFacts(tick, facts, err) || !DecodeTickRun(tick, fixture.cap, run, err))
            {
                report.failures.push_back(fixture.name + ": seq " + std::to_string(tick.seq) + ": " + err);
                report.pass = false;
                return report;
            }
            order = DungeonRun::Tick(run, facts, tick.dt);
            carried = run;
            carriedReady = true;
        }

        result.got = OutOf(run, order);
        result.post = DecisionCapture::EncodeFields(run);
        bool const orderSame = result.got.order == tick.out.order;
        result.diffs = FieldDiffs(result.post, tick.postRun);
        result.reproduced = orderSame && result.diffs.empty();
        if (!orderSame)
            result.diffs.insert(result.diffs.begin(), "order: got \"" + Cut(result.got.order, 120) + "\", want \"" +
                                                         Cut(tick.out.order, 120) + "\"");

        if (fixture.mode == "chain")
        {
            if (!result.reproduced)
                diverged = true;
            result.diverged = diverged;
        }

        bool const checked = hasExpect ? tick.seq < firstExpect : true;
        bool const mustReproduce = fixture.reproduce == "all" ? true :
                                   fixture.reproduce == "before-first-mark" ? checked : false;
        if (mustReproduce && !result.reproduced && !(fixture.mode == "chain" && result.diverged))
        {
            std::string failure = fixture.name + ": seq " + std::to_string(tick.seq) + " did not reproduce";
            for (std::size_t i = 0; i < result.diffs.size() && i < 6; ++i)
                failure += (i ? "; " : ": ") + result.diffs[i];
            report.failures.push_back(failure);
        }

        for (const Expect& expect : fixture.expect)
        {
            if (expect.seq != tick.seq)
                continue;
            std::string why;
            if (!EvaluateExpect(expect, result.got, result.post, why))
                report.failures.push_back(fixture.name + ": seq " + std::to_string(tick.seq) +
                                          ": expectation failed: " + why +
                                          (expect.why.empty() ? "" : " (" + expect.why + ")"));
        }
        report.ticks.push_back(std::move(result));
    }

    report.pass = report.failures.empty();
    if (fixture.status == "open")
    {
        if (report.pass)
        {
            report.xpass = true;
            report.pass = false;
            report.failures.push_back(fixture.name + ": XPASS: flip status to fixed");
        }
        else
        {
            report.xfail = true;
            report.pass = true;
        }
    }
    else if (fixture.status != "fixed")
    {
        report.pass = false;
        report.failures.push_back(fixture.name + ": unknown status \"" + fixture.status + "\"");
    }
    return report;
}

// -- why ----------------------------------------------------------------

inline bool Why(const Fixture& fixture, std::uint64_t seq, std::vector<std::string>& fields, std::string& err)
{
    const TickRec* tick = nullptr;
    for (const TickRec& candidate : fixture.cap.ticks)
        if (candidate.seq == seq)
            tick = &candidate;
    if (!tick || tick->kind != "tick")
    {
        err = fixture.name + ": no decoded tick with seq " + std::to_string(seq);
        return false;
    }

    DungeonRun::Facts facts;
    DungeonRun::Run run;
    if (!DecodeTickFacts(*tick, facts, err) || !DecodeTickRun(*tick, fixture.cap, run, err))
        return false;
    std::string const order = DungeonRun::Tick(run, facts, tick->dt);
    TickOut const reference = OutOf(run, order);

    FieldMap const defaults = DecisionCapture::EncodeFields(DungeonRun::Facts{});
    fields.clear();
    for (const auto& field : tick->facts)
    {
        const std::string* defaultText = nullptr;
        for (const auto& entry : defaults)
            if (entry.first == field.first)
            {
                defaultText = &entry.second;
                break;
            }
        if (!defaultText)
            continue;

        FieldMap trial = tick->facts;
        MiniJson::Value delta = VObject();
        Set(delta, field.first, ParseText(*defaultText));
        ApplyDelta(trial, delta);

        DungeonRun::Facts trialFacts;
        DungeonRun::Run trialRun;
        std::string trialError;
        MiniJson::Value trialValue = FromFields(trial);
        if (!DecisionCapture::DecodeFields(trialValue, trialFacts, trialError))
        {
            err = "seq " + std::to_string(seq) + " " + field.first + ": " + trialError;
            return false;
        }
        if (!DecodeTickRun(*tick, fixture.cap, trialRun, trialError))
            return false;
        std::string const trialOrder = DungeonRun::Tick(trialRun, trialFacts, tick->dt);
        TickOut const trialOut = OutOf(trialRun, trialOrder);
        if (trialOut.order != reference.order || trialOut.phase != reference.phase || trialOut.step != reference.step)
            fields.push_back(field.first);
    }
    return true;
}

// -- extract ------------------------------------------------------------

struct ExtractOptions
{
    std::uint64_t fromSeq = 0, toSeq = 0;
    std::string name;
    std::string mode = "independent";
    std::string status = "open";
    std::string outDir;
    std::string segment;
    bool overwrite = false; // replace an existing fixture of the same name
};

// A fixture name is one filename component: letters, digits, '.', '_' or '-',
// no leading dot. So the fixture stays directly under its out dir.
inline bool SafeFixtureName(const std::string& name)
{
    if (name.empty() || name[0] == '.')
        return false;
    for (char c : name)
    {
        bool const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok)
            return false;
    }
    return true;
}

inline MiniJson::Value TickObject(const TickRec& tick, bool first, const FieldMap& previousFacts,
                                  const FieldMap& previousPost)
{
    MiniJson::Value object = VObject();
    Set(object, "k", VString("tick"));
    Set(object, "seq", VNumber(tick.seq));
    Set(object, "t", VNumber(tick.t));
    Set(object, "dt", VNumber(tick.dt));
    Set(object, "key", VBool(first));
    Set(object, "facts", ParseText(first ? FullObject(tick.facts) : DeltaObject(previousFacts, tick.facts)));
    Set(object, "run", ParseText(first ? FullObject(tick.preRun) : DeltaObject(previousPost, tick.preRun)));
    MiniJson::Value out = VObject();
    Set(out, "order", VString(tick.out.order));
    Set(out, "state", VNumber(static_cast<std::uint64_t>(tick.out.state)));
    Set(out, "phase", VNumber(static_cast<std::uint64_t>(tick.out.phase)));
    Set(out, "step", VNumber(tick.out.step));
    Set(out, "note", VString(tick.out.note));
    Set(out, "say", VString(tick.out.say));
    Set(object, "out", std::move(out));
    Set(object, "post", ParseText(DeltaObject(tick.preRun, tick.postRun)));
    return object;
}

inline bool WriteIfMissing(const std::string& path, const std::string& text, std::size_t maxBytes, std::string& err)
{
    if (text.size() > maxBytes)
    {
        err = path + " would be " + std::to_string(text.size()) + " bytes (cap " + std::to_string(maxBytes) + ")";
        return false;
    }
    if (std::filesystem::exists(path))
        return true;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        err = "cannot write " + path;
        return false;
    }
    out << text;
    return true;
}

inline bool Extract(const Capture& capture, const ExtractOptions& options, std::string& fixturePath,
                    std::string& err)
{
    if (capture.plan.name.empty())
    {
        err = "the capture has no plan body in its headers; it cannot become a fixture";
        return false;
    }
    if (options.name.empty())
    {
        err = "a fixture needs a name";
        return false;
    }
    if (!SafeFixtureName(options.name))
    {
        err = "fixture name \"" + options.name +
              "\" must be one file name: letters, digits, '.', '_' or '-', no leading dot";
        return false;
    }
    std::filesystem::path const dir(options.outDir);
    fixturePath = (dir / (options.name + ".json")).string();
    if (!options.overwrite && std::filesystem::exists(fixturePath))
    {
        err = fixturePath + " already exists; pass --overwrite to replace it";
        return false;
    }
    if (options.fromSeq == 0 || options.toSeq < options.fromSeq)
    {
        err = "the seq window is empty";
        return false;
    }
    if (options.mode != "independent" && options.mode != "chain")
    {
        err = "unknown replay mode \"" + options.mode + "\"";
        return false;
    }

    std::string const planHash = DecisionCapture::PlanHash(capture.plan);
    MiniJson::Value ticks = VArray();
    FieldMap previousFacts, previousPost;
    bool first = true;
    std::size_t count = 0;
    for (const TickRec& tick : capture.ticks)
    {
        if (tick.seq < options.fromSeq || tick.seq > options.toSeq)
            continue;
        if (tick.kind != "tick")
        {
            err = "tick " + std::to_string(tick.seq) +
                  " is undecodable (its segment has no keyframe in reach); extract after the next keyframe";
            return false;
        }
        ticks.arr.push_back(TickObject(tick, first, previousFacts, previousPost));
        previousFacts = tick.facts;
        previousPost = tick.postRun;
        first = false;
        if (++count > 300)
        {
            err = "the window has more than 300 ticks";
            return false;
        }
    }
    if (count == 0)
    {
        err = "no tick in the window";
        return false;
    }

    std::string const planFile = "plans/" + planHash + ".json";
    MiniJson::Value root = VObject();
    Set(root, "format", VString("tb-decision-fixture"));
    Set(root, "v", VNumber(1));
    Set(root, "name", VString(options.name));
    Set(root, "schema", VString(kDecisionSchemaHash));
    Set(root, "planHash", VString(planHash));
    Set(root, "plan", VString(planFile));
    Set(root, "mode", VString(options.mode));
    Set(root, "reproduce", VString("before-first-mark"));
    Set(root, "status", VString(options.status));
    MiniJson::Value source = VObject();
    Set(source, "capture", VString(capture.captureId));
    Set(source, "segment", VString(options.segment));
    Set(source, "fromSeq", VNumber(options.fromSeq));
    Set(source, "toSeq", VNumber(options.toSeq));
    Set(source, "walk", VString(""));
    Set(source, "issue", VString(""));
    Set(root, "source", std::move(source));
    Set(root, "expect", VArray());
    Set(root, "ticks", std::move(ticks));
    Set(root, "history", VArray());

    std::string const text = MiniJson::Raw(root);
    if (text.size() > 1024u * 1024u)
    {
        err = "the fixture would be " + std::to_string(text.size()) + " bytes (cap 1 MiB)";
        return false;
    }

    std::string const planPath = (dir / planFile).string();
    MiniJson::Value planRoot = VObject();
    Set(planRoot, "plan", ParseText(DecisionCapture::EncodePlan(capture.plan)));
    Set(planRoot, "tactics", ParseText(DecisionCapture::EncodeTactics(
                                     capture.tacticsSet ? &capture.tactics : nullptr, capture.plan.map)));
    std::string planError;
    if (!WriteIfMissing(planPath, MiniJson::Raw(planRoot), 4u * 1024u * 1024u, planError))
    {
        err = planError;
        return false;
    }
    std::string const schemaPath = (dir / "schemas" / (std::string(kDecisionSchemaHash) + ".txt")).string();
    if (!WriteIfMissing(schemaPath, kDecisionSchemaText, 4u * 1024u * 1024u, planError))
    {
        err = planError;
        return false;
    }

    std::filesystem::create_directories(dir);
    std::ofstream output(fixturePath, std::ios::binary);
    if (!output)
    {
        err = "cannot write " + fixturePath;
        return false;
    }
    output << text;
    return true;
}

// -- upgrade ------------------------------------------------------------

struct TypeRef
{
    std::string name;
    std::vector<TypeRef> args;
};

inline bool ParseTypeRef(std::string_view text, TypeRef& out)
{
    std::string const line = Trim(text);
    std::size_t open = line.find('<');
    if (open == std::string::npos)
    {
        if (line.empty())
            return false;
        out.name = line;
        return true;
    }
    out.name = Trim(line.substr(0, open));
    if (line.back() != '>')
        return false;
    std::string const inside = line.substr(open + 1, line.size() - open - 2);
    std::size_t start = 0;
    int depth = 0;
    while (start <= inside.size())
    {
        std::size_t comma = start;
        for (; comma < inside.size(); ++comma)
        {
            if (inside[comma] == '<')
                ++depth;
            else if (inside[comma] == '>')
                --depth;
            else if (inside[comma] == ',' && depth == 0)
                break;
        }
        TypeRef arg;
        if (!ParseTypeRef(std::string_view(inside).substr(start, comma - start), arg))
            return false;
        out.args.push_back(std::move(arg));
        if (comma == inside.size())
            break;
        start = comma + 1;
    }
    return true;
}

inline bool IsContainer(const std::string& name)
{
    return name == "std::vector" || name == "std::deque" || name == "std::set" || name == "std::unordered_set" ||
           name == "std::map" || name == "std::unordered_map" || name == "std::pair" || name == "std::optional" ||
           name == "std::array";
}

inline void DropRemoved(MiniJson::Value& value, const TypeRef& oldType, const SchemaModel& oldModel,
                        const SchemaModel& newModel);

inline void DropRemovedObject(MiniJson::Value& value, const std::string& structName, const SchemaModel& oldModel,
                              const SchemaModel& newModel)
{
    if (value.kind != MiniJson::Value::Object)
        return;
    auto oldIt = oldModel.structs.find(structName);
    if (oldIt == oldModel.structs.end())
        return;
    auto newIt = newModel.structs.find(structName);
    if (newIt == newModel.structs.end())
        return;
    for (std::size_t i = value.obj.size(); i > 0; --i)
    {
        std::string const& field = value.obj[i - 1].first;
        std::string const* oldFieldType = nullptr;
        for (const auto& entry : oldIt->second)
            if (entry.first == field)
            {
                oldFieldType = &entry.second;
                break;
            }
        if (!oldFieldType)
            continue; // a field the old schema did not have: leave it
        std::string const* newFieldType = nullptr;
        for (const auto& entry : newIt->second)
            if (entry.first == field)
            {
                newFieldType = &entry.second;
                break;
            }
        if (!newFieldType)
        {
            value.obj.erase(value.obj.begin() + static_cast<std::ptrdiff_t>(i - 1));
            continue;
        }
        TypeRef type;
        if (ParseTypeRef(*oldFieldType, type))
            DropRemoved(value.obj[i - 1].second, type, oldModel, newModel);
    }
}

inline void DropRemoved(MiniJson::Value& value, const TypeRef& oldType, const SchemaModel& oldModel,
                        const SchemaModel& newModel)
{
    if (oldModel.structs.count(oldType.name))
    {
        DropRemovedObject(value, oldType.name, oldModel, newModel);
        return;
    }
    if (!IsContainer(oldType.name))
        return;
    if (value.kind == MiniJson::Value::Array)
    {
        if (oldType.name == "std::map" || oldType.name == "std::unordered_map" || oldType.name == "std::pair")
        {
            if (oldType.args.empty())
                return;
            for (MiniJson::Value& element : value.arr)
                if (element.kind == MiniJson::Value::Array && element.arr.size() == 2)
                {
                    DropRemoved(element.arr[0], oldType.args[0], oldModel, newModel);
                    DropRemoved(element.arr[1], oldType.args.size() > 1 ? oldType.args[1] : oldType.args[0], oldModel,
                                newModel);
                }
            return;
        }
        if (!oldType.args.empty())
            for (MiniJson::Value& element : value.arr)
                DropRemoved(element, oldType.args[0], oldModel, newModel);
    }
    else if (!oldType.args.empty())
        DropRemoved(value, oldType.args[0], oldModel, newModel);
}

inline bool UpgradeFixture(const std::string& path, std::string& err, std::string& summary)
{
    std::string text, readError;
    if (!ReadWholeFile(path, text, readError))
    {
        err = readError;
        return false;
    }
    MiniJson::Value root;
    std::string parseError;
    if (!MiniJson::Parse(text, root, parseError))
    {
        err = path + ": " + parseError;
        return false;
    }
    std::string const name = StrOr(root, "name", std::filesystem::path(path).stem().string());
    std::string const oldHash = StrOr(root, "schema", "");
    if (oldHash == kDecisionSchemaHash)
    {
        summary = name + ": already at schema " + kDecisionSchemaHash;
        return true;
    }
    std::string oldText;
    if (!ReadWholeFile(SchemaPathFor(path, oldHash).string(), oldText, readError))
    {
        err = name + ": schema " + oldHash + " is not known: schemas/" + oldHash + ".txt is missing";
        return false;
    }
    SchemaModel const oldModel = ParseSchema(oldText);
    SchemaModel const newModel = ParseSchema(kDecisionSchemaText);

    std::vector<std::string> dropped, added;
    for (const auto& oldStruct : oldModel.structs)
    {
        for (const auto& oldField : oldStruct.second)
        {
            const std::string* now = FieldType(newModel, oldStruct.first, oldField.first);
            if (!now)
                dropped.push_back(oldStruct.first + "." + oldField.first);
            else if (*now != oldField.second)
            {
                err = name + ": refuses retyped field " + oldStruct.first + "." + oldField.first + ": " +
                      oldField.second + " -> " + *now;
                return false;
            }
        }
    }
    for (const auto& newStruct : newModel.structs)
        for (const auto& newField : newStruct.second)
            if (!FieldType(oldModel, newStruct.first, newField.first))
                added.push_back(newStruct.first + "." + newField.first);
    for (const auto& oldEnum : oldModel.enums)
    {
        auto now = newModel.enums.find(oldEnum.first);
        if (now == newModel.enums.end())
            continue;
        bool prefix = now->second.size() >= oldEnum.second.size() &&
                      std::equal(oldEnum.second.begin(), oldEnum.second.end(), now->second.begin());
        if (!prefix)
        {
            err = name + ": refuses enum " + oldEnum.first + ": its existing enumerators moved";
            return false;
        }
    }

    std::filesystem::path const dir = DirOf(path);
    std::string planRelative = StrOr(root, "plan", "plans/" + StrOr(root, "planHash", "") + ".json");
    std::filesystem::path planPath(planRelative);
    if (!planPath.is_absolute())
        planPath = (dir / planPath).lexically_normal();
    std::string planText;
    if (!ReadWholeFile(planPath.string(), planText, readError))
    {
        err = name + ": " + readError;
        return false;
    }
    MiniJson::Value planRoot;
    if (!MiniJson::Parse(planText, planRoot, parseError))
    {
        err = name + ": plan file: " + parseError;
        return false;
    }
    MiniJson::Value* planValue = nullptr;
    for (auto& entry : planRoot.obj)
        if (entry.first == "plan")
            planValue = &entry.second;
    if (!planValue)
    {
        err = name + ": plan file has no plan";
        return false;
    }
    MiniJson::Value* planBody = nullptr;
    for (auto& entry : planValue->obj)
        if (entry.first == "body")
            planBody = &entry.second;
    if (planBody)
    {
        TypeRef planType;
        planType.name = "DungeonPlan";
        DropRemoved(*planBody, planType, oldModel, newModel);
    }
    MiniJson::Value* tactics = nullptr;
    for (auto& entry : planRoot.obj)
        if (entry.first == "tactics")
            tactics = &entry.second;
    if (tactics)
    {
        MiniJson::Value* rows = nullptr;
        for (auto& entry : tactics->obj)
            if (entry.first == "rows")
                rows = &entry.second;
        if (rows && rows->kind == MiniJson::Value::Array)
            for (MiniJson::Value& row : rows->arr)
            {
                TypeRef rowType;
                rowType.name = "BossTacticRow";
                DropRemoved(row, rowType, oldModel, newModel);
            }
    }

    ai::DungeonPlan plan;
    ai::BossTacticTable table;
    bool tableSet = false;
    std::string decodeError;
    if (planBody)
    {
        // The body changed under the drop, so the old object's hash no longer
        // matches it: decode the body alone and re-encode all of it.
        if (!DecisionCapture::DecodeStruct(*planBody, plan, DecisionCapture::DecodeContext{}, decodeError))
        {
            err = name + ": upgraded plan does not decode: " + decodeError;
            return false;
        }
        *planValue = ParseText(DecisionCapture::EncodePlan(plan));
    }
    else if (!DecisionCapture::DecodePlan(*planValue, plan, decodeError))
    {
        err = name + ": upgraded plan does not decode: " + decodeError;
        return false;
    }
    if (tactics && !DecisionCapture::DecodeTactics(*tactics, table, tableSet, decodeError))
    {
        err = name + ": upgraded tactics do not decode: " + decodeError;
        return false;
    }
    std::string const planHash = DecisionCapture::PlanHash(plan);
    if (tactics)
        *tactics = ParseText(DecisionCapture::EncodeTactics(tableSet ? &table : nullptr, plan.map));

    MiniJson::Value* ticks = nullptr;
    for (auto& entry : root.obj)
        if (entry.first == "ticks")
            ticks = &entry.second;
    if (!ticks || ticks->kind != MiniJson::Value::Array)
    {
        err = name + ": no ticks array";
        return false;
    }
    for (MiniJson::Value& tick : ticks->arr)
    {
        for (auto& entry : tick.obj)
        {
            TypeRef type;
            if (entry.first == "facts")
                type.name = "Facts";
            else if (entry.first == "run" || entry.first == "post")
                type.name = "Run";
            else
                continue;
            DropRemoved(entry.second, type, oldModel, newModel);
        }
    }

    for (auto& entry : root.obj)
    {
        if (entry.first == "schema")
            entry.second = VString(kDecisionSchemaHash);
        else if (entry.first == "planHash")
            entry.second = VString(planHash);
        else if (entry.first == "plan")
            entry.second = VString("plans/" + planHash + ".json");
    }

    MiniJson::Value* history = nullptr;
    for (auto& entry : root.obj)
        if (entry.first == "history")
            history = &entry.second;
    if (!history)
    {
        Set(root, "history", VArray());
        history = &root.obj.back().second;
    }
    if (history->kind != MiniJson::Value::Array)
    {
        err = name + ": history is not an array";
        return false;
    }
    MiniJson::Value step = VObject();
    Set(step, "from", VString(oldHash));
    Set(step, "to", VString(kDecisionSchemaHash));
    Set(step, "at", VNumber(static_cast<std::uint64_t>(std::time(nullptr))));
    MiniJson::Value addedList = VArray();
    for (const std::string& field : added)
        addedList.arr.push_back(VString(field));
    MiniJson::Value droppedList = VArray();
    for (const std::string& field : dropped)
        droppedList.arr.push_back(VString(field));
    Set(step, "added", std::move(addedList));
    Set(step, "dropped", std::move(droppedList));
    history->arr.push_back(std::move(step));

    std::string const newPlanText = MiniJson::Raw(planRoot);
    if (newPlanText.size() > 4u * 1024u * 1024u)
    {
        err = name + ": the upgraded plan file would be over 4 MiB";
        return false;
    }
    std::string const tempPath = (dir / (name + ".upgrade-tmp.json")).string();
    {
        std::ofstream output(tempPath, std::ios::binary);
        if (!output)
        {
            err = "cannot write " + tempPath;
            return false;
        }
        output << MiniJson::Raw(root);
    }

    std::string planError;
    if (!WriteIfMissing((dir / "plans" / (planHash + ".json")).string(), newPlanText, 4u * 1024u * 1024u,
                        planError))
    {
        std::filesystem::remove(tempPath);
        err = planError;
        return false;
    }

    Fixture check;
    std::string checkError;
    if (!LoadFixture(tempPath, check, checkError))
    {
        std::filesystem::remove(tempPath);
        err = name + ": the upgraded fixture does not load: " + checkError;
        return false;
    }
    check.status = "fixed";
    Report const report = Replay(check);
    if (!report.pass)
    {
        std::filesystem::remove(tempPath);
        err = name + ": the upgraded fixture does not pass its reproduce check: " +
              (report.failures.empty() ? std::string("failed") : report.failures.front());
        return false;
    }

    std::filesystem::rename(tempPath, path);
    summary = name + ": schema " + oldHash + " -> " + kDecisionSchemaHash + ", dropped " +
              std::to_string(dropped.size()) + ", added " + std::to_string(added.size()) + " field(s), history recorded";
    return true;
}

} // namespace DecisionReplay
} // namespace TortoiseBots
