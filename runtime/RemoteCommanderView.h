#pragma once

// Issue #884, phase 2: the remote commander's perception feed ("the bots'
// eyes"). While a commander is bound (runtime/RemoteCommanderPolicy.h) and its
// feed is on (`.bot commander view on|off`; a new binding starts off), the
// module sends the commander one `TBM:VIEW` frame about every
// AiPlayerbot.RemoteCommanderViewMs (default 1000): the bound bots, the
// hostiles they see, and the top of each engaged hostile's threat list.
//
// This header holds the frame grammar and every decision about it: the caps,
// the hostile dedupe, the threat top-N, the packing of records into lines
// under the chat line limit, and the send pacing. The builder only reads a
// Snapshot of plain values: the world glue in commands/BotCommands.cpp fills
// it from values the bots already keep (their cached "attackers" and
// "possible targets" lists, their victim and selection, the units attacking
// them, the hostile's own threat list), never from a new world scan. With no
// game type in reach here, the builder cannot touch the world;
// tools/test_remote_commander_view.cpp builds it standalone to prove that.
//
// Plain data and the standard library only.
//
// Grammar, version v1 (one frame = the lines sharing one <seq>):
//
//   TBM:VIEW|v1|<seq>|B|<lead>,<party>,<hostiles>,<threats>,<truncated>
//   TBM:VIEW|v1|<seq>|P|<party record>[;<party record>...]
//   TBM:VIEW|v1|<seq>|H|<hostile record>[;<hostile record>...]
//   TBM:VIEW|v1|<seq>|T|<threat record>[;<threat record>...]
//   TBM:VIEW|v1|<seq>|E|<lines>
//
//   party record:   guid,name,hp,maxhp,power,maxpower,powertype,x,y,z,o,map,
//                   instance,target,spell,dead,state,action,result,reason
//   hostile record: guid,entry,x,y,z,hp%,target,spell,flags
//   threat record:  hostile,guid:pct[,guid:pct...]
//
// B comes first and E last; <lines> counts every line of the frame, B and E
// included, so a client knows the frame is whole. P, H and T lines appear in
// that order, each only when it has records, and a part may span several
// lines. <truncated> is 1 when the caps or the line limit left at least one
// record out (the counts in B are what was written), else 0. It is the fifth
// field of B and was added within v1: fields 1-4 keep their meaning, and a
// reader takes a missing fifth field as 0. Fields never contain `|`, `;`, `,`
// or `:`.
//
// The party record's last three fields describe the bot's newest action
// verdict as parts: `action` is the action name only (at most 16 bytes, `-`
// for none), `result` one letter (O ok, F failed, I impossible, U useless,
// P prerequisites queued first, V dropped before running by a veto or the
// failure backoff, `-` none yet), `reason` a short code when the engine
// knows it (no-item, oom, range, los, cd, moving, casting, no-target; at
// most 10 bytes, `-` for none). Until this change field 18 held the head of
// the AI's rolling action log cut to 24 bytes; `result` and `reason` are new
// trailing fields, so a v1 reader that stops at 18 fields keeps working
// (parsers accept longer records) and now gets a clean action name.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace TortoiseBots {
namespace RemoteCommander {
namespace View {

// AiPlayerbot.RemoteCommanderViewMs: default, and the clamp applied to it.
constexpr uint32_t kDefaultIntervalMs = 1000;
constexpr uint32_t kMinIntervalMs = 250;
constexpr uint32_t kMaxIntervalMs = 60000;
// Hostiles per frame (deduplicated across the bound bots).
constexpr size_t kMaxHostiles = 24;
// Threat entries per engaged hostile.
constexpr size_t kThreatTopN = 3;
// Bytes per line: under the 255-byte chat and addon message limit.
constexpr size_t kMaxLineBytes = 250;
// Lines per frame. A full frame (5 bots, 24 hostiles, 24 threat lists) packs
// into about 20; the cap only bounds a malformed snapshot.
constexpr size_t kMaxLinesPerFrame = 40;
// Lines sent to one commander per world tick; the rest go out on later ticks.
constexpr size_t kLinesPerTick = 8;
// Text field caps (bytes).
constexpr size_t kMaxNameBytes = 12;
constexpr size_t kMaxActionBytes = 16;
constexpr size_t kMaxReasonBytes = 10;

inline uint32_t ClampIntervalMs(int64_t ms)
{
    if (ms < static_cast<int64_t>(kMinIntervalMs))
        return kMinIntervalMs;
    if (ms > static_cast<int64_t>(kMaxIntervalMs))
        return kMaxIntervalMs;
    return static_cast<uint32_t>(ms);
}

// ---------------------------------------------------------------------------
// Snapshot: what the glue copies out of the bots' cached values
// ---------------------------------------------------------------------------

struct PartyMember
{
    uint64_t guid = 0;
    std::string name;
    uint32_t hp = 0;
    uint32_t maxHp = 0;
    uint32_t power = 0;
    uint32_t maxPower = 0;
    uint8_t powerType = 0; // 0 mana, 1 rage, 2 focus, 3 energy
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
    uint32_t map = 0;
    uint32_t instance = 0;
    uint64_t target = 0;   // the bot's selection, else its victim
    uint32_t casting = 0;  // spell id being cast or channelled, 0 = none
    bool dead = false;
    char state = 'n';      // c combat, n non-combat, d dead, r reaction
    std::string action;    // name of the bot's newest action verdict
    char result = '-';     // O ok, F failed, I impossible, U useless, P prereq, V vetoed, - none
    std::string reason;    // short failure code, empty = unknown
};

struct Hostile
{
    uint64_t guid = 0;
    uint32_t entry = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32_t hpPct = 0;
    uint64_t target = 0;
    uint32_t casting = 0;
    bool stealthed = false;
    bool evading = false;
    bool crowdControlled = false;
};

struct ThreatEntry
{
    uint64_t guid = 0;
    float threat = 0.0f;
};

// The top kThreatTopN entries of one hostile's threat list, kept sorted
// (highest first) as the glue pushes the list's entries in any order.
struct ThreatTop
{
    uint64_t hostile = 0;
    ThreatEntry entries[kThreatTopN];
    size_t count = 0;

    void Reset(uint64_t hostileGuid)
    {
        hostile = hostileGuid;
        count = 0;
    }

    void Push(uint64_t guid, float threat)
    {
        if (!guid || !(threat >= 0.0f))
            return;
        size_t pos = count;
        while (pos > 0 && entries[pos - 1].threat < threat)
            --pos;
        if (pos >= kThreatTopN)
            return;
        size_t last = count < kThreatTopN ? count : kThreatTopN - 1;
        for (size_t i = last; i > pos; --i)
            entries[i] = entries[i - 1];
        entries[pos].guid = guid;
        entries[pos].threat = threat;
        if (count < kThreatTopN)
            ++count;
    }

    // Percent of the top entry's threat (the top is 100).
    uint32_t Percent(size_t index) const
    {
        if (index >= count)
            return 0;
        float const top = entries[0].threat;
        if (top <= 0.0f)
            return index == 0 ? 100u : 0u;
        float pct = entries[index].threat * 100.0f / top;
        if (pct < 0.0f)
            pct = 0.0f;
        if (pct > 100.0f)
            pct = 100.0f;
        return static_cast<uint32_t>(pct + 0.5f);
    }
};

// Hostile guids already taken for this frame: dedupe across the bots, capped.
class HostileSet
{
public:
    void Clear() { m_guids.clear(); }
    bool Contains(uint64_t guid) const
    {
        for (uint64_t taken : m_guids)
            if (taken == guid)
                return true;
        return false;
    }
    bool Full() const { return m_guids.size() >= kMaxHostiles; }
    // False for 0, a duplicate, or a full set.
    bool Add(uint64_t guid)
    {
        if (!guid || Full() || Contains(guid))
            return false;
        m_guids.push_back(guid);
        return true;
    }
    size_t Size() const { return m_guids.size(); }

private:
    std::vector<uint64_t> m_guids;
};

// One frame's worth of facts. Kept and reused by the glue: Clear() keeps the
// vectors' capacity, so steady-state frames do not allocate.
struct Snapshot
{
    uint64_t lead = 0;
    std::vector<PartyMember> party;
    size_t partyCount = 0;
    std::vector<Hostile> hostiles;
    size_t hostileCount = 0;
    std::vector<ThreatTop> threats;
    size_t threatCount = 0;

    void Clear()
    {
        lead = 0;
        partyCount = 0;
        hostileCount = 0;
        threatCount = 0;
    }

    // Next slot, reused when one is there already.
    PartyMember& AddPartyMember()
    {
        if (partyCount == party.size())
            party.emplace_back();
        PartyMember& member = party[partyCount++];
        // Field by field, so the two strings keep their buffers.
        member.guid = 0;
        member.name.clear();
        member.hp = member.maxHp = member.power = member.maxPower = 0;
        member.powerType = 0;
        member.x = member.y = member.z = member.o = 0.0f;
        member.map = member.instance = 0;
        member.target = 0;
        member.casting = 0;
        member.dead = false;
        member.state = 'n';
        member.action.clear();
        member.result = '-';
        member.reason.clear();
        return member;
    }

    // nullptr once kMaxHostiles are taken.
    Hostile* AddHostile()
    {
        if (hostileCount >= kMaxHostiles)
            return nullptr;
        if (hostileCount == hostiles.size())
            hostiles.emplace_back();
        Hostile& hostile = hostiles[hostileCount++];
        hostile = Hostile();
        return &hostile;
    }

    ThreatTop* AddThreat(uint64_t hostileGuid)
    {
        if (threatCount >= kMaxHostiles)
            return nullptr;
        if (threatCount == threats.size())
            threats.emplace_back();
        ThreatTop& top = threats[threatCount++];
        top.Reset(hostileGuid);
        return &top;
    }

    // Drop the last threat slot (its hostile had no entries after all).
    void DropLastThreat()
    {
        if (threatCount)
            --threatCount;
    }
};

// ---------------------------------------------------------------------------
// Frame builder
// ---------------------------------------------------------------------------

// The lines of one frame. Kept per commander and reused: lines grow to the
// largest frame seen and are overwritten in place.
struct Frame
{
    std::vector<std::string> lines;
    size_t count = 0;
    // Records the caps left out of the frame (never for a well-formed snapshot).
    size_t droppedRecords = 0;
};

// Copies a free-text field (a name, an action) into the frame: separator
// characters and control bytes become '_', cut at maxBytes on a UTF-8
// character boundary.
inline void AppendToken(std::string& out, std::string const& text, size_t maxBytes)
{
    size_t end = text.size() < maxBytes ? text.size() : maxBytes;
    // Never cut inside a UTF-8 sequence: back up over continuation bytes.
    if (end < text.size())
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
            --end;
    if (end == 0)
    {
        out += '-';
        return;
    }
    for (size_t i = 0; i < end; ++i)
    {
        char c = text[i];
        if (c == '|' || c == ';' || c == ',' || c == ':' || static_cast<unsigned char>(c) < 0x20 ||
            c == 0x7F)
            c = '_';
        out += c;
    }
}

inline void AppendUint(std::string& out, uint64_t value)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    out += buffer;
}

// GUIDs in the form `.bot cmd` accepts back: 0x + upper-case hex, 0 for none.
inline void AppendGuid(std::string& out, uint64_t guid)
{
    if (!guid)
    {
        out += '0';
        return;
    }
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(guid));
    out += buffer;
}

inline void AppendFloat(std::string& out, float value, int decimals)
{
    char buffer[32];
    if (!(value == value) || value > 1.0e7f || value < -1.0e7f)
        value = 0.0f;
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, static_cast<double>(value));
    out += buffer;
}

inline bool IsResultLetter(char c)
{
    return c == 'O' || c == 'F' || c == 'I' || c == 'U' || c == 'P' || c == 'V';
}

inline void FormatPartyRecord(std::string& out, PartyMember const& m)
{
    out.clear();
    AppendGuid(out, m.guid); out += ',';
    AppendToken(out, m.name, kMaxNameBytes); out += ',';
    AppendUint(out, m.hp); out += ',';
    AppendUint(out, m.maxHp); out += ',';
    AppendUint(out, m.power); out += ',';
    AppendUint(out, m.maxPower); out += ',';
    AppendUint(out, m.powerType); out += ',';
    AppendFloat(out, m.x, 1); out += ',';
    AppendFloat(out, m.y, 1); out += ',';
    AppendFloat(out, m.z, 1); out += ',';
    AppendFloat(out, m.o, 2); out += ',';
    AppendUint(out, m.map); out += ',';
    AppendUint(out, m.instance); out += ',';
    AppendGuid(out, m.target); out += ',';
    AppendUint(out, m.casting); out += ',';
    out += m.dead ? '1' : '0'; out += ',';
    char const state = (m.state == 'c' || m.state == 'n' || m.state == 'd' || m.state == 'r') ? m.state : '-';
    out += state; out += ',';
    AppendToken(out, m.action, kMaxActionBytes); out += ',';
    out += IsResultLetter(m.result) ? m.result : '-'; out += ',';
    AppendToken(out, m.reason, kMaxReasonBytes);
}

// Flags: s stealthed, e evading, c crowd-controlled; '-' for none.
inline void FormatHostileRecord(std::string& out, Hostile const& h)
{
    out.clear();
    AppendGuid(out, h.guid); out += ',';
    AppendUint(out, h.entry); out += ',';
    AppendFloat(out, h.x, 1); out += ',';
    AppendFloat(out, h.y, 1); out += ',';
    AppendFloat(out, h.z, 1); out += ',';
    AppendUint(out, h.hpPct > 100 ? 100 : h.hpPct); out += ',';
    AppendGuid(out, h.target); out += ',';
    AppendUint(out, h.casting); out += ',';
    size_t const before = out.size();
    if (h.stealthed)
        out += 's';
    if (h.evading)
        out += 'e';
    if (h.crowdControlled)
        out += 'c';
    if (out.size() == before)
        out += '-';
}

inline void FormatThreatRecord(std::string& out, ThreatTop const& t)
{
    out.clear();
    AppendGuid(out, t.hostile);
    for (size_t i = 0; i < t.count && i < kThreatTopN; ++i)
    {
        out += ',';
        AppendGuid(out, t.entries[i].guid);
        out += ':';
        AppendUint(out, t.Percent(i));
    }
}

namespace Detail {

inline void LinePrefix(std::string& line, uint32_t seq, char part)
{
    line.clear();
    line += "TBM:VIEW|v1|";
    AppendUint(line, seq);
    line += '|';
    line += part;
    line += '|';
}

// Packs records of one part into lines; returns records written.
class PartWriter
{
public:
    PartWriter(Frame& frame, uint32_t seq, char part, size_t reservedLines)
        : m_frame(frame), m_seq(seq), m_part(part), m_reserved(reservedLines)
    {
    }

    bool Add(std::string const& record)
    {
        if (m_open)
        {
            std::string& line = m_frame.lines[m_frame.count - 1];
            if (line.size() + 1 + record.size() <= kMaxLineBytes)
            {
                line += ';';
                line += record;
                return true;
            }
        }
        // A new line: the record must fit on a line of its own, and the frame
        // must keep room for the lines still to come (E at least).
        if (m_frame.count + 1 + m_reserved > kMaxLinesPerFrame)
            return Drop();
        std::string& line = NextLine(m_frame);
        LinePrefix(line, m_seq, m_part);
        if (line.size() + record.size() > kMaxLineBytes)
        {
            --m_frame.count;
            m_open = false;
            return Drop();
        }
        line += record;
        m_open = true;
        return true;
    }

    static std::string& NextLine(Frame& frame)
    {
        if (frame.count == frame.lines.size())
            frame.lines.emplace_back();
        std::string& line = frame.lines[frame.count++];
        line.clear();
        return line;
    }

private:
    bool Drop()
    {
        ++m_frame.droppedRecords;
        return false;
    }

    Frame& m_frame;
    uint32_t m_seq;
    char m_part;
    size_t m_reserved;
    bool m_open = false;
};

} // namespace Detail

// Builds one frame from the snapshot into `frame` (reused); returns the line
// count. Reads the snapshot only.
inline size_t BuildFrame(Snapshot const& snapshot, uint32_t seq, Frame& frame, std::string& scratch)
{
    frame.count = 0;
    frame.droppedRecords = 0;
    Detail::PartWriter::NextLine(frame); // B, written last (it carries the counts)

    size_t party = 0;
    {
        Detail::PartWriter writer(frame, seq, 'P', 1);
        for (size_t i = 0; i < snapshot.partyCount && i < snapshot.party.size(); ++i)
        {
            FormatPartyRecord(scratch, snapshot.party[i]);
            if (writer.Add(scratch))
                ++party;
        }
    }
    size_t hostiles = 0;
    {
        Detail::PartWriter writer(frame, seq, 'H', 1);
        size_t const n = snapshot.hostileCount < kMaxHostiles ? snapshot.hostileCount : kMaxHostiles;
        for (size_t i = 0; i < n && i < snapshot.hostiles.size(); ++i)
        {
            FormatHostileRecord(scratch, snapshot.hostiles[i]);
            if (writer.Add(scratch))
                ++hostiles;
        }
        frame.droppedRecords += snapshot.hostileCount > n ? snapshot.hostileCount - n : 0;
    }
    size_t threats = 0;
    {
        Detail::PartWriter writer(frame, seq, 'T', 1);
        size_t const n = snapshot.threatCount < kMaxHostiles ? snapshot.threatCount : kMaxHostiles;
        for (size_t i = 0; i < n && i < snapshot.threats.size(); ++i)
        {
            if (!snapshot.threats[i].count)
                continue;
            FormatThreatRecord(scratch, snapshot.threats[i]);
            if (writer.Add(scratch))
                ++threats;
        }
    }

    std::string& end = Detail::PartWriter::NextLine(frame);
    Detail::LinePrefix(end, seq, 'E');
    AppendUint(end, frame.count);

    std::string& begin = frame.lines[0];
    Detail::LinePrefix(begin, seq, 'B');
    AppendGuid(begin, snapshot.lead);
    begin += ',';
    AppendUint(begin, party);
    begin += ',';
    AppendUint(begin, hostiles);
    begin += ',';
    AppendUint(begin, threats);
    begin += ',';
    begin += frame.droppedRecords > 0 ? '1' : '0';
    return frame.count;
}

// ---------------------------------------------------------------------------
// Pacing (per commander)
// ---------------------------------------------------------------------------

// A frame is due every interval; it goes out kLinesPerTick lines per world
// tick. A frame still being sent when the next one falls due makes that one
// skip (counted), so a slow client never queues frames up.
class Pacer
{
public:
    // Advances the clock; true when a new frame should be built now.
    bool Due(uint32_t diffMs, uint32_t intervalMs)
    {
        m_sinceFrameMs += diffMs;
        if (m_sinceFrameMs < intervalMs)
            return false;
        m_sinceFrameMs = 0;
        if (Pending())
        {
            ++m_skipped;
            return false;
        }
        return true;
    }

    void Start(size_t lineCount)
    {
        m_next = 0;
        m_count = lineCount;
    }

    bool Pending() const { return m_next < m_count; }

    // The [first, first + n) lines to send this tick; advances.
    size_t NextBatch(size_t& first)
    {
        first = m_next;
        size_t n = m_count - m_next;
        if (n > kLinesPerTick)
            n = kLinesPerTick;
        m_next += n;
        return n;
    }

    // The frame in flight is abandoned (the commander is between maps).
    void Abandon() { m_next = m_count; }

    uint32_t Skipped() const { return m_skipped; }

private:
    uint32_t m_sinceFrameMs = 0;
    size_t m_next = 0;
    size_t m_count = 0;
    uint32_t m_skipped = 0;
};

} // namespace View
} // namespace RemoteCommander
} // namespace TortoiseBots
