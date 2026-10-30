// Standalone tests for the combat monitor policy (raid support A8a):
// per-fight casts, damage, healing, deaths and debuff uptime of one bound group.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_combat_monitor_policy.cpp -o /tmp/raid_combat
//   /tmp/raid_combat

#include "../runtime/CombatMonitorPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace TortoiseBots::CombatMonitor;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static bool Contains(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

static size_t Count(std::string const& hay, std::string const& needle)
{
    size_t n = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos)
    {
        ++n;
        ++pos;
    }
    return n;
}

static std::vector<MemberInfo> Trio()
{
    return {
        {1, "Tanky", "warrior", Role::Tank, -1},
        {2, "Healy", "priest", Role::Healer, -1},
        {3, "Magey", "mage", Role::Damage, -1},
    };
}

static FightContext BossCtx()
{
    FightContext c;
    c.plan = "molten_core";
    c.leg = "l1";
    c.kind = "boss";
    c.step = 3;
    c.bossEntry = 11502;
    c.bossName = "Magmadar";
    c.planned = {11502};
    return c;
}

// One take per payload: the seq callback runs only when a payload goes out.
struct Seq
{
    uint64_t next = 1;
    uint64_t calls = 0;
    std::function<uint64_t()> fn()
    {
        return [this]() { ++calls; return next++; };
    }
};

static void TestNonMemberIgnored()
{
    Book b;
    b.SetMembers(Trio());
    b.OnCast(99, "Fireball", false, 1000);
    b.OnDamage(0, 0, 500, "Mob", 11502, 777, "Mob", 1000);
    b.OnHeal(99, 100, 0, 1000);
    b.OnDeath(99, "Mob", 0.0f, 0.0f, 0.0f, 1000);
    b.SampleDebuffs({"sunder"});
    b.SetInCombat(false, false, 2000);
    CHECK(!b.InFight());
    Seq seq;
    CHECK(b.TakeFightPayloads(1, 9, seq.fn(), 1700000000, "Cmdr", 123, 55, 409).empty());
    CHECK(seq.calls == 0);
    CHECK(!b.IsMember(99));
    CHECK(b.IsMember(1) && b.IsMember(2) && b.IsMember(3));
}

static void TestFightLifecycle()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 1);
    CHECK(b.InFight());
    for (int i = 0; i < 4; ++i)
        b.OnCast(1, "Sunder Armor", false, (uint32_t)(1000 + i));
    for (int i = 0; i < 2; ++i)
        b.OnCast(1, "Heroic Strike", false, (uint32_t)(2000 + i));
    for (int i = 0; i < 3; ++i)
        b.OnCast(2, "Heal", true, (uint32_t)(3000 + i));
    for (int i = 0; i < 5; ++i)
        b.OnCast(3, "Fireball", false, (uint32_t)(4000 + i));
    b.OnDamage(3, 0, 5000, "Magey", 11502, 777, "Magmadar", 5000);
    b.OnDamage(1, 0, 1200, "Tanky", 11502, 777, "Magmadar", 6000);
    b.OnDamage(0, 1, 3000, "Magmadar", 11502, 777, "Magmadar", 7000);
    b.OnDamage(3, 1, 100, "Magey", 0, 0, "", 8000);
    b.OnHeal(2, 2500, 500, 9000);
    b.NoteBossDied(11501);
    b.SetInCombat(false, false, 20000);
    CHECK(b.InFight());
    b.NoteBossDied(11502);
    b.SetInCombat(false, false, 23000);
    CHECK(!b.InFight());

    Seq seq;
    seq.next = 100;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700023000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(seq.next == 101);
    CHECK(seq.calls == 1);
    CHECK(Contains(p[0], "\"id\":\"55-1\""));
    CHECK(Contains(p[0], "\"outcome\":\"kill\""));
    CHECK(Contains(p[0], "\"dur_ms\":20000"));
    CHECK(Contains(p[0], "\"dmg\":5000"));
    CHECK(!Contains(p[0], "\"dmg\":5100"));
    CHECK(Contains(p[0], "\"taken\":3000"));
    CHECK(Contains(p[0], "{\"n\":\"Sunder Armor\",\"c\":4}"));
    CHECK(Contains(p[0], "\"heal\":2500"));
    CHECK(Contains(p[0], "\"over\":500"));
    CHECK(Contains(p[0], "\"e\":11502"));
    CHECK(Contains(p[0], "\"c\":1}"));
    CHECK(Contains(p[0], "\"role\":\"tank\""));
    CHECK(Contains(p[0], "\"role\":\"healer\""));
    CHECK(Contains(p[0], "\"role\":\"dps\""));

    std::vector<std::string> again = b.TakeFightPayloads(1, 9, seq.fn(), 1700023000, "Cmdr", 123, 55, 409);
    CHECK(again.size() == 1);
    CHECK(Contains(again[0], "\"id\":\"55-1\""));
    CHECK(seq.next == 102);
    CHECK(seq.calls == 2);
    CHECK(b.TakeFightPayloads(1, 9, seq.fn(), 1700023000, "Cmdr", 123, 55, 409).empty());
    CHECK(seq.next == 102);
    CHECK(seq.calls == 2);
}

static void TestShortFightDropped()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 1);
    b.OnDamage(3, 0, 500, "Magey", 11502, 777, "Magmadar", 500);
    b.SetInCombat(true, false, 1000);
    b.SetInCombat(false, false, 4000);
    CHECK(b.InFight());
    b.SetInCombat(false, false, 7000);
    CHECK(!b.InFight());
    Seq seq;
    CHECK(b.TakeFightPayloads(1, 9, seq.fn(), 1700007000, "Cmdr", 123, 55, 409).empty());
    CHECK(seq.next == 1);
    CHECK(seq.calls == 0);
}

static void TestWipe()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 1);
    for (int i = 0; i < 7; ++i)
        b.OnDamage(0, 1, (uint32_t)(10 * (i + 1)), "Magmadar", 11502, 777, "Magmadar", (uint32_t)(1000 + i));
    b.OnDamage(0, 2, 500, "Magmadar", 11502, 777, "Magmadar", 2000);
    b.OnDamage(0, 3, 700, "Magmadar", 11502, 777, "Magmadar", 2000);
    b.OnDeath(1, "Magmadar", 1.0f, 2.0f, 3.0f, 6000);
    b.OnDeath(2, "Magmadar", 4.0f, 5.0f, 6.0f, 7000);
    CHECK(b.InFight());
    b.OnDeath(3, "Magmadar", 7.0f, 8.0f, 9.0f, 8000);
    CHECK(!b.InFight());
    b.SetInCombat(false, true, 9000);

    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"outcome\":\"wipe\""));
    CHECK(Count(p[0], "\"killer\":\"Magmadar\"") == 3);
    CHECK(Contains(p[0], "\"x\":1.0"));
    size_t h30 = p[0].find("\"d\":30,");
    size_t h40 = p[0].find("\"d\":40,");
    size_t h50 = p[0].find("\"d\":50,");
    size_t h60 = p[0].find("\"d\":60,");
    size_t h70 = p[0].find("\"d\":70,");
    CHECK(h30 != std::string::npos && h40 != std::string::npos && h50 != std::string::npos);
    CHECK(h60 != std::string::npos && h70 != std::string::npos);
    CHECK(h30 < h40 && h40 < h50 && h50 < h60 && h60 < h70);
    CHECK(!Contains(p[0], "\"d\":10,"));
    CHECK(!Contains(p[0], "\"d\":20,"));
}

static void TestDebuffUptime()
{
    CHECK((DebuffKeys("Sunder Armor", 5) == std::vector<std::string>{"sunder", "sunder5"}));
    CHECK((DebuffKeys("Sunder Armor", 3) == std::vector<std::string>{"sunder"}));
    CHECK((DebuffKeys("Faerie Fire (Feral)", 1) == std::vector<std::string>{"faerie_fire"}));
    CHECK((DebuffKeys("Faerie Fire", 1) == std::vector<std::string>{"faerie_fire"}));
    CHECK(DebuffKeys("Fireball", 1).empty());
    CHECK((DebuffKeys("Curse of Recklessness", 1) == std::vector<std::string>{"cor"}));
    CHECK((DebuffKeys("Demoralizing Roar", 1) == std::vector<std::string>{"demo"}));
    CHECK(!AlwaysListed("Fireball"));
    CHECK(AlwaysListed("Sunder Armor") && AlwaysListed("Maul") && AlwaysListed("Hunter's Mark"));

    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 1);
    for (int i = 0; i < 6; ++i)
        b.SampleDebuffs({"sunder", "sunder5", "faerie_fire"});
    for (int i = 0; i < 4; ++i)
        b.SampleDebuffs({"faerie_fire"});
    b.SetInCombat(true, false, 1000);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"sampled_s\":10"));
    CHECK(Contains(p[0], "\"sunder\":6"));
    CHECK(Contains(p[0], "\"sunder5\":6"));
    CHECK(Contains(p[0], "\"faerie_fire\":10"));
}

static void TestWindow()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 1);
    b.OnDamage(3, 0, 9000, "Magey", 11502, 777, "Magmadar", 1000);
    b.OnDamage(3, 0, 3000, "Magey", 11502, 777, "Magmadar", 15000);
    std::string w = b.WindowJson(20000);
    CHECK(!w.empty());
    CHECK(Contains(w, "\"win_s\":10"));
    CHECK(Contains(w, "\"dps\":300"));
    b.SetInCombat(false, false, 20000);
    CHECK(!b.WindowJson(21000).empty());
    b.SetInCombat(false, false, 23000);
    CHECK(b.WindowJson(24000).empty());
}

static void TestWindowPrune()
{
    // Per-event window vectors drop entries older than the window, on append and on read.
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 1);
    for (int i = 0; i < 10; ++i)
    {
        b.OnCast(3, "Fireball", false, (uint32_t)(1000 + i));
        b.OnDamage(3, 0, 100, "Magey", 11502, 777, "Magmadar", (uint32_t)(1000 + i));
        b.OnHeal(2, 100, 0, (uint32_t)(1000 + i));
    }
    CHECK(b.WindowEventCount() == 30);
    std::string w = b.WindowJson(30000);
    // Every event is older than the 10 s window: none counts, and the read pruned the vectors.
    CHECK(b.WindowEventCount() == 0);
    CHECK(!Contains(w, "\"g\":"));
    b.OnCast(3, "Fireball", false, 30000);
    b.OnDamage(3, 0, 100, "Magey", 11502, 778, "Add", 30000);
    b.OnHeal(2, 100, 0, 30000);
    w = b.WindowJson(30000);
    CHECK(Contains(w, "\"c\":1"));
    // Deaths cap at 200 per fight and engaged creatures at 500 (counted past the cap as dropped).
    Book c;
    c.SetMembers(Trio());
    c.Start(0, 1700000000, BossCtx(), 2);
    for (int i = 0; i < 250; ++i)
        c.OnDeath(1, "Mob", 0.0f, 0.0f, 0.0f, (uint32_t)(1000 + i));
    for (uint32_t e = 0; e < 600; ++e)
        c.OnDamage(3, 0, 1, "Magey", 20000 + e, (uint64_t)(1000 + e), "Mob", 2000);
    c.SetInCombat(true, false, 1000);
    c.SetInCombat(false, false, 6000);
    c.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = c.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Count(p[0], "\"t_ms\":") == 200);
    CHECK(Contains(p[0], "\"dropped\":100"));
}

static void TestBudget()
{
    Book b;
    std::vector<MemberInfo> m;
    for (int i = 0; i < 40; ++i)
        m.push_back({(uint32_t)(i + 1), "Bot" + std::to_string(i), "mage", Role::Damage, i / 5});
    m[0].role = Role::Tank;
    m[0].cls = "warrior";
    m[0].name = "Tanky";
    b.SetMembers(m);
    b.Start(0, 1700000000, BossCtx(), 1);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < 20; ++j)
            b.OnCast((uint32_t)(i + 1), "Spell" + std::to_string(i) + "_" + std::to_string(j), false, 1500);
    b.OnCast(1, "Sunder Armor", false, 2000);
    b.SetInCombat(true, false, 1000);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(p[0].size() < kPayloadByteBudget);
    CHECK(Contains(p[0], "{\"n\":\"Sunder Armor\",\"c\":1}"));
    CHECK(Count(p[0], "\"name\":\"Bot") == 39);
}

// Codex r3: sizes are measured with seq 0; the widest seq (UINT64_MAX, 20 digits)
// must not push a queued payload over the budget. Sweep one spell name's length so
// the full payload crosses the budget byte by byte.
static Book NearBudgetFight(size_t pad)
{
    Book b;
    std::vector<MemberInfo> m;
    for (int i = 0; i < 40; ++i)
        m.push_back({(uint32_t)(i + 1), "Bot" + std::to_string(i), "mage", Role::Damage, i / 5});
    b.SetMembers(m);
    b.Start(0, 1700000000, BossCtx(), 1);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < 20; ++j)
            b.OnCast((uint32_t)(i + 1), "Spell" + std::to_string(i) + "_" + std::to_string(j), false, 1500);
    for (int k = 0; k < 5; ++k)   // the most cast spell, so it is always in the bot's list
        b.OnCast(1, std::string(pad, 'p'), false, (uint32_t)(1600 + k));
    b.SetInCombat(true, false, 1000);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    return b;
}

static void TestSeqWidthStaysInBudget()
{
    auto maxSeq = []() { return std::numeric_limits<uint64_t>::max(); };
    Book probe = NearBudgetFight(0);
    std::vector<std::string> p0 = probe.TakeFightPayloads(1, 9, maxSeq, 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p0.size() == 1);
    size_t const base = p0[0].size();
    CHECK(base < kPayloadByteBudget);
    size_t const target = kPayloadByteBudget - base;   // pad that lands the payload on the budget
    bool sawCut = false, sawFull = false;
    for (size_t pad = target > 40 ? target - 40 : 0; pad <= target + 40; ++pad)
    {
        Book b = NearBudgetFight(pad);
        std::vector<std::string> p = b.TakeFightPayloads(1, 9, maxSeq, 1700009000, "Cmdr", 123, 55, 409);
        CHECK(p.size() == 1);
        CHECK(p[0].size() <= kPayloadByteBudget);
        bool const full = p[0].size() == base + pad;   // the full form grows one byte per pad byte
        if (full)
            sawFull = true;
        else
            sawCut = true;
    }
    CHECK(sawFull && sawCut);   // the sweep crossed the cut, so the boundary was exercised
}

static void TestTruncatedPayload()
{
    // A 40-bot fight with 200 deaths and 500 engaged: every payload fits the budget.
    Book b;
    std::vector<MemberInfo> m;
    for (int i = 0; i < 40; ++i)
        m.push_back({(uint32_t)(i + 1), std::string(200, static_cast<char>('A' + (i % 26))), "mage",
            Role::Damage, i / 5});
    b.SetMembers(m);
    b.Start(0, 1700000000, BossCtx(), 7);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < 50; ++j)
            b.OnCast((uint32_t)(i + 1), "Spell" + std::to_string(i) + "_" + std::to_string(j) + std::string(60, 'x'),
                false, 1500);
    for (int i = 0; i < 200; ++i)
        b.OnDeath(1, std::string(80, 'k'), 1.0f, 2.0f, 3.0f, (uint32_t)(1500 + i));
    for (uint32_t e = 0; e < 500; ++e)
        b.OnDamage(3, 0, 1, "Magey", 30000 + e, (uint64_t)(5000 + e), std::string(80, 'e'), 1600);
    b.SetInCombat(true, false, 1000);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    for (std::string const& js : p)
        CHECK(js.size() <= kPayloadByteBudget);
    CHECK(Contains(p[0], "\"id\":\"55-7\""));
}

static void TestEngagedCapCountsGuids()
{
    // The 500 cap counts distinct guids over all entries: 700 guids of one entry
    // keep 500 and drop 200, and repeated hits on a dropped guid count once.
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 4);
    for (int round = 0; round < 2; ++round)
        for (uint32_t g = 0; g < 700; ++g)
            b.OnDamage(3, 0, 1, "Magey", 11502, (uint64_t)(90000 + g), "Lava Spawn", 1500);
    b.OnDamage(3, 0, 1, "Magey", 12000, 99999999, "Other", 1600);
    b.SetInCombat(true, false, 1000);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "{\"e\":11502,\"n\":\"Lava Spawn\",\"c\":500}"));
    CHECK(!Contains(p[0], "\"e\":12000"));
    CHECK(Contains(p[0], "\"dropped\":201"));
}

static void TestOversizeFightOneSeqPerTake()
{
    // Every text and row far past the budget, even the old truncated form
    // (names kept whole) would not fit: the bounded form goes out, one seq per
    // queued payload, twice, then the fight is gone.
    Book b;
    std::vector<MemberInfo> m;
    for (uint32_t i = 0; i < 200; ++i)
        m.push_back({i + 1, std::string(3000, '\x01'), std::string(3000, '\x02'), Role::Damage, 0});
    b.SetMembers(m);
    FightContext ctx = BossCtx();
    ctx.plan = std::string(20000, '\x03');
    ctx.leg = std::string(20000, '"');
    ctx.kind = std::string(20000, '\\');
    ctx.bossName = std::string(20000, '\n');
    ctx.planned.assign(20000, 4000000000u);
    b.Start(0, 1700000000, ctx, 8);
    b.SetInCombat(true, false, 1000);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    std::string const commander(30000, '\x04');
    Seq seq;
    std::vector<std::string> dropped;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, commander, 123, 55, 409, &dropped);
    CHECK(p.size() == 1);
    CHECK(seq.calls == 1);
    CHECK(dropped.empty());
    CHECK(p[0].size() <= kPayloadByteBudget);
    CHECK(Contains(p[0], "\"seq\":1,"));
    CHECK(Contains(p[0], "\"truncated\":true"));
    CHECK(Count(p[0], "\"guid\":") == kTruncatedBots);
    CHECK(b.HasTakeablePayloads());
    p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009001, commander, 123, 55, 409, &dropped);
    CHECK(p.size() == 1);
    CHECK(seq.calls == 2);
    CHECK(Contains(p[0], "\"seq\":2,"));
    CHECK(!b.HasTakeablePayloads());
    CHECK(b.TakeFightPayloads(1, 9, seq.fn(), 1700009002, commander, 123, 55, 409, &dropped).empty());
    CHECK(seq.calls == 2);
    // A bounded text is cut on a character boundary: a 2-byte character
    // straddling the cut is left out whole.
    CHECK(BoundedJson(std::string(47, 'a') + "\xc3\xa9", 48) == std::string(47, 'a'));
    CHECK(BoundedJson("ab", 48) == "ab");
}

static void TestMonitorOffIsInert()
{
    Book b;
    b.Start(0, 1700000000, BossCtx(), 1);
    b.OnCast(1, "Fireball", false, 1000);
    b.OnDamage(1, 2, 500, "A", 11502, 777, "Mob", 1000);
    b.OnHeal(1, 500, 0, 1000);
    b.OnDeath(1, "Mob", 0.0f, 0.0f, 0.0f, 1000);
    b.SampleDebuffs({"sunder"});
    b.SetInCombat(false, false, 2000);
    b.NoteBossDied(11502);
    CHECK(!b.InFight());
    CHECK(b.WindowJson(2000).empty());
    Seq seq;
    CHECK(b.TakeFightPayloads(1, 9, seq.fn(), 1700002000, "Cmdr", 123, 55, 409).empty());
    CHECK(seq.calls == 0);
    CHECK(std::string(OutcomeName(Outcome::Kill)) == "kill");
    CHECK(std::string(OutcomeName(Outcome::Wipe)) == "wipe");
    CHECK(std::string(OutcomeName(Outcome::End)) == "end");
}

static void TestEngageCoalescedPerEntry()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(1000, 1700000000, BossCtx(), 1);
    b.OnDamage(1, 0, 10, "Tanky", 10184, 777, "Onyxia", 1100);
    b.OnDamage(1, 0, 10, "Tanky", 10184, 777, "Onyxia", 1200);   // same guid: not new
    b.OnDamage(2, 0, 10, "Healy", 10184, 778, "Onyxia", 1300);   // a second guid of that entry
    b.OnDamage(3, 0, 10, "Magey", 9999, 600, "Whelp", 1400);     // another entry, one guid
    std::vector<LiveEvent> events;
    uint32_t dropped = 0;
    b.TakeEvents(events, dropped);
    CHECK(dropped == 0);
    CHECK(events.size() == 3);   // start + two engages (per entry, in entry order)
    CHECK(events[0].kind == EventKind::Start);
    LiveEvent const* big = nullptr;
    LiveEvent const* small = nullptr;
    for (LiveEvent const& e : events)
    {
        if (e.kind != EventKind::Engage)
            continue;
        if (e.entry == 10184)
            big = &e;
        else if (e.entry == 9999)
            small = &e;
    }
    CHECK(big && big->count == 2 && big->name == "Onyxia");
    CHECK(small && small->count == 1 && small->name == "Whelp");
    CHECK(!b.HasEvents());
}

static void TestCloseOnFullWipe()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(1000, 1700000000, BossCtx(), 1);
    b.OnDeath(1, "Onyxia", 0.0f, 0.0f, 0.0f, 2000);
    b.SetMembers({Trio()[0]});   // the rest left the group: the only member left is the dead one
    b.Close(2500);
    CHECK(!b.InFight());
    std::vector<LiveEvent> events;
    uint32_t dropped = 0;
    b.TakeEvents(events, dropped);
    CHECK(!events.empty());
    CHECK(events.back().kind == EventKind::End);
    CHECK(events.back().outcome == Outcome::Wipe);
    CHECK(events.back().kept == false);   // 1.5 s: shorter than kMinFightMs
}

// The sampler's boss is set once per fight and only while one runs; a run's
// own boss is never replaced.
static void TestNoteBossSetsOnce()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, FightContext{}, 1);
    b.NoteBoss(5, "A");
    b.NoteBoss(6, "B");
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    CHECK(!b.InFight());
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"boss\":5"));
    CHECK(Contains(p[0], "\"boss_name\":\"A\""));
    CHECK(!Contains(p[0], "\"boss\":6"));
}

static void TestNoteBossOutsideFightIgnored()
{
    Book b;
    b.SetMembers(Trio());
    b.NoteBoss(5, "A");
    b.Start(0, 1700000000, FightContext{}, 1);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"boss\":0"));
    CHECK(Contains(p[0], "\"boss_name\":\"\""));
}

static void TestNoteBossKeepsRunBoss()
{
    FightContext ctx;
    ctx.kind = "boss";
    ctx.bossEntry = 7;
    ctx.bossName = "RunBoss";
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, ctx, 1);
    b.NoteBoss(5, "Sampler");
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"boss\":7"));
    CHECK(Contains(p[0], "\"boss_name\":\"RunBoss\""));
}

static void TestRefinedBossDeathIsKill()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, FightContext{}, 1);
    b.NoteBoss(5, "A");
    b.NoteBossDied(5);
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"boss\":5"));
    CHECK(Contains(p[0], "\"outcome\":\"kill\""));
}

// The sampler picks the boss after the tick that records its death, so the
// death of an engaged creature can arrive before any boss is picked: the later
// pick still makes the fight a kill, and another creature's death does not.
static void TestBossDeathBeforePickIsKill()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, FightContext{}, 1);
    b.OnDamage(1, 0, 500, "Tanky", 11502, 777, "Magmadar", 1000);
    b.NoteBossDied(11503);   // an add: not the boss picked next
    b.NoteBossDied(11502);   // the boss, before the sampler had picked one
    b.NoteBoss(11502, "Magmadar");
    b.SetInCombat(false, false, 6000);
    b.SetInCombat(false, false, 9000);
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"boss\":11502"));
    CHECK(Contains(p[0], "\"boss_name\":\"Magmadar\""));
    CHECK(Contains(p[0], "\"outcome\":\"kill\""));

    Book other;   // only another creature died: the picked boss is not a kill
    other.SetMembers(Trio());
    other.Start(0, 1700000000, FightContext{}, 1);
    other.NoteBossDied(11503);
    other.NoteBoss(11502, "Magmadar");
    other.SetInCombat(false, false, 6000);
    other.SetInCombat(false, false, 9000);
    std::vector<std::string> q = other.TakeFightPayloads(1, 9, seq.fn(), 1700009000, "Cmdr", 123, 55, 409);
    CHECK(q.size() == 1);
    CHECK(Contains(q[0], "\"boss\":11502"));
    CHECK(Contains(q[0], "\"outcome\":\"end\""));
}

// The sampler closes a fight its lead left open as unfinished; a boss that
// already fell still makes that fight a kill.
static void TestCloseUnfinished()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, FightContext{}, 1);
    b.CloseUnfinished(20000);
    CHECK(!b.InFight());
    Seq seq;
    std::vector<std::string> p = b.TakeFightPayloads(1, 9, seq.fn(), 1700020000, "Cmdr", 123, 55, 409);
    CHECK(p.size() == 1);
    CHECK(Contains(p[0], "\"outcome\":\"unfinished\""));
    CHECK(Contains(p[0], "\"dur_ms\":20000"));

    Book kill;
    kill.SetMembers(Trio());
    kill.Start(0, 1700000000, FightContext{}, 1);
    kill.NoteBoss(11502, "Magmadar");
    kill.NoteBossDied(11502);
    kill.CloseUnfinished(20000);
    std::vector<std::string> q = kill.TakeFightPayloads(1, 9, seq.fn(), 1700020000, "Cmdr", 123, 55, 409);
    CHECK(q.size() == 1);
    CHECK(Contains(q[0], "\"outcome\":\"kill\""));
}

static void TestEngagedNowListsEntries()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(0, 1700000000, BossCtx(), 1);
    b.OnDamage(3, 0, 5000, "Magey", 11502, 777, "Magmadar", 5000);
    b.OnDamage(1, 0, 1200, "Tanky", 11502, 778, "Magmadar", 6000);   // same entry: one row
    b.OnDamage(0, 1, 3000, "Magmadar", 11503, 900, "Add", 7000);
    std::vector<EngagedUnit> rows = b.EngagedNow();
    CHECK(rows.size() == 2);
    CHECK(rows[0].entry == 11502 && rows[0].guid == 777 && rows[0].name == "Magmadar");
    CHECK(rows[1].entry == 11503 && rows[1].guid == 900 && rows[1].name == "Add");
    b.SetInCombat(false, false, 12000);
    b.SetInCombat(false, false, 15000);
    CHECK(!b.InFight());
    CHECK(b.EngagedNow().empty());
}

static void TestEventsJsonHalvesUnderBudget()
{
    std::vector<LiveEvent> events;
    for (size_t i = 0; i < kMaxEventsPerDatagram; ++i)
    {
        LiveEvent e;
        e.kind = EventKind::Death;
        e.atMs = 1000 + static_cast<uint32_t>(i);
        e.guid = 4000 + static_cast<uint32_t>(i);
        e.name = std::string(2000, 'n');
        e.by = std::string(2000, 'k');
        events.push_back(std::move(e));
    }
    EventsFightHeader header;
    header.inFight = true;
    header.number = 1;
    header.startMs = 1000;
    header.startUnix = 1793209948;
    header.fightMs = 500;
    header.ctx = BossCtx();
    size_t emitted = 0;
    std::string const json = BuildEventsJson(12, 77, 1, 1793210001, 1793210001000, "Cmdr", 5, 9, 249, header, events, 0,
                                             &emitted);
    CHECK(json.size() <= kEventsByteBudget);
    CHECK(emitted > 0);
    CHECK(emitted < events.size());
    CHECK(Contains(json, "\"k\":\"death\""));
    CHECK(Count(json, "\"k\":\"death\"") == emitted);
}

// A single event whose names are far past the budget still goes out as one
// event: the names are clipped (the last form), so the count never has to drop
// to zero, and the header alone fits too.
static void TestEventsJsonBoundsHugeNames()
{
    LiveEvent e;
    e.kind = EventKind::Death;
    e.atMs = 1200;
    e.guid = 4000;
    e.name = std::string(200000, 'n');
    e.by = std::string(200000, 'k');
    EventsFightHeader header;
    header.inFight = true;
    header.number = 2;
    header.startMs = 1000;
    header.startUnix = 1793209948;
    header.fightMs = 500;
    header.ctx = BossCtx();
    size_t emitted = 0;
    std::string const json = BuildEventsJson(12, 77, 1, 1793210001, 1793210001000, std::string(70000, 'c'), 5, 9, 249,
                                             header, {e}, 0, &emitted);
    CHECK(emitted == 1);
    CHECK(Count(json, "\"k\":\"death\"") == 1);
    CHECK(json.size() <= kEventsByteBudget);
    CHECK(json.size() < 20000);

    // No events at all and a commander name of 70000 bytes: the header alone
    // is bounded too, so the keepalive still fits.
    size_t const before = emitted;
    std::string const keepalive = BuildEventsJson(12, 77, 2, 1793210002, 1793210002000, std::string(70000, 'c'), 5, 9,
                                                  249, header, {}, 0, &emitted);
    CHECK(emitted == 0);
    CHECK(before == 1);
    CHECK(keepalive.size() <= kEventsByteBudget);
    CHECK(Contains(keepalive, "\"events\":[]"));
    CHECK(Contains(keepalive, "\"fight\":{"));
}

// The datagram budget shrinks the count; the events taken but not serialized
// are requeued at the front, in order, so repeating take/shrink/requeue hands
// every event out exactly once, oldest first.
static void TestEventsRequeueKeepsOrder()
{
    Book b;
    b.SetMembers(Trio());
    b.Start(1000, 1700000000, BossCtx(), 1);
    std::string const prefix(2000, 'x');
    for (uint32_t i = 0; i < 40; ++i)
        b.OnCast(1, prefix + "#" + std::to_string(i), false, 1100, 't', 777, "Magmadar");
    EventsFightHeader header;
    header.inFight = true;
    header.number = 1;
    header.startMs = 1000;
    header.startUnix = 1793209948;
    header.fightMs = 1000;
    header.ctx = BossCtx();
    size_t nextCast = 0;
    bool sawStart = false;
    size_t rounds = 0;
    while (b.HasEvents())
    {
        std::vector<LiveEvent> events;
        uint32_t dropped = 0;
        b.TakeEvents(events, dropped);
        CHECK(!events.empty());
        size_t emitted = 0;
        std::string const json = BuildEventsJson(12, 77, 1, 1793210001, 1793210001000, "Cmdr", 5, 9, 249, header,
                                                 events, dropped, &emitted);
        CHECK(emitted > 0);   // the clipped form always carries one event
        CHECK(json.size() <= kEventsByteBudget);
        for (size_t i = 0; i < emitted; ++i)
        {
            if (events[i].kind == EventKind::Start)
            {
                CHECK(!sawStart);
                sawStart = true;
                continue;
            }
            CHECK(events[i].spell == prefix + "#" + std::to_string(nextCast));
            ++nextCast;
        }
        b.RequeueEvents(events, emitted);
        ++rounds;
    }
    CHECK(sawStart);
    CHECK(nextCast == 40);
    CHECK(rounds > 1);   // the budget shrank at least once
}

int main()
{
    TestNonMemberIgnored();
    TestFightLifecycle();
    TestShortFightDropped();
    TestWipe();
    TestDebuffUptime();
    TestWindow();
    TestWindowPrune();
    TestBudget();
    TestSeqWidthStaysInBudget();
    TestTruncatedPayload();
    TestEngagedCapCountsGuids();
    TestOversizeFightOneSeqPerTake();
    TestMonitorOffIsInert();
    TestEngageCoalescedPerEntry();
    TestCloseOnFullWipe();
    TestNoteBossSetsOnce();
    TestNoteBossOutsideFightIgnored();
    TestNoteBossKeepsRunBoss();
    TestRefinedBossDeathIsKill();
    TestBossDeathBeforePickIsKill();
    TestCloseUnfinished();
    TestEngagedNowListsEntries();
    TestEventsJsonHalvesUnderBudget();
    TestEventsJsonBoundsHugeNames();
    TestEventsRequeueKeepsOrder();
    std::printf("combat monitor policy: %d checks passed\n", checks);
    return 0;
}
