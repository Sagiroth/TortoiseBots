// Standalone tests for the Dungeons tab mob feed (issue #1101): the tier order and the cap of each run's mob list, the per-run
// caps under the cycle budget, the run rotation, the sight readers of a raid, the DUNGEON_MOBS JSON (64-bit ids as strings,
// escaping, the omitted target, "mobs":[] when read and empty, the "runs":[] end marker) and the packing into datagrams (whole
// runs, the mob, run and byte limits of a part, the worst-case sizes).
//
// The header includes the standard library only, so this file compiles it without a game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_mob_telemetry.cpp -o /tmp/test_dungeon_mob_telemetry
//   /tmp/test_dungeon_mob_telemetry

#include "../runtime/DungeonMobTelemetry.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TortoiseBots::DungeonMobTelemetry;

namespace {

int g_failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

size_t Count(std::string const& hay, std::string const& needle)
{
    size_t n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size()))
        ++n;
    return n;
}

MemberInfo Member(uint32_t guid, float x, float y, bool bot = true, bool tank = false, uint64_t target = 0)
{
    MemberInfo m;
    m.guid = guid;
    m.id = 0x0000000000000000ull + guid;
    m.target = target;
    m.x = x;
    m.y = y;
    m.bot = bot;
    m.tank = tank;
    return m;
}

Candidate Cand(uint64_t id, uint8_t source, float x, float y, char reaction = 'h', char kind = 0, char rank = 0, uint64_t victim = 0)
{
    Candidate c;
    c.mob.id = id;
    c.mob.entry = static_cast<uint32_t>(id);
    c.mob.name = "Mob " + std::to_string(id);
    c.mob.hpPct = 100;
    c.mob.x = x;
    c.mob.y = y;
    c.mob.reaction = reaction;
    c.mob.kind = kind;
    c.mob.rank = rank;
    c.mob.victim = victim;
    c.source = source;
    return c;
}

// A mob with every optional field and the longest strings the writer keeps (ids of 20 digits, a name and a cast clipped at
// 24 bytes of characters JSON escapes into 6 bytes each).
MobInfo WorstMob(uint64_t i)
{
    MobInfo m;
    m.id = 18446744073709551615ull - i;
    m.entry = 4294967295u;
    m.name = std::string(40, '\x01');
    m.hpPct = 100;
    m.x = -12345.6f;
    m.y = -12345.6f;
    m.victim = 18446744073709551615ull;
    m.reaction = 'n';
    m.kind = 'u';
    m.level = 63;
    m.rank = 'b';
    m.cc = 's';
    m.cast = std::string(40, '\x02');
    m.hasO = true;
    m.o = -3.14159f;
    m.castId = 4294967295u;
    m.castLeftMs = 4294967295u;
    m.castTotalMs = 4294967295u;
    m.castTarget = 18446744073709551615ull;
    m.channel = true;
    m.hasShape = true;
    m.shape.kind = TortoiseBots::SpellShape::Kind::Line;
    m.shape.length = 4294967295.0f;
    m.shape.width = 4294967295.0f;
    return m;
}

// A realistic mob: plain name and cast.
MobInfo PlainMob(uint64_t i)
{
    MobInfo m;
    m.id = 17370988236253874177ull + i;
    m.entry = 11520;
    m.name = "Taragaman the Hungerer"; // 22
    m.hpPct = 87;
    m.x = -1234.5f;
    m.y = 2345.6f;
    m.victim = 17370988236253874000ull;
    m.reaction = 'h';
    m.level = 60;
    m.rank = 'e';
    m.cast = "Shadow Bolt Volley";
    return m;
}

RunInfo Run(uint32_t map, uint32_t instance, size_t members, size_t mobs, bool worst)
{
    RunInfo r;
    r.map = map;
    r.instance = instance;
    r.raid = members > kPartySize;
    for (size_t i = 0; i < members; ++i)
    {
        MemberInfo m = Member(4000000000u - static_cast<uint32_t>(i), 0, 0);
        m.id = 18446744073709551615ull - i;
        m.target = 18446744073709551615ull - 100 - i;
        r.members.push_back(m);
    }
    r.mobsReported = true;
    r.mobsTotal = 999999;
    for (size_t i = 0; i < mobs; ++i)
        r.mobs.push_back(worst ? WorstMob(i) : PlainMob(i));
    return r;
}

void TestSelectionOrder()
{
    std::vector<MemberInfo> members = { Member(1, 0, 0), Member(2, 10, 0) };
    std::vector<Candidate> c;
    c.push_back(Cand(100, kFromSight, 5, 5, 'n'));               // neutral, near
    c.push_back(Cand(101, kFromSight, 80, 80, 'h'));             // hostile, far
    c.push_back(Cand(102, kFromSight, 1, 1, 'h', 'k'));          // critter, nearest
    c.push_back(Cand(103, kFromAttackers, 90, 90, 'h'));         // attacking a member
    c.push_back(Cand(104, kFromSelection, 200, 200, 'h'));       // a member's victim
    c.push_back(Cand(105, kFromSight, 300, 300, 'h', 0, 'b'));   // a boss seen far away
    c.push_back(Cand(106, kFromSight, 3, 3, 'h'));               // hostile, nearer than 101
    c.push_back(Cand(107, kFromSight, 400, 400, 'n', 0, 0, 2));  // a neutral that targets member 2
    c.push_back(Cand(101, kFromSight, 0, 0, 'h'));               // duplicate of 101: dropped, the first kept
    std::vector<MobInfo> out;
    size_t const total = SelectMobs(c, members, 100, out);
    Check(total == 8, "mobs_total counts distinct candidates before the cut");
    std::vector<uint64_t> want = { 104, 107, 103, 105, 106, 101, 100, 102 };
    bool same = out.size() == want.size();
    for (size_t i = 0; same && i < want.size(); ++i)
        same = out[i].id == want[i];
    Check(same, "tier order: engaged (victim on a member first by distance), attacker, boss, hostile by distance, neutral, other");
    if (!same)
    {
        for (MobInfo const& m : out)
            std::fprintf(stderr, "  got %llu\n", static_cast<unsigned long long>(m.id));
    }

    // The cap keeps the lowest tiers.
    std::vector<Candidate> c2;
    for (uint64_t i = 0; i < 30; ++i)
        c2.push_back(Cand(1000 + i, kFromSight, -static_cast<float>(i), 0, 'n'));
    c2.push_back(Cand(2000, kFromAttackers, 500, 500));
    c2.push_back(Cand(2001, kFromSelection, 600, 600));
    size_t const total2 = SelectMobs(c2, members, 5, out);
    Check(total2 == 32 && out.size() == 5, "cap cuts the list; total stays the full count");
    Check(out.size() == 5 && out[0].id == 2001 && out[1].id == 2000 && out[2].id == 1000 && out[3].id == 1001,
        "cap never drops an engaged or attacking mob for a sight-list one; distance breaks ties");

    // Tier rules on their own.
    std::unordered_set<uint64_t> ids = { 1, 2 };
    Check(TierOf(Cand(1, kFromSight, 0, 0, 'h', 0, 'b'), ids) == kTierAttacker, "a seen boss is promoted to tier 1");
    Check(TierOf(Cand(1, kFromSight, 0, 0, 'h', 'g', 'b'), ids) == kTierAttacker, "a boss guard still tier 1");
    Check(TierOf(Cand(1, kFromSight, 0, 0, 'h', 'g'), ids) == kTierOther, "a seen guard is tier 4");
    Check(TierOf(Cand(1, kFromAttackers, 0, 0, 'h', 'k'), ids) == kTierAttacker, "an attacking critter keeps tier 1");
    Check(TierOf(Cand(1, kFromSight, 0, 0, 'n', 0, 0, 3), ids) == kTierNeutral, "a victim outside the run does not promote");
    Check(TierOf(Cand(1, kFromSight, 0, 0, 'f'), ids) == kTierNeutral, "friendly reaction is not hostile");
}

void TestCaps()
{
    Check(PerRunCap(1, 5) == 48, "party cap 48");
    Check(PerRunCap(1, 6) == 64, "raid cap 64 above 5 members");
    Check(PerRunCap(8, 5) == 48, "8 parties fit the budget at 48");
    Check(PerRunCap(9, 5) == 42, "9 parties share the budget: 384/9 = 42");
    Check(PerRunCap(16, 5) == 24, "16 parties share the budget: 384/16 = 24");
    Check(PerRunCap(16, 40) == 24, "16 raids share the budget: 384/16 = 24");
    Check(PerRunCap(10, 40) == 38, "10 raids get 38");
    Check(PerRunCap(32, 5) == 12, "32 runs get 12");
    Check(PerRunCap(100, 5) == kMinMobsPerRun, "floor 8");
    Check(PerRunCap(0, 5) == 48, "no runs: the normal cap");
    for (size_t runs = 1; runs <= kMaxRunsPerCycle; ++runs)
        Check(runs * PerRunCap(runs, 40) <= kMaxMobsPerCycle, "the cycle total stays within kMaxMobsPerCycle");
    Check(CandidateBudget(48) == 192, "candidate budget is 4x the cap");
}

void TestRotation()
{
    std::vector<size_t> w = RotationWindow(5, 3);
    Check(w.size() == 5 && w[0] == 0 && w[4] == 4, "few runs: all, in order");
    Check(RotationWindow(0, 0).empty(), "no runs: none");
    std::vector<bool> seen(70, false);
    size_t offset = 0;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        std::vector<size_t> win = RotationWindow(70, offset);
        Check(win.size() == kMaxRunsPerCycle, "70 runs: 32 per cycle");
        for (size_t i : win)
            seen[i] = true;
        offset += kMaxRunsPerCycle;
    }
    bool all = true;
    for (bool s : seen)
        all = all && s;
    Check(all, "every run takes its turn within ceil(70/32) cycles");
    std::vector<size_t> wrap = RotationWindow(40, 30);
    Check(wrap.front() == 30 && wrap[9] == 39 && wrap[10] == 0 && wrap.back() == 21, "the window wraps");
}

void TestSightReaders()
{
    std::vector<MemberInfo> party = { Member(1, 0, 0, false), Member(2, 0, 0), Member(3, 0, 0), Member(4, 0, 0) };
    std::vector<size_t> p = PickSightReaders(party, 7);
    Check(p.size() == 3 && p[0] == 1, "a party: every bot reads, a player never");

    std::vector<MemberInfo> raid;
    for (uint32_t i = 0; i < 40; ++i)
        raid.push_back(Member(i, static_cast<float>(i * 10), 0, i != 0, i == 17 || i == 33));
    std::vector<size_t> r = PickSightReaders(raid, 0);
    Check(r.size() == kSightReaders, "a raid: at most 10 readers");
    Check(r.size() >= 2 && r[0] == 17 && r[1] == 33, "tanks first");
    // centre x = 195: member 19 or 20 is nearest (190 vs 200: 19 is 5 away, 20 is 5 away; the first wins).
    Check(r.size() >= 3 && r[2] == 19, "then the bot nearest the centre");
    bool player = false;
    for (size_t i : r)
        player = player || i == 0;
    Check(!player, "a player never reads");
    std::vector<size_t> r2 = PickSightReaders(raid, 5);
    Check(r2.size() == kSightReaders && r2[3] != r[3], "the rest take turns by cycle");
    std::vector<MemberInfo> allPlayers(8, Member(1, 0, 0, false));
    Check(PickSightReaders(allPlayers, 1).empty(), "no bots: no readers");
}

void TestJson()
{
    RunInfo r;
    r.map = 389;
    r.instance = 7;
    r.members.push_back(Member(1234, 0, 0));
    r.members[0].id = 17370988236253874177ull;
    r.members[0].target = 17370988236253874178ull;
    r.members.push_back(Member(1235, 0, 0));
    r.members[1].id = 17370988236253874179ull;
    r.mobsReported = true;
    r.mobsTotal = 57;
    MobInfo m;
    m.id = 17370988236253874178ull;
    m.entry = 4423;
    m.name = "Ra\"ider\\";
    m.hpPct = 50;
    m.x = 1.25f;
    m.y = -3.0f;
    m.victim = 17370988236253874177ull;
    m.reaction = 'h';
    r.mobs.push_back(m);
    std::vector<RunInfo> runs = { r };
    std::vector<std::string> p = PackRuns(12, 99, 5, 1700000000, runs);
    Check(p.size() == 1, "one run: one datagram");
    std::string const& s = p.empty() ? std::string() : p[0];
    Check(s.rfind("{\"v\":12,\"session\":99,\"seq\":5,\"ts\":1700000000,\"type\":\"DUNGEON_MOBS\",\"part\":0,\"parts\":1,\"runs\":[", 0) == 0,
        "header fields in order");
    Check(Has(s, "{\"map\":389,\"instance\":7,\"raid\":false,\"members\":["), "run head");
    Check(Has(s, "{\"guid\":1234,\"id\":\"17370988236253874177\",\"target\":\"17370988236253874178\"}"),
        "member id and target are decimal strings");
    Check(Has(s, "{\"guid\":1235,\"id\":\"17370988236253874179\"}"), "no target: the key is left out");
    Check(Has(s, "\"mobs_map\":389,\"mobs_total\":57,\"mobs\":[{\"id\":\"17370988236253874178\""), "mob list head");
    Check(Has(s, "\"victim\":\"17370988236253874177\""), "victim is a decimal string");
    Check(Has(s, "\"name\":\"Ra\\\"ider\\\\\""), "names are escaped");
    Check(Has(s, "\"x\":1.2,\"y\":-3.0") || Has(s, "\"x\":1.3,\"y\":-3.0"), "positions with one decimal");
    Check(s.size() >= 2 && s.compare(s.size() - 3, 3, "}]}") == 0, "closed");

    RunInfo empty = r;
    empty.mobs.clear();
    empty.mobsTotal = 0;
    std::string const e = BuildRunJson(empty);
    Check(Has(e, "\"mobs_total\":0,\"mobs\":[]}"), "read and empty: \"mobs\":[]");
    RunInfo unread = r;
    unread.mobsReported = false;
    std::string const u = BuildRunJson(unread);
    Check(!Has(u, "\"mobs\"") && !Has(u, "mobs_map") && !Has(u, "mobs_total"), "not read: no mob keys");
    RunInfo raid = r;
    raid.raid = true;
    Check(Has(BuildRunJson(raid), "\"raid\":true"), "raid flag");

    std::vector<std::string> end = PackRuns(12, 99, 6, 1700000002, {});
    Check(end.size() == 1 && end[0] ==
        "{\"v\":12,\"session\":99,\"seq\":6,\"ts\":1700000002,\"type\":\"DUNGEON_MOBS\",\"part\":0,\"parts\":1,\"runs\":[]}",
        "no runs: the end marker");
}

void TestWriterCaps()
{
    RunInfo big = Run(409, 3, 60, 70, false);
    std::string const s = BuildRunJson(big);
    Check(Count(s, "{\"guid\":") == kMaxMembersPerRun, "members capped at 40 in the writer");
    Check(Count(s, "\"entry\":") == 64, "mobs capped at 64 in the writer");
}

void TestPacking()
{
    // 8-run limit: 20 small runs -> 3 parts of 8, 8, 4.
    std::vector<RunInfo> runs;
    for (uint32_t i = 0; i < 20; ++i)
        runs.push_back(Run(36, i + 1, 5, 2, false));
    std::vector<std::string> p = PackRuns(12, 1, 2, 3, runs);
    Check(p.size() == 3, "20 small runs: 3 parts (8 runs per part)");
    if (p.size() == 3)
    {
        Check(Count(p[0], "\"instance\":") == 8 && Count(p[2], "\"instance\":") == 4, "parts of 8, 8, 4 runs");
        Check(Has(p[0], "\"part\":0,\"parts\":3") && Has(p[2], "\"part\":2,\"parts\":3"), "part numbering");
    }

    // 96-mob limit: 2 party runs of 48 -> 2 + 1 (96 fits, the third would pass it).
    runs.clear();
    for (uint32_t i = 0; i < 3; ++i)
        runs.push_back(Run(36, i + 1, 5, 48, false));
    p = PackRuns(12, 1, 2, 3, runs);
    Check(p.size() == 2 && Count(p[0], "\"instance\":") == 2, "96 mobs per part: 2 runs of 48, then a new part");

    // Byte limit: worst-case runs pass 32000 bytes before 96 mobs; every part stays under it, no run is split.
    runs.clear();
    for (uint32_t i = 0; i < 8; ++i)
        runs.push_back(Run(409, i + 1, 40, 12, true));
    p = PackRuns(12, 18446744073709551615ull, 18446744073709551615ull, 9223372036854775807ll, runs);
    size_t listed = 0;
    bool under = true;
    for (std::string const& s : p)
    {
        under = under && s.size() <= kMaxPayloadBytes;
        listed += Count(s, "\"instance\":");
    }
    Check(p.size() >= 2, "worst-case strings: the byte limit splits the parts");
    Check(under, "every part <= 32000 bytes");
    Check(listed == 8, "every run sent exactly once (whole runs)");

    // Worst single runs.
    std::string const worstRaid = BuildRunJson(Run(409, 1, 40, 64, true));
    std::string const plainRaid = BuildRunJson(Run(409, 1, 40, 64, false));
    std::string const plainParty = BuildRunJson(Run(36, 1, 5, 48, false));
    std::printf("worst raid run (40 members, 64 mobs, escaped strings): %zu bytes\n", worstRaid.size());
    std::printf("plain raid run (40 members, 64 mobs): %zu bytes\n", plainRaid.size());
    std::printf("plain party run (5 members, 48 mobs): %zu bytes\n", plainParty.size());
    Check(worstRaid.size() < 65507, "the worst single run stays below the UDP limit (a run is never split)");
    Check(plainRaid.size() < 18000, "a realistic raid run < 18 KB");

    // Worst cycle by total bytes: 32 runs at their shared cap, worst strings.
    runs.clear();
    size_t const cap = PerRunCap(kMaxRunsPerCycle, 40);
    for (uint32_t i = 0; i < kMaxRunsPerCycle; ++i)
        runs.push_back(Run(409, i + 1, 40, cap, true));
    p = PackRuns(12, 1, 2, 3, runs);
    size_t biggest = 0, total = 0;
    for (std::string const& s : p)
    {
        biggest = std::max(biggest, s.size());
        total += s.size();
    }
    std::printf("worst cycle by total (32 raid runs x %zu mobs): %zu parts, largest %zu bytes, %zu bytes in all\n", cap, p.size(), biggest, total);
    Check(biggest <= kMaxPayloadBytes, "worst cycle: every multi-run part <= 32000 bytes");

    // Worst cycle by part size: 6 raid runs at the full 64 cap (the same 384 mobs), worst strings. Whole runs only, so each
    // pathological run goes out alone and passes the 32000 packing target while staying under the UDP limit.
    runs.clear();
    for (uint32_t i = 0; i < kMaxMobsPerCycle / kRaidMobCap; ++i)
        runs.push_back(Run(409, i + 1, 40, kRaidMobCap, true));
    p = PackRuns(12, 1, 2, 3, runs);
    biggest = 0;
    total = 0;
    for (std::string const& s : p)
    {
        biggest = std::max(biggest, s.size());
        total += s.size();
    }
    std::printf("worst cycle by part (6 raid runs x 64 mobs): %zu parts, largest %zu bytes, %zu bytes in all\n", p.size(), biggest, total);
    Check(biggest < 65507, "worst cycle: every part below the UDP limit");
}

} // namespace

int main()
{
    TestSelectionOrder();
    TestCaps();
    TestRotation();
    TestSightReaders();
    TestJson();
    TestWriterCaps();
    TestPacking();
    if (g_failures)
    {
        std::fprintf(stderr, "test_dungeon_mob_telemetry: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("test_dungeon_mob_telemetry: all checks passed\n");
    return 0;
}
