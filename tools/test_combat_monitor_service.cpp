// A8b round 2: the combat monitor service's enable flag, fight starts, fight
// ids and killer-group boss kills, against the real service TU with the stub
// config:
//
//   g++ -std=c++17 -Wall -Wextra -DCOMBAT_MONITOR_TEST_CONFIG -I. -Itools/tsan_stubs
//       tools/test_combat_monitor_service.cpp runtime/CombatMonitorService.cpp
//       -o /tmp/combat_svc && /tmp/combat_svc
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

struct PlayerbotAIConfig { bool combatMonitor = false; };
#define COMBAT_MONITOR_TEST_CONFIG 1
#include "../runtime/CombatMonitorService.h"
#undef COMBAT_MONITOR_TEST_CONFIG
PlayerbotAIConfig sPlayerbotAIConfig;

using TortoiseBots::CombatMonitorService;
using TortoiseBots::CombatMonitor::MemberInfo;
using TortoiseBots::CombatMonitor::Role;
using TortoiseBots::CombatMonitor::FightContext;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static std::vector<MemberInfo> Members()
{
    return {MemberInfo{7, "Tanky", "warrior", Role::Tank, -1}, MemberInfo{9, "Healy", "priest", Role::Healer, -1}};
}

static FightContext Ctx(uint32_t boss = 11502)
{
    FightContext ctx;
    ctx.plan = "molten_core";
    ctx.leg = "l1";
    ctx.kind = "boss";
    ctx.step = 3;
    ctx.bossEntry = boss;
    ctx.bossName = "Magmadar";
    ctx.planned = {boss};
    return ctx;
}

static uint64_t g_seq = 1;
static uint64_t NextSeq()
{
    return g_seq++;
}

static std::string TakeOne(CombatMonitorService& svc, uint32_t group, int64_t ts)
{
    std::vector<std::string> out;
    svc.TakePayloads(out, 12, 77, NextSeq, ts);
    for (std::string const& p : out)
        if (p.find("\"group\":" + std::to_string(group)) != std::string::npos)
            return p;
    return "";
}

static std::vector<std::string> TakeEventsFor(CombatMonitorService& svc, uint32_t group, uint32_t nowMs,
                                              uint32_t gap = 250)
{
    std::vector<std::string> out;
    svc.TakeEventPayloads(out, 12, 77, NextSeq, 1700010000, 1700010000000, nowMs, gap);
    std::vector<std::string> mine;
    for (std::string const& p : out)
        if (p.find("\"group\":" + std::to_string(group) + ",") != std::string::npos)
            mine.push_back(p);
    return mine;
}

static size_t CountOf(std::string const& hay, std::string const& needle)
{
    size_t n = 0;
    for (size_t pos = 0; (pos = hay.find(needle, pos)) != std::string::npos; pos += needle.size())
        ++n;
    return n;
}

static std::string FightId(std::string const& payload)
{
    std::string const tag = "\"id\":\"";
    std::string::size_type pos = payload.find(tag);
    if (pos == std::string::npos)
        return "";
    pos += tag.size();
    return payload.substr(pos, payload.find('"', pos) - pos);
}

static void TestSyncConfigUngated()
{
    // SyncConfig stores the flag even though nobody enabled the monitor: the
    // flag no longer lives inside Refresh()/Forget().
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = false;
    svc.SyncConfig(false);
    CHECK(!svc.Enabled());
    svc.Refresh(55, Members(), true, false, Ctx(), {}, 1000);
    CHECK(!svc.Enabled());
    CHECK(svc.WindowJson(55).empty());
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    CHECK(svc.Enabled());
    svc.Refresh(55, Members(), true, false, Ctx(), {}, 1000);
    CHECK(!svc.WindowJson(55).empty());
    // Turning off drops the books and the member set: hooks stop locking.
    uint64_t const locks = CombatMonitorService::TestHookLockCount();
    svc.SyncConfig(false);
    CHECK(!svc.Enabled());
    CHECK(svc.WindowJson(55).empty());
    svc.OnCast(7, "Fireball", false);
    svc.OnDamage(7, 0, 10, "Tanky", 11502, 777, "Magmadar");
    CHECK(CombatMonitorService::TestHookLockCount() == locks);
    svc.SyncConfig(true);
    svc.Forget(55);
}

static void TestFightStartNeedsCombat()
{
    // No fight out of combat; an enter-combat fight carries the cached context.
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(56, Members(), false, false, Ctx(11502), {}, 1000);
    CHECK(svc.WindowJson(56).empty());
    svc.OnEnterCombat(7);
    std::string const window = svc.WindowJson(56);
    CHECK(!window.empty());
    CHECK(!svc.WindowJson(57).empty() == false);
    svc.Refresh(56, Members(), true, false, Ctx(11502), {}, 2000);
    svc.Refresh(56, Members(), false, false, Ctx(11502), {}, 9000);
    svc.Refresh(56, Members(), false, false, Ctx(11502), {}, 13000);
    std::string const fight = TakeOne(svc, 56, 1700013000);
    CHECK(!fight.empty());
    CHECK(fight.find("\"boss\":11502") != std::string::npos);
    svc.Forget(56);
}

static void TestFightIdsSurviveForget()
{
    // The per-group fight counter survives Forget(): ids never repeat in a session.
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(58, Members(), true, false, Ctx(), {}, 1000);
    svc.Refresh(58, Members(), false, false, Ctx(), {}, 8000);
    svc.Refresh(58, Members(), false, false, Ctx(), {}, 12000);
    std::string const first = TakeOne(svc, 58, 1700012000);
    CHECK(!first.empty());
    svc.Forget(58);
    svc.Refresh(58, Members(), true, false, Ctx(), {}, 13000);
    svc.Refresh(58, Members(), false, false, Ctx(), {}, 20000);
    svc.Refresh(58, Members(), false, false, Ctx(), {}, 24000);
    std::string const second = TakeOne(svc, 58, 1700024000);
    CHECK(!second.empty());
    CHECK(FightId(first) != FightId(second));
    CHECK(FightId(first) == "58-1");
    CHECK(FightId(second) == "58-2");
    svc.Forget(58);
}

static void TestHooksSkipWithoutMembers()
{
    // Hooks with no members never take the mutex (the lock counter stands still).
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    uint64_t const locks = CombatMonitorService::TestHookLockCount();
    svc.OnCast(4242, "Fireball", false);
    svc.OnDamage(4242, 0, 10, "Nobody", 11502, 777, "Magmadar");
    svc.OnDamage(0, 4242, 10, "Magmadar", 11502, 777, "Magmadar");
    svc.OnHeal(4242, 10, 0);
    svc.OnEnterCombat(4242);
    svc.OnDeath(4242, 0, "Magmadar", 0.0f, 0.0f, 0.0f, 0);
    svc.OnDeath(0, 4242, "", 0.0f, 0.0f, 0.0f, 11502);
    CHECK(CombatMonitorService::TestHookLockCount() == locks);
}

static void TestBossKillNeedsKillerGroup()
{
    // A creature death notes the boss kill only in the killer's group's book.
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    std::vector<MemberInfo> other = {MemberInfo{21, "Other", "mage", Role::Damage, -1}};
    svc.Refresh(61, Members(), true, false, Ctx(11502), {}, 1000);
    svc.Refresh(62, other, true, false, Ctx(11502), {}, 1000);
    // A stranger's kill: no book notes it; both fights end without the kill.
    svc.OnDeath(0, 999, "", 0.0f, 0.0f, 0.0f, 11502);
    svc.Refresh(61, Members(), false, false, Ctx(11502), {}, 6000);
    svc.Refresh(62, other, false, false, Ctx(11502), {}, 6000);
    svc.Refresh(61, Members(), false, false, Ctx(11502), {}, 10000);
    svc.Refresh(62, other, false, false, Ctx(11502), {}, 10000);
    std::string const plain61 = TakeOne(svc, 61, 1700010000);
    std::string const plain62 = TakeOne(svc, 62, 1700010000);
    CHECK(!plain61.empty() && !plain62.empty());
    CHECK(plain61.find("\"outcome\":\"end\"") != std::string::npos);
    CHECK(plain62.find("\"outcome\":\"end\"") != std::string::npos);
    // The killer's group only.
    svc.Refresh(61, Members(), true, false, Ctx(11502), {}, 10000);
    svc.Refresh(62, other, true, false, Ctx(11502), {}, 10000);
    svc.OnDeath(0, 7, "", 0.0f, 0.0f, 0.0f, 11502);
    svc.Refresh(61, Members(), false, false, Ctx(11502), {}, 16000);
    svc.Refresh(62, other, false, false, Ctx(11502), {}, 16000);
    svc.Refresh(61, Members(), false, false, Ctx(11502), {}, 20000);
    svc.Refresh(62, other, false, false, Ctx(11502), {}, 20000);
    std::string const kill61 = TakeOne(svc, 61, 1700020000);
    std::string const kill62 = TakeOne(svc, 62, 1700020000);
    CHECK(!kill61.empty() && !kill62.empty());
    CHECK(kill61.find("\"outcome\":\"kill\"") != std::string::npos);
    CHECK(kill62.find("\"outcome\":\"end\"") != std::string::npos);
    // No killer known: nothing notes it.
    svc.Refresh(61, Members(), true, false, Ctx(11502), {}, 21000);
    svc.OnDeath(0, 0, "", 0.0f, 0.0f, 0.0f, 11502);
    svc.Refresh(61, Members(), false, false, Ctx(11502), {}, 28000);
    svc.Refresh(61, Members(), false, false, Ctx(11502), {}, 32000);
    std::string const nokiller = TakeOne(svc, 61, 1700032000);
    CHECK(!nokiller.empty());
    CHECK(nokiller.find("\"outcome\":\"end\"") != std::string::npos);
    svc.Forget(61);
    svc.Forget(62);
}

static void TestBitsetFiltersNonMembers()
{
    // With a group bound, a non-member guid that misses the member bitset
    // neither loads the snapshot nor locks; a member guid does both.
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(63, Members(), true, false, Ctx(), {}, 1000);
    uint64_t const locks = CombatMonitorService::TestHookLockCount();
    uint64_t const loads = CombatMonitorService::TestSnapshotLoadCount();
    svc.OnCast(4242, "Fireball", false);
    svc.OnDamage(4242, 0, 10, "Nobody", 11502, 777, "Magmadar");
    svc.OnDamage(0, 4242, 10, "Magmadar", 11502, 777, "Magmadar");
    svc.OnHeal(4242, 10, 0);
    svc.OnEnterCombat(4242);
    svc.OnDeath(4242, 0, "Magmadar", 0.0f, 0.0f, 0.0f, 0);
    svc.OnDeath(0, 4242, "", 0.0f, 0.0f, 0.0f, 11502);
    CHECK(CombatMonitorService::TestHookLockCount() == locks);
    CHECK(CombatMonitorService::TestSnapshotLoadCount() == loads);
    svc.OnCast(7, "Sunder Armor", false);
    CHECK(CombatMonitorService::TestHookLockCount() == locks + 1);
    CHECK(CombatMonitorService::TestSnapshotLoadCount() == loads + 1);
    svc.OnDamage(0, 9, 10, "Magmadar", 11502, 777, "Magmadar");
    CHECK(CombatMonitorService::TestHookLockCount() == locks + 2);
    // A member that left the group misses again once the next Refresh republished.
    svc.Refresh(63, {MemberInfo{7, "Tanky", "warrior", Role::Tank, -1}}, true, false, Ctx(), {}, 2000);
    uint64_t const after = CombatMonitorService::TestHookLockCount();
    svc.OnHeal(9, 10, 0);
    CHECK(CombatMonitorService::TestHookLockCount() == after);
    svc.Forget(63);
}

static void TestRepublishOnlyOnChange()
{
    // Refresh republishes the member set only when it changed; SyncConfig(false)
    // drops the books only on the on->off turn.
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(64, Members(), false, false, Ctx(), {}, 1000);
    uint64_t const publishes = CombatMonitorService::TestPublishCount();
    svc.Refresh(64, Members(), false, false, Ctx(), {}, 2000);
    svc.Refresh(64, {Members()[1], Members()[0]}, false, false, Ctx(), {}, 3000);
    CHECK(CombatMonitorService::TestPublishCount() == publishes);
    svc.Refresh(64, {Members()[0]}, false, false, Ctx(), {}, 4000);
    CHECK(CombatMonitorService::TestPublishCount() == publishes + 1);
    svc.Refresh(64, Members(), false, false, Ctx(), {}, 5000);
    CHECK(CombatMonitorService::TestPublishCount() == publishes + 2);
    svc.SyncConfig(true);
    CHECK(CombatMonitorService::TestPublishCount() == publishes + 2);
    svc.SyncConfig(false);
    CHECK(CombatMonitorService::TestPublishCount() == publishes + 3);
    svc.SyncConfig(false);
    CHECK(CombatMonitorService::TestPublishCount() == publishes + 3);
    svc.SyncConfig(true);
    svc.Forget(64);
}

static void TestOversizeFightTakesOneSeq()
{
    // Names far past the budget: the bounded truncated form still goes out,
    // once per take, each with exactly one seq.
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    std::vector<MemberInfo> big;
    for (uint32_t i = 0; i < 40; ++i)
        big.push_back(MemberInfo{100 + i, std::string(5000, 'n'), "mage", Role::Damage, 0});
    svc.NoteBinding(65, std::string(70000, 'c'), 5, 409);
    svc.Refresh(65, big, true, false, Ctx(), {}, 1000);
    svc.Refresh(65, big, false, false, Ctx(), {}, 8000);
    svc.Refresh(65, big, false, false, Ctx(), {}, 12000);
    uint64_t const seqBefore = g_seq;
    std::vector<std::string> out;
    svc.TakePayloads(out, 12, 77, NextSeq, 1700012000);
    CHECK(out.size() == 1);
    CHECK(g_seq == seqBefore + 1);
    CHECK(out[0].size() <= TortoiseBots::CombatMonitor::kPayloadByteBudget);
    CHECK(out[0].find("\"truncated\":true") != std::string::npos);
    CHECK(out[0].find("\"seq\":" + std::to_string(seqBefore) + ",") != std::string::npos);
    CHECK(CombatMonitorService::TestOversizeDropCount() == 0);
    svc.Forget(65);
}

// A fight keeps running after its run is gone; RefreshNoRun neither
// starts one nor loses the open one.
static void TestFightOutlivesRunStop()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(70, Members(), true, false, Ctx(), {}, 1000);
    svc.RefreshNoRun(70, true, false, 300000);
    svc.RefreshNoRun(70, false, false, 301000);
    svc.RefreshNoRun(70, false, false, 304500);
    std::string const first = TakeOne(svc, 70, 1700013000);
    std::string const second = TakeOne(svc, 70, 1700013000);
    std::string const third = TakeOne(svc, 70, 1700013000);
    CHECK(!first.empty());
    CHECK(first.find("\"dur_ms\":300000") != std::string::npos);
    CHECK(!second.empty());
    CHECK(second.find("\"dur_ms\":300000") != std::string::npos);
    CHECK(third.empty());
    svc.Forget(70);
}

static void TestRefreshNoRunNeverStarts()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.RefreshNoRun(71, true, false, 1000);   // no book: nothing to follow
    CHECK(TakeOne(svc, 71, 1700001000).empty());
    svc.Refresh(71, Members(), false, false, Ctx(), {}, 2000);   // a book, no fight
    svc.RefreshNoRun(71, true, false, 3000);
    CHECK(!svc.InFight(71));
    CHECK(TakeOne(svc, 71, 1700003000).empty());
    svc.Forget(71);
}

static void TestForgetKeepsUntakenFight()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(72, Members(), true, false, Ctx(), {}, 1000);
    svc.Refresh(72, Members(), false, false, Ctx(), {}, 8000);
    svc.Refresh(72, Members(), false, false, Ctx(), {}, 11500);   // ends at 8000, 7 s: kept
    svc.Forget(72);
    std::string const first = TakeOne(svc, 72, 1700011500);
    std::string const second = TakeOne(svc, 72, 1700011500);
    std::string const third = TakeOne(svc, 72, 1700011500);
    CHECK(!first.empty());
    CHECK(first.find("\"outcome\":\"end\"") != std::string::npos);
    CHECK(!second.empty());
    CHECK(third.empty());
}

static void TestForgetClosesOpenFight()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(73, Members(), true, false, Ctx(), {}, 1000);
    svc.RefreshNoRun(73, true, false, 61000);   // 60 s in, still fighting
    svc.Forget(73);
    std::string const fight = TakeOne(svc, 73, 1700061000);
    CHECK(!fight.empty());
    CHECK(fight.find("\"dur_ms\":60000") != std::string::npos);
    CHECK(fight.find("\"outcome\":\"end\"") != std::string::npos);
    TakeOne(svc, 73, 1700061000);
}

static void TestEventsStartAndDeath()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(74, Members(), true, false, Ctx(), {}, 1000);
    std::vector<std::string> events = TakeEventsFor(svc, 74, 1000);
    CHECK(events.size() == 1);
    CHECK(events[0].find("\"k\":\"start\"") != std::string::npos);
    CHECK(events[0].find("\"id\":\"74-1\"") != std::string::npos);
    svc.OnDeath(7, 0, "Magmadar", 1.0f, 2.0f, 3.0f, 0);
    events = TakeEventsFor(svc, 74, 1400);
    CHECK(events.size() == 1);
    CHECK(events[0].find("\"k\":\"death\"") != std::string::npos);
    CHECK(events[0].find("\"by\":\"Magmadar\"") != std::string::npos);
    svc.Forget(74);
}

static void TestEventGapLimits()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(75, Members(), true, false, Ctx(), {}, 1000);
    CHECK(TakeEventsFor(svc, 75, 1000).size() == 1);
    svc.OnDeath(7, 0, "Magmadar", 0.0f, 0.0f, 0.0f, 0);
    CHECK(TakeEventsFor(svc, 75, 1100).empty());
    std::vector<std::string> const later = TakeEventsFor(svc, 75, 1400);
    CHECK(later.size() == 1);
    CHECK(later[0].find("\"k\":\"death\"") != std::string::npos);
    svc.Forget(75);
}

static void TestKeepaliveAfter5s()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(76, Members(), true, false, Ctx(), {}, 1000);
    CHECK(TakeEventsFor(svc, 76, 1000).size() == 1);
    CHECK(TakeEventsFor(svc, 76, 5000).empty());
    std::vector<std::string> const keep = TakeEventsFor(svc, 76, 6000);
    CHECK(keep.size() == 1);
    CHECK(keep[0].find("\"events\":[]") != std::string::npos);
    CHECK(keep[0].find("\"fight_ms\":5000") != std::string::npos);
    svc.Forget(76);
}

static void TestEventQueueBounded()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(77, Members(), true, false, Ctx(), {}, 1000);
    CHECK(TakeEventsFor(svc, 77, 1000).size() == 1);   // the start went out first
    for (uint32_t i = 1; i <= 100; ++i)
    {
        svc.Refresh(77, Members(), true, false, Ctx(), {}, 1000 + i);
        svc.OnDeath(7, 0, "Magmadar", 0.0f, 0.0f, 0.0f, 0);
    }
    std::vector<std::string> const first = TakeEventsFor(svc, 77, 2000);
    CHECK(first.size() == 1);
    CHECK(first[0].find("\"dropped\":36") != std::string::npos);
    CHECK(first[0].find("\"events\":[{\"k\":\"death\",\"t\":37,") != std::string::npos);
    std::vector<std::string> const second = TakeEventsFor(svc, 77, 2400);
    CHECK(second.size() == 1);
    CHECK(second[0].find("\"dropped\":0") != std::string::npos);
    CHECK(second[0].find("\"t\":100,") != std::string::npos);
    svc.Forget(77);
}

static void TestEndEventKeptFlag()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(78, Members(), true, false, Ctx(), {}, 1000);
    CHECK(TakeEventsFor(svc, 78, 1000).size() == 1);
    svc.Refresh(78, Members(), false, false, Ctx(), {}, 2000);
    svc.Refresh(78, Members(), false, false, Ctx(), {}, 5000);   // 1 s fight: too short to send
    std::vector<std::string> const end = TakeEventsFor(svc, 78, 6000);
    CHECK(end.size() == 1);
    CHECK(end[0].find("\"k\":\"end\"") != std::string::npos);
    CHECK(end[0].find("\"kept\":false") != std::string::npos);
    CHECK(end[0].find("\"fight\":null") != std::string::npos);
    svc.Forget(78);
}

static void TestTauntAndInterruptKinds()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(79, Members(), true, false, Ctx(), {}, 1000);
    CHECK(TakeEventsFor(svc, 79, 1000).size() == 1);
    svc.OnCast(7, "Taunt", false, 't', 777, "Magmadar");
    svc.OnCast(9, "Kick", false, 'i', 778, "Magmadar");
    svc.OnCast(9, "Smite", false);
    std::vector<std::string> const events = TakeEventsFor(svc, 79, 1400);
    CHECK(events.size() == 1);
    CHECK(events[0].find("\"k\":\"taunt\"") != std::string::npos);
    CHECK(events[0].find("\"k\":\"interrupt\"") != std::string::npos);
    CHECK(events[0].find("{\"k\":\"taunt\",\"t\":0,\"g\":7,\"n\":\"Tanky\",\"s\":\"Taunt\"") != std::string::npos);
    CHECK(events[0].find("{\"k\":\"interrupt\",\"t\":0,\"g\":9,\"n\":\"Healy\",\"s\":\"Kick\"") != std::string::npos);
    CHECK(events[0].find("\"tg\":\"777\"") != std::string::npos);
    CHECK(events[0].find("\"tg\":\"778\"") != std::string::npos);
    CHECK(events[0].find("\"s\":\"Taunt\"") != std::string::npos);
    CHECK(CountOf(events[0], "\"k\":\"") == 2);
    svc.Forget(79);
}

static void TestEnemyCastEventOnlyEliteBoss()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(80, Members(), true, false, Ctx(), {}, 1000);
    CHECK(TakeEventsFor(svc, 80, 1000).size() == 1);
    svc.NoteEnemyCast(80, 10184, 90001, "Onyxia", "Shadowflame", 't');
    svc.NoteEnemyCast(80, 10184, 90002, "Onyxia", "Fireball", 'b');
    svc.NoteEnemyCast(80, 10184, 90003, "Onyxia", "Fear", 'e');
    std::vector<std::string> const events = TakeEventsFor(svc, 80, 1400);
    CHECK(events.size() == 1);
    CHECK(CountOf(events[0], "\"k\":\"cast\"") == 2);
    CHECK(events[0].find("\"id\":\"90001\"") == std::string::npos);
    CHECK(events[0].find("\"id\":\"90002\"") != std::string::npos);
    CHECK(events[0].find("\"id\":\"90003\"") != std::string::npos);
    CHECK(events[0].find("\"r\":\"b\"") != std::string::npos);
    CHECK(events[0].find("\"r\":\"e\"") != std::string::npos);
    svc.Forget(80);
}

static void TestRetiredBookDrainsThenErased()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    svc.SyncConfig(true);
    svc.Refresh(81, Members(), true, false, Ctx(), {}, 1000);
    svc.RefreshNoRun(81, true, false, 61000);
    svc.Forget(81);
    std::vector<std::string> const events = TakeEventsFor(svc, 81, 62000);
    CHECK(events.size() == 1);
    CHECK(events[0].find("\"k\":\"end\"") != std::string::npos);
    CHECK(!TakeOne(svc, 81, 1700062000).empty());
    CHECK(!TakeOne(svc, 81, 1700062000).empty());
    CHECK(TakeOne(svc, 81, 1700062000).empty());
    CHECK(TakeEventsFor(svc, 81, 63000).empty());
}

int main()
{
    TestSyncConfigUngated();
    TestFightStartNeedsCombat();
    TestFightIdsSurviveForget();
    TestHooksSkipWithoutMembers();
    TestBossKillNeedsKillerGroup();
    TestBitsetFiltersNonMembers();
    TestRepublishOnlyOnChange();
    TestOversizeFightTakesOneSeq();
    TestFightOutlivesRunStop();
    TestRefreshNoRunNeverStarts();
    TestForgetKeepsUntakenFight();
    TestForgetClosesOpenFight();
    TestEventsStartAndDeath();
    TestEventGapLimits();
    TestKeepaliveAfter5s();
    TestEventQueueBounded();
    TestEndEventKeptFlag();
    TestTauntAndInterruptKinds();
    TestEnemyCastEventOnlyEliteBoss();
    TestRetiredBookDrainsThenErased();
    std::printf("combat monitor service: %d checks passed\n", checks);
    return 0;
}
