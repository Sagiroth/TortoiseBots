// Standalone tests for the remote commander's perception feed (issue #884,
// phase 2): the TBM:VIEW v1 grammar, the hostile cap and dedupe, the threat
// top-N, packing under the line limit, free-text sanitising, buffer reuse and
// the send pacing.
//
// Perf guard: the builder lives in a header that includes the standard
// library only, and this file compiles it without a single game header, so
// BuildFrame cannot reach the world: it can only read the Snapshot it is
// handed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_remote_commander_view.cpp -o /tmp/test_remote_commander_view
//   /tmp/test_remote_commander_view

#include "../runtime/RemoteCommanderView.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace TortoiseBots::RemoteCommander::View;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static bool StartsWith(std::string const& s, char const* prefix)
{
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

static std::vector<std::string> Split(std::string const& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == sep)
        {
            out.push_back(cur);
            cur.clear();
        }
        else
            cur += c;
    }
    out.push_back(cur);
    return out;
}

// The payload after "TBM:VIEW|v1|<seq>|<part>|".
static std::string Payload(std::string const& line)
{
    std::vector<std::string> f = Split(line, '|');
    return f.size() == 5 ? f[4] : std::string("<bad>");
}

static char Part(std::string const& line)
{
    std::vector<std::string> f = Split(line, '|');
    return f.size() == 5 && f[3].size() == 1 ? f[3][0] : '?';
}

static void FillParty(Snapshot& s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        PartyMember& m = s.AddPartyMember();
        m.guid = 0x10 + i;
        m.name = "Botname" + std::to_string(i);
        m.hp = 3500;
        m.maxHp = 4200;
        m.power = 5000;
        m.maxPower = 6000;
        m.powerType = 0;
        m.x = -8913.25f;
        m.y = 554.5f;
        m.z = 93.1f;
        m.o = 3.14159f;
        m.map = 0;
        m.instance = 0;
        m.target = 0xF130000F2E0012A4ull;
        m.casting = 10934;
        m.state = 'c';
        m.action = "shadow word: pain";
        m.result = 'F';
        m.reason = "oom";
    }
}

static void FillHostiles(Snapshot& s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        Hostile* h = s.AddHostile();
        if (!h)
            return;
        h->guid = 0xF130000F2E000000ull + i;
        h->entry = 3874;
        h->x = -8900.0f;
        h->y = 560.0f;
        h->z = 94.0f;
        h->hpPct = 64;
        h->target = 0x10;
        h->casting = 0;
        h->evading = (i % 3) == 1;
        h->crowdControlled = (i % 3) == 2;
    }
}

static void FillThreat(Snapshot& s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        ThreatTop* t = s.AddThreat(0xF130000F2E000000ull + i);
        if (!t)
            return;
        t->Push(0x10, 1000.0f);
        t->Push(0x11, 1300.0f);
        t->Push(0x12, 650.0f);
        t->Push(0x13, 10.0f);
    }
}

static void CheckGrammar(Frame const& frame, uint32_t seq)
{
    CHECK(frame.count >= 2);
    CHECK(frame.count <= kMaxLinesPerFrame);
    std::string const prefix = "TBM:VIEW|v1|" + std::to_string(seq) + "|";
    int lastOrder = 0;
    for (size_t i = 0; i < frame.count; ++i)
    {
        std::string const& line = frame.lines[i];
        CHECK(line.size() <= kMaxLineBytes);
        CHECK(StartsWith(line, prefix.c_str()));
        CHECK(Split(line, '|').size() == 5);
        char part = Part(line);
        if (i == 0)
            CHECK(part == 'B');
        else if (i + 1 == frame.count)
            CHECK(part == 'E');
        else
        {
            CHECK(part == 'P' || part == 'H' || part == 'T');
            // P, H, T in that order.
            int order = part == 'P' ? 1 : part == 'H' ? 2 : 3;
            CHECK(order >= lastOrder);
            lastOrder = order;
        }
    }
    CHECK(Payload(frame.lines[frame.count - 1]) == std::to_string(frame.count));
    // B carries exactly five fields: lead, party, hostiles, threats, truncated.
    CHECK(Split(Payload(frame.lines[0]), ',').size() == 5);
}

static size_t CountRecords(Frame const& frame, char part)
{
    size_t n = 0;
    for (size_t i = 0; i < frame.count; ++i)
        if (Part(frame.lines[i]) == part)
            n += Split(Payload(frame.lines[i]), ';').size();
    return n;
}

static void TestEmptyFrame()
{
    Snapshot s;
    s.Clear();
    s.lead = 0x10;
    Frame frame;
    std::string scratch;
    CHECK(BuildFrame(s, 7, frame, scratch) == 2);
    CHECK(frame.lines[0] == "TBM:VIEW|v1|7|B|0x10,0,0,0,0");
    CHECK(frame.lines[1] == "TBM:VIEW|v1|7|E|2");
    CheckGrammar(frame, 7);
}

static void TestPartyRecord()
{
    Snapshot s;
    s.lead = 0x10;
    PartyMember& m = s.AddPartyMember();
    m.guid = 0x10;
    m.name = "Tanky";
    m.hp = 4000;
    m.maxHp = 5000;
    m.power = 35;
    m.maxPower = 100;
    m.powerType = 1;
    m.x = 1.3f;
    m.y = -2.0f;
    m.z = 3.0f;
    m.o = 1.5f;
    m.map = 36;
    m.instance = 12;
    m.target = 0xF130000F2E0012A4ull;
    m.casting = 0;
    m.dead = false;
    m.state = 'c';
    m.action = "heroic strike";
    m.result = 'O';
    Frame frame;
    std::string scratch;
    BuildFrame(s, 1, frame, scratch);
    CHECK(frame.count == 3);
    CHECK(frame.lines[0] == "TBM:VIEW|v1|1|B|0x10,1,0,0,0");
    CHECK(frame.lines[1] ==
        "TBM:VIEW|v1|1|P|0x10,Tanky,4000,5000,35,100,1,1.3,-2.0,3.0,1.50,36,12,0xF130000F2E0012A4,0,0,c,heroic strike,O,-");
    CheckGrammar(frame, 1);
    CHECK(Split(Payload(frame.lines[1]), ',').size() == 20);
}

static void TestHostileAndThreatRecords()
{
    Snapshot s;
    s.lead = 0x10;
    Hostile* h = s.AddHostile();
    h->guid = 0xF1300000000000AAull;
    h->entry = 1234;
    h->x = 10.0f;
    h->y = 20.0f;
    h->z = 30.0f;
    h->hpPct = 250; // clamped
    h->target = 0x11;
    h->casting = 8122;
    h->stealthed = true;
    h->evading = true;
    h->crowdControlled = true;
    Hostile* calm = s.AddHostile();
    calm->guid = 0xBB;
    ThreatTop* t = s.AddThreat(0xF1300000000000AAull);
    t->Push(0x11, 500.0f);
    t->Push(0x10, 2000.0f);
    t->Push(0x12, 1000.0f);
    t->Push(0x13, 999.0f); // fourth: pushes 0x11 out of the top three
    // A hostile whose threat list was empty is not written.
    s.AddThreat(0xBB);
    Frame frame;
    std::string scratch;
    BuildFrame(s, 2, frame, scratch);
    CheckGrammar(frame, 2);
    CHECK(frame.lines[0] == "TBM:VIEW|v1|2|B|0x10,0,2,1,0");
    CHECK(frame.lines[1] ==
        "TBM:VIEW|v1|2|H|0xF1300000000000AA,1234,10.0,20.0,30.0,100,0x11,8122,sec,h,-;0xBB,0,0.0,0.0,0.0,0,0,0,-,h,-");
    CHECK(frame.lines[2] == "TBM:VIEW|v1|2|T|0xF1300000000000AA,0x10:100,0x12:50,0x13:50");
    CHECK(frame.lines[3] == "TBM:VIEW|v1|2|E|4");
}

// Reaction and kind (hostile fields 10 and 11): trailing fields added within
// v1, so the first nine keep their place for an older reader.
static void TestHostileReactionAndKind()
{
    Snapshot s;
    s.lead = 0x10;
    Hostile* wolf = s.AddHostile();
    wolf->guid = 0xA1;
    wolf->entry = 299;
    Hostile* rabbit = s.AddHostile();
    rabbit->guid = 0xA2;
    rabbit->entry = 721;
    rabbit->reaction = 'n';
    rabbit->critter = true;
    Hostile* guard = s.AddHostile();
    guard->guid = 0xA3;
    guard->entry = 68;
    guard->reaction = 'f';
    guard->civilian = true;
    guard->guard = true;
    guard->unattackable = true;
    Hostile* odd = s.AddHostile();
    odd->guid = 0xA4;
    odd->reaction = '?';
    Frame frame;
    std::string scratch;
    BuildFrame(s, 3, frame, scratch);
    CheckGrammar(frame, 3);
    CHECK(frame.lines[0] == "TBM:VIEW|v1|3|B|0x10,0,4,0,0");
    CHECK(frame.lines[1] ==
        "TBM:VIEW|v1|3|H|0xA1,299,0.0,0.0,0.0,0,0,0,-,h,-;0xA2,721,0.0,0.0,0.0,0,0,0,-,n,k;"
        "0xA3,68,0.0,0.0,0.0,0,0,0,-,f,vgu;0xA4,0,0.0,0.0,0.0,0,0,0,-,-,-");
    // An older reader that takes the first nine fields sees the old record.
    std::vector<std::string> fields = Split(Split(Payload(frame.lines[1]), ';')[1], ',');
    CHECK(fields.size() == 11);
    CHECK(fields[0] == "0xA2" && fields[1] == "721" && fields[8] == "-");
    // The longest record still fits a line of its own.
    Hostile big;
    big.guid = 0xFFFFFFFFFFFFFFFFull;
    big.entry = 0xFFFFFFFFu;
    big.x = big.y = big.z = -99999.9f;
    big.hpPct = 100;
    big.target = 0xFFFFFFFFFFFFFFFFull;
    big.casting = 0xFFFFFFFFu;
    big.stealthed = big.evading = big.crowdControlled = true;
    big.critter = big.civilian = big.guard = big.unattackable = true;
    FormatHostileRecord(scratch, big);
    CHECK(scratch.size() + 26 <= kMaxLineBytes);
}

static void TestThreatTopN()
{
    ThreatTop t;
    t.Reset(1);
    t.Push(0, 9999.0f);   // no guid: ignored
    t.Push(5, -1.0f);     // negative: ignored
    CHECK(t.count == 0);
    for (uint64_t g = 1; g <= 10; ++g)
        t.Push(g, static_cast<float>(g * 10));
    CHECK(t.count == kThreatTopN);
    CHECK(t.entries[0].guid == 10 && t.entries[1].guid == 9 && t.entries[2].guid == 8);
    CHECK(t.Percent(0) == 100 && t.Percent(1) == 90 && t.Percent(2) == 80);
    CHECK(t.Percent(3) == 0);
    // Descending input.
    ThreatTop d;
    d.Reset(2);
    d.Push(1, 300.0f);
    d.Push(2, 200.0f);
    d.Push(3, 100.0f);
    d.Push(4, 50.0f);
    CHECK(d.count == 3 && d.entries[2].guid == 3);
    // All zero: the first is the top.
    ThreatTop z;
    z.Reset(3);
    z.Push(1, 0.0f);
    z.Push(2, 0.0f);
    CHECK(z.count == 2 && z.Percent(0) == 100 && z.Percent(1) == 0);
}

static void TestHostileDedupeAndCap()
{
    HostileSet set;
    CHECK(!set.Add(0));
    CHECK(set.Add(42));
    CHECK(!set.Add(42)); // the second bot sees the same mob
    CHECK(set.Contains(42));
    for (uint64_t g = 100; g < 200; ++g)
        set.Add(g);
    CHECK(set.Size() == kMaxHostiles);
    CHECK(set.Full());
    CHECK(!set.Add(500));
    set.Clear();
    CHECK(set.Size() == 0 && set.Add(500));

    Snapshot s;
    size_t added = 0;
    for (size_t i = 0; i < 40; ++i)
        if (s.AddHostile())
            ++added;
    CHECK(added == kMaxHostiles && s.hostileCount == kMaxHostiles);
    size_t threats = 0;
    for (size_t i = 0; i < 40; ++i)
        if (s.AddThreat(i + 1))
            ++threats;
    CHECK(threats == kMaxHostiles);
}

static void TestFullFrameSplitsUnderLimit()
{
    Snapshot s;
    s.lead = 0x10;
    FillParty(s, 5);
    FillHostiles(s, kMaxHostiles);
    FillThreat(s, kMaxHostiles);
    Frame frame;
    std::string scratch;
    size_t lines = BuildFrame(s, 4000000000u, frame, scratch);
    CheckGrammar(frame, 4000000000u);
    CHECK(lines > 5); // split across several lines
    CHECK(lines <= 24);
    CHECK(frame.droppedRecords == 0);
    CHECK(CountRecords(frame, 'P') == 5);
    CHECK(CountRecords(frame, 'H') == kMaxHostiles);
    CHECK(CountRecords(frame, 'T') == kMaxHostiles);
    CHECK(Payload(frame.lines[0]) == "0x10,5,24,24,0");
    // Every record keeps its field count.
    for (size_t i = 1; i + 1 < frame.count; ++i)
    {
        char part = Part(frame.lines[i]);
        for (std::string const& rec : Split(Payload(frame.lines[i]), ';'))
        {
            size_t fields = Split(rec, ',').size();
            if (part == 'P')
                CHECK(fields == 20);
            else if (part == 'H')
                CHECK(fields == 11);
            else
                CHECK(fields == 1 + kThreatTopN);
        }
    }
}

// B says when the caps or the line limit left records out (field 5). Records
// of about 145 bytes take a line each: 40 of them keep 38 party lines (B, the
// 38 and E make 40) and drop two.
static void TestTruncatedFlag()
{
    Snapshot s;
    s.lead = 0x10;
    for (size_t i = 0; i < 40; ++i)
    {
        PartyMember& m = s.AddPartyMember();
        m.guid = 0xFFFFFFFFFFFFFFFFull;
        m.name = "Longestnames";
        m.hp = m.maxHp = m.power = m.maxPower = 99999;
        m.x = m.y = m.z = -19999.9f;
        m.o = 6.28f;
        m.map = m.instance = 1;
        m.target = 0xFFFFFFFFFFFFFFFFull;
        m.casting = 99999;
        m.state = 'c';
        m.action = "a very long action name";
        m.result = 'I';
        m.reason = "a very long reason code";
    }
    Frame frame;
    std::string scratch;
    FormatPartyRecord(scratch, s.party[0]);
    CHECK(scratch.size() > kMaxLineBytes / 2); // one record per line
    CHECK(BuildFrame(s, 5, frame, scratch) == kMaxLinesPerFrame);
    CheckGrammar(frame, 5);
    CHECK(frame.droppedRecords == 2);
    CHECK(CountRecords(frame, 'P') == 38);
    CHECK(Payload(frame.lines[0]) == "0x10,38,0,0,1");
    // The same party short of the cap: nothing dropped, the flag stays 0.
    s.partyCount = 38;
    CHECK(BuildFrame(s, 6, frame, scratch) == kMaxLinesPerFrame);
    CheckGrammar(frame, 6);
    CHECK(frame.droppedRecords == 0);
    CHECK(Payload(frame.lines[0]) == "0x10,38,0,0,0");
}

static void TestSanitising()
{
    Snapshot s;
    PartyMember& m = s.AddPartyMember();
    m.guid = 1;
    m.name = "Evil|Name;x,y:z";
    m.action = std::string("cast\nspell|with;every,sep:and a very long tail that goes on");
    Frame frame;
    std::string scratch;
    BuildFrame(s, 3, frame, scratch);
    CheckGrammar(frame, 3);
    std::vector<std::string> fields = Split(Payload(frame.lines[1]), ',');
    CHECK(fields.size() == 20);
    CHECK(fields[1] == "Evil_Name_x_");
    CHECK(fields[17].size() == kMaxActionBytes);
    CHECK(fields[17] == "cast_spell_with_");
    CHECK(fields[18] == "-"); // no result set
    CHECK(fields[19] == "-"); // no reason set
    // UTF-8: never cut inside a character.
    std::string out;
    AppendToken(out, "\xC3\xA9\xC3\xA9\xC3\xA9", 5); // three two-byte characters, 5 bytes allowed
    CHECK(out == "\xC3\xA9\xC3\xA9");
    out.clear();
    AppendToken(out, "", 5);
    CHECK(out == "-");
    // Unknown state letter and bad floats.
    Snapshot b;
    PartyMember& odd = b.AddPartyMember();
    odd.guid = 2;
    odd.state = 'x';
    odd.x = 1.0e30f;
    BuildFrame(b, 1, frame, scratch);
    std::vector<std::string> f2 = Split(Payload(frame.lines[1]), ',');
    CHECK(f2[16] == "-");
    CHECK(f2[7] == "0.0");
}

// The action verdict as parts: name, result letter, reason code (field 18-20).
static void TestActionResultFields()
{
    Snapshot s;
    PartyMember& m = s.AddPartyMember();
    m.guid = 7;
    m.name = "Pat";
    m.action = "drink";
    m.result = 'F';
    m.reason = "no-item";
    PartyMember& bad = s.AddPartyMember();
    bad.guid = 8;
    bad.name = "Bad";
    bad.action = "spellstone";
    bad.result = 'x';               // not a result letter
    bad.reason = "oom,with|seps";   // separators and length are cleaned
    Frame frame;
    std::string scratch;
    BuildFrame(s, 9, frame, scratch);
    CheckGrammar(frame, 9);
    std::vector<std::string> recs = Split(Payload(frame.lines[1]), ';');
    CHECK(recs.size() == 2);
    std::vector<std::string> a = Split(recs[0], ',');
    CHECK(a.size() == 20);
    CHECK(a[17] == "drink" && a[18] == "F" && a[19] == "no-item");
    std::vector<std::string> b = Split(recs[1], ',');
    CHECK(b.size() == 20);
    CHECK(b[17] == "spellstone" && b[18] == "-");
    CHECK(b[19] == "oom_with_s");
    CHECK(b[19].size() == kMaxReasonBytes);
    // Every result letter the grammar names passes through; reuse resets the slots.
    for (char c : std::string("OFIUPV"))
    {
        s.Clear();
        PartyMember& r = s.AddPartyMember();
        CHECK(r.result == '-' && r.reason.empty());
        r.guid = 1;
        r.result = c;
        BuildFrame(s, 1, frame, scratch);
        CHECK(Split(Payload(frame.lines[1]), ',')[18] == std::string(1, c));
    }
    // The widest record the caps allow: name and fields at their caps, still
    // under a line, so a party of five never loses one to the limit.
    Snapshot w;
    PartyMember& x = w.AddPartyMember();
    x.guid = 0xFFFFFFFFFFFFFFFFull;
    x.name = "Longestnames";
    x.hp = x.maxHp = x.power = x.maxPower = 4294967295u;
    x.powerType = 3;
    x.x = x.y = x.z = -9999999.9f;
    x.o = 6.28f;
    x.map = x.instance = 4294967295u;
    x.target = 0xFFFFFFFFFFFFFFFFull;
    x.casting = 4294967295u;
    x.dead = true;
    x.state = 'c';
    x.action = std::string(40, 'a');
    x.result = 'I';
    x.reason = std::string(40, 'r');
    FormatPartyRecord(scratch, x);
    std::printf("worst-case party record: %zu bytes\n", scratch.size());
    CHECK(scratch.size() == 204); // the exact maximum; with the longest line prefix (26 bytes) it stays under 250
    CHECK(scratch.size() + 26 <= kMaxLineBytes);
}

static void TestBufferReuse()
{
    Snapshot s;
    s.lead = 0x10;
    FillParty(s, 5);
    FillHostiles(s, kMaxHostiles);
    FillThreat(s, kMaxHostiles);
    Frame frame;
    std::string scratch;
    BuildFrame(s, 1, frame, scratch);
    size_t const linesCapacity = frame.lines.capacity();
    char const* const firstBuffer = frame.lines[1].data();
    size_t const partyCapacity = s.party.capacity();
    // Next frame: same shape, no growth.
    s.Clear();
    s.lead = 0x10;
    FillParty(s, 5);
    FillHostiles(s, kMaxHostiles);
    FillThreat(s, kMaxHostiles);
    BuildFrame(s, 2, frame, scratch);
    CHECK(frame.lines.capacity() == linesCapacity);
    CHECK(frame.lines[1].data() == firstBuffer);
    CHECK(s.party.capacity() == partyCapacity);
    CheckGrammar(frame, 2);
    // A smaller frame after a big one ends with its own E line.
    s.Clear();
    s.lead = 0x10;
    BuildFrame(s, 3, frame, scratch);
    CHECK(frame.count == 2);
    CheckGrammar(frame, 3);
    CHECK(s.partyCount == 0 && s.hostileCount == 0 && s.threatCount == 0);
}

static void TestOversizedRecordDropped()
{
    // A record that cannot fit a line on its own is dropped, never cut.
    Frame frame;
    frame.count = 0;
    std::string huge(kMaxLineBytes, 'a');
    Detail::PartWriter::NextLine(frame);
    Detail::PartWriter writer(frame, 1, 'H', 1);
    CHECK(!writer.Add(huge));
    CHECK(frame.count == 1);
    CHECK(frame.droppedRecords == 1);
    CHECK(writer.Add("small"));
    CHECK(frame.count == 2);
    CHECK(frame.lines[1] == "TBM:VIEW|v1|1|H|small");
}

static void TestLineCap()
{
    // More records than kMaxLinesPerFrame lines can hold: the frame stops at
    // the cap and still ends with E.
    Frame frame;
    frame.count = 0;
    Detail::PartWriter::NextLine(frame);
    std::string record(kMaxLineBytes - 20, 'r');
    size_t written = 0;
    {
        Detail::PartWriter writer(frame, 9, 'H', 1);
        for (size_t i = 0; i < 100; ++i)
            if (writer.Add(record))
                ++written;
    }
    CHECK(frame.count == kMaxLinesPerFrame - 1);
    CHECK(written == kMaxLinesPerFrame - 2);
    CHECK(frame.droppedRecords == 100 - written);
}

static void TestPacer()
{
    Pacer pacer;
    CHECK(!pacer.Due(400, 1000));
    CHECK(!pacer.Due(500, 1000));
    CHECK(pacer.Due(100, 1000));
    pacer.Start(20);
    size_t first = 0;
    CHECK(pacer.NextBatch(first) == kLinesPerTick && first == 0);
    CHECK(pacer.Pending());
    // The next frame falls due while 12 lines are unsent: skipped.
    CHECK(!pacer.Due(1000, 1000));
    CHECK(pacer.Skipped() == 1);
    CHECK(pacer.NextBatch(first) == kLinesPerTick && first == kLinesPerTick);
    CHECK(pacer.NextBatch(first) == 4 && first == 2 * kLinesPerTick);
    CHECK(!pacer.Pending());
    CHECK(pacer.NextBatch(first) == 0);
    CHECK(pacer.Due(1000, 1000));
    pacer.Start(3);
    pacer.Abandon();
    CHECK(!pacer.Pending());
    // Interval clamp.
    CHECK(ClampIntervalMs(0) == kMinIntervalMs);
    CHECK(ClampIntervalMs(-5) == kMinIntervalMs);
    CHECK(ClampIntervalMs(1000) == 1000);
    CHECK(ClampIntervalMs(10000000) == kMaxIntervalMs);
    CHECK(kDefaultIntervalMs == 1000);
}

int main()
{
    TestEmptyFrame();
    TestPartyRecord();
    TestHostileAndThreatRecords();
    TestHostileReactionAndKind();
    TestThreatTopN();
    TestHostileDedupeAndCap();
    TestFullFrameSplitsUnderLimit();
    TestTruncatedFlag();
    TestSanitising();
    TestActionResultFields();
    TestBufferReuse();
    TestOversizedRecordDropped();
    TestLineCap();
    TestPacer();
    std::printf("remote commander view: %d checks passed\n", checks);
    return 0;
}
