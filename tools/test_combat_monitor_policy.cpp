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
    TestTruncatedPayload();
    TestEngagedCapCountsGuids();
    TestOversizeFightOneSeqPerTake();
    TestMonitorOffIsInert();
    std::printf("combat monitor policy: %d checks passed\n", checks);
    return 0;
}
