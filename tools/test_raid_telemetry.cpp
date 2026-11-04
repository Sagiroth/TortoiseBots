// Standalone tests for the Raids tab feed (spec dash-raids-tab, worker A): the RAIDS JSON shape (the offline row's name and
// flag, the bound group's fight key and the unbound "raid#N" key, the raw combat window), the group partition into parts
// (whole groups only, one part carries no part fields), the cut ladder (combat, then offline names, then members, in that
// order), the qualification rule (a raid map and an instance copy, no size and no group-type threshold) and the rotation.
//
// The header includes the standard library only, so this file compiles it without a game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_raid_telemetry.cpp -o /tmp/test_raid_telemetry
//   /tmp/test_raid_telemetry

#include "../runtime/RaidTelemetry.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TortoiseBots::RaidTelemetry;

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

MemberRef Member(uint32_t guid, int sub, bool online, std::string name = "")
{
    MemberRef m;
    m.guid = guid;
    m.sub = sub;
    m.online = online;
    m.name = std::move(name);
    return m;
}

GroupInfo Group(uint32_t id, uint32_t map = 409, uint32_t instance = 7)
{
    GroupInfo g;
    g.groupId = id;
    g.map = map;
    g.instance = instance;
    g.raidGroup = true;
    g.leader = "Celindra";
    g.leaderGuid = 4567;
    return g;
}

std::vector<GroupInfo const*> Refs(std::vector<GroupInfo> const& groups)
{
    std::vector<GroupInfo const*> out;
    for (GroupInfo const& g : groups)
        out.push_back(&g);
    return out;
}

// A window-shaped raw JSON string of `bytes` bytes (it is copied verbatim, the module never parses it).
std::string Window(size_t bytes)
{
    std::string s = "{\"w\":\"";
    while (s.size() + 2 < bytes)
        s += 'x';
    s += "\"}";
    return s;
}

// T-A1: the exact object of one group; the offline member carries its name and "off", the online one neither.
void TestPayloadShape()
{
    std::vector<GroupInfo> groups{ Group(123) };
    groups[0].members.push_back(Member(4567, 0, true));
    groups[0].members.push_back(Member(4570, 1, false, "Alez"));
    std::string const got = BuildRaidsPayload(12, 77, 5, 1700, 1700000, Refs(groups));
    std::string const want =
        "{\"v\":12,\"session\":77,\"seq\":5,\"ts\":1700,\"ts_ms\":1700000,\"type\":\"RAIDS\",\"raids\":["
        "{\"group\":123,\"key\":\"raid#123\",\"map\":409,\"instance\":7,\"rg\":true,\"leader\":\"Celindra\","
        "\"leader_guid\":4567,\"members\":[{\"g\":4567,\"s\":0},{\"g\":4570,\"s\":1,\"n\":\"Alez\",\"off\":true}]}]}";
    Check(got == want, "T-A1 exact payload shape");
    Check(!Has(got, "\"n\":\"Celindra\""), "T-A1 the leader is not a member row");
}

// T-A2: a bound group's key is its commander name and it names the commander; an unbound group carries neither.
void TestBoundKey()
{
    std::vector<GroupInfo> bound{ Group(123) };
    bound[0].commander = "Hypeer";
    bound[0].members.push_back(Member(4567, 0, true));
    std::string const b = BuildRaidsPayload(12, 1, 2, 3, 4, Refs(bound));
    Check(Has(b, "\"key\":\"Hypeer\",\"map\":409"), "T-A2 the bound key is the commander name");
    Check(Has(b, "\"leader_guid\":4567,\"commander\":\"Hypeer\""), "T-A2 the bound row names the commander");

    std::vector<GroupInfo> loose{ Group(123) };
    loose[0].members.push_back(Member(4567, 0, true));
    std::string const u = BuildRaidsPayload(12, 1, 2, 3, 4, Refs(loose));
    Check(Has(u, "\"key\":\"raid#123\""), "T-A2 the unbound key is raid#<id>");
    Check(!Has(u, "\"commander\""), "T-A2 an unbound row carries no commander");
    Check(!Has(u, "\"key\":\"Celindra\""), "T-A2 the key never falls back to the leader name");
}

// T-A3: the window goes out verbatim; an empty window adds no key.
void TestCombatRaw()
{
    std::vector<GroupInfo> groups{ Group(9) };
    groups[0].combatJson = "{\"win_s\":10}";
    Check(Has(BuildRaidsPayload(12, 1, 2, 3, 4, Refs(groups)), ",\"combat\":{\"win_s\":10}"), "T-A3 combat verbatim");

    groups[0].combatJson.clear();
    Check(!Has(BuildRaidsPayload(12, 1, 2, 3, 4, Refs(groups)), "\"combat\""), "T-A3 no combat key when empty");
}

// T-A4: three heavy groups split into two parts, never splitting a group; one light group is a single part with no part fields.
void TestPartition()
{
    std::vector<GroupInfo> groups;
    for (uint32_t g = 1; g <= 3; ++g)
    {
        GroupInfo info = Group(100 + g);
        for (uint32_t m = 0; m < 40; ++m)
            info.members.push_back(Member(1000 + g * 100 + m, static_cast<int>(m % 8), true));
        info.combatJson = Window(4800);
        groups.push_back(info);
    }
    std::vector<std::string> const parts = BuildRaidsParts(12, 1, 2, 3, 4, groups);
    Check(parts.size() == 2, "T-A4 three heavy groups need two parts");
    size_t seen = 0;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        Check(parts[i].size() <= kRaidPartByteCap, "T-A4 every part stays under the byte cap");
        Check(Has(parts[i], "\"part\":" + std::to_string(i) + ",\"parts\":2"), "T-A4 part fields present");
        size_t const n = Count(parts[i], "\"group\":");
        Check(n >= 1, "T-A4 a part holds at least one group");
        seen += n;
        // A group that were split would leave an unterminated object: every part ends the raids array.
        Check(parts[i].size() >= 3 && parts[i].compare(parts[i].size() - 2, 2, "]}") == 0, "T-A4 part is a whole object");
    }
    Check(seen == 3, "T-A4 all three groups are sent once");

    std::vector<GroupInfo> const small{ Group(7) };
    std::vector<std::string> const one = BuildRaidsParts(12, 1, 2, 3, 4, small);
    Check(one.size() == 1, "T-A4 one small group is one part");
    Check(!Has(one[0], ",\"part\":"), "T-A4 a single part carries no part fields");
}

// T-A5: the ladder drops combat first, then offline names, then members; `more` counts the cut members.
void TestLadder()
{
    std::vector<GroupInfo> big{ Group(5) };
    big[0].members.push_back(Member(1, 0, true));
    big[0].combatJson = Window(20000);
    std::vector<TrimEvent> trims;
    std::vector<std::string> const fit = BuildRaidsParts(12, 1, 2, 3, 4, big, &trims);
    Check(fit.size() == 1, "T-A5 a single group stays one part");
    Check(fit[0].size() <= kRaidPartByteCap, "T-A5 the trimmed group fits");
    Check(Has(fit[0], "\"trimmed\":\"combat\""), "T-A5 the window is what makes it too big");
    Check(!Has(fit[0], "\"combat\":"), "T-A5 the trimmed window is gone");
    Check(trims.size() == 1 && trims[0].level == 1, "T-A5 the trim is logged");

    // Combat goes before the offline names: a cap that already fits after dropping the names must keep all 40 members.
    std::vector<GroupInfo> names{ Group(6) };
    for (uint32_t m = 0; m < 40; ++m)
        names[0].members.push_back(Member(2000 + m, static_cast<int>(m % 8), false, "ABCDEFGHIJKLMNOPQRSTUVWX"));
    names[0].combatJson = Window(200);
    std::vector<std::string> const l2 = BuildRaidsParts(12, 1, 2, 3, 4, names, nullptr, 1500);
    Check(l2.size() == 1, "T-A5 names level: one part");
    Check(Has(l2[0], "\"trimmed\":\"names\""), "T-A5 combat is dropped before the offline names");
    Check(Count(l2[0], "\"g\":") == 40, "T-A5 no member is cut at the names level");
    Check(!Has(l2[0], "\"n\":\""), "T-A5 the offline names are gone");
    Check(!Has(l2[0], "\"more\""), "T-A5 nothing to count at the names level");

    // A cap that the names level cannot meet cuts members from the end, counted in "more".
    std::vector<std::string> const l3 = BuildRaidsParts(12, 1, 2, 3, 4, names, nullptr, 550);
    Check(l3.size() == 1, "T-A5 members level: one part");
    Check(l3[0].size() <= 550, "T-A5 the cut group fits the lowered cap");
    Check(Has(l3[0], "\"trimmed\":\"members\""), "T-A5 the members are the last thing cut");
    size_t const kept = Count(l3[0], "\"g\":");
    Check(kept < 40 && kept >= 1, "T-A5 some members are cut, some kept");
    Check(Has(l3[0], "\"more\":" + std::to_string(40 - kept)), "T-A5 more counts the cut members");
}

// T-A6: a raid map copy with a member in the world, outside a battleground; no size and no group-type threshold.
void TestQualification()
{
    Check(Qualifies(true, false, true, 7), "T-A6 a raid map copy qualifies");
    Check(!Qualifies(false, false, true, 7), "T-A6 not in the world: no");
    Check(!Qualifies(true, true, true, 7), "T-A6 a battleground copy: no");
    Check(!Qualifies(true, false, false, 7), "T-A6 a dungeon map: no");
    Check(!Qualifies(true, false, true, 0), "T-A6 the open world copy: no");

    std::vector<GroupInfo> party{ Group(77) };
    party[0].raidGroup = false;
    party[0].members.push_back(Member(1, 0, true));
    party[0].members.push_back(Member(2, 1, true));
    std::string const p = BuildRaidsPayload(12, 1, 2, 3, 4, Refs(party));
    Check(Has(p, "\"rg\":false"), "T-A6 a plain party on a raid map is listed as a group");
}

// T-A7: 45 groups take two cycles of 40 and every group has had its turn.
void TestRotation()
{
    std::vector<size_t> const a = RotationWindow(45, 0);
    std::vector<size_t> const b = RotationWindow(45, 40);
    Check(a.size() == kMaxRaidGroupsPerCycle && b.size() == kMaxRaidGroupsPerCycle, "T-A7 windows of 40");
    std::vector<bool> covered(45, false);
    for (size_t i : a)
        covered[i] = true;
    for (size_t i : b)
        covered[i] = true;
    size_t all = 0;
    for (bool c : covered)
        all += c ? 1 : 0;
    Check(all == 45, "T-A7 two cycles cover all 45 groups");
    Check(RotationWindow(3, 9).size() == 3, "T-A7 a small set is sent whole");
}

// T-A8: nothing qualifies -> one end-marker payload.
void TestEndMarker()
{
    std::string const empty = BuildRaidsPayload(12, 1, 2, 3, 4, {});
    Check(Has(empty, "\"type\":\"RAIDS\",\"raids\":[]"), "T-A8 the end marker is an empty raids list");
    Check(!Has(empty, "\"part\":"), "T-A8 the end marker carries no part fields");
    Check(BuildRaidsParts(12, 1, 2, 3, 4, {}).size() == 1, "T-A8 one end-marker datagram");
}

// T-A9: the G2 detail goes last, after every G0/G1 key, and is dropped by
// the level-1 trim with the window.
void TestDetailTail()
{
    std::vector<GroupInfo> groups{ Group(9) };
    groups[0].combatJson = "{\"win_s\":10}";
    DetailEntry d;
    d.guid = 1;
    d.hasReady = true;
    d.readyBuffs = 3;
    d.readyCons = 5;
    d.cdReady = 2;
    d.cdKnown = 3;
    TortoiseBots::CommanderTelemetry::AuraInfo aura;
    aura.id = 100;
    aura.name = "Burn";
    d.debuffs.push_back(aura);
    d.mc = true;
    groups[0].detail.push_back(d);
    std::string const got = BuildRaidsPayload(12, 1, 2, 3, 4, Refs(groups));
    size_t const combatAt = got.find(",\"combat\":");
    size_t const detailAt = got.find(",\"detail\":[");
    Check(combatAt != std::string::npos && detailAt != std::string::npos && detailAt > combatAt,
        "T-A9 the detail is appended after the window");
    Check(Has(got, "\"g\":1,\"rdy\":{\"b\":3,\"c\":5,\"cr\":2,\"cn\":3},\"db\":[{\"id\":100,\"n\":\"Burn\"}],\"mc\":1"),
        "T-A9 the detail entry shape");
    Check(Has(BuildGroupJson(groups[0], 1), ",\"combat\":") == false, "T-A9 level 1 drops the window");
    Check(Has(BuildGroupJson(groups[0], 1), "\"detail\"") == false, "T-A9 level 1 drops the detail with it");
}

} // namespace

int main()
{
    TestPayloadShape();
    TestBoundKey();
    TestCombatRaw();
    TestPartition();
    TestLadder();
    TestQualification();
    TestRotation();
    TestEndMarker();
    TestDetailTail();
    if (g_failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_raid_telemetry: all checks passed\n");
    return 0;
}
