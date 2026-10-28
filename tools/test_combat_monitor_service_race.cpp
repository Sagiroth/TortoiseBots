// A8b: the combat monitor service under ThreadSanitizer. Two hook threads
// record events (casts, damage, heals, combat, deaths) while the world thread
// refreshes the books, drains payloads and windows; every access must stay
// race-free (the hooks run on map threads, the tick and the emitter on the
// world thread). The tick steps nowMs by 1000, six ticks in combat and four
// out, so each 6 s fight ends and TakeFightPayloads/BuildFightJson run while
// hooks fire; every tick waits for a hook pass, so the threads interleave:
//   g++ -std=c++17 -O1 -g -fsanitize=thread -DCOMBAT_MONITOR_TEST_CONFIG -I. -Itools/tsan_stubs
//       tools/test_combat_monitor_service_race.cpp runtime/CombatMonitorService.cpp
//       -o /tmp/x -lpthread && setarch "$(uname -m)" -R /tmp/x
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

// The one field the service reads through the config singleton (the real
// header needs game headers; both TUs compile with COMBAT_MONITOR_TEST_CONFIG
// so the service TU uses the stub in tools/tsan_stubs, and this TU defines
// the singleton the stub declares).
struct PlayerbotAIConfig { bool combatMonitor = true; };
#define COMBAT_MONITOR_TEST_CONFIG 1
#include "../runtime/CombatMonitorService.h"
#undef COMBAT_MONITOR_TEST_CONFIG
PlayerbotAIConfig sPlayerbotAIConfig;

using TortoiseBots::CombatMonitorService;
using TortoiseBots::CombatMonitor::MemberInfo;
using TortoiseBots::CombatMonitor::Role;
using TortoiseBots::CombatMonitor::FightContext;

static std::vector<MemberInfo> Members()
{
    return {MemberInfo{7, "Tanky", "warrior", Role::Tank, 0}, MemberInfo{9, "Healy", "priest", Role::Healer, 1},
        MemberInfo{11, "Magey", "mage", Role::Damage, 0}};
}

static FightContext Ctx()
{
    FightContext ctx;
    ctx.plan = "molten_core";
    ctx.leg = "l1";
    ctx.kind = "boss";
    ctx.step = 3;
    ctx.bossEntry = 11502;
    ctx.bossName = "Magmadar";
    ctx.planned = {11502};
    return ctx;
}

static std::atomic<uint64_t> g_hookPasses{0};

static void HookLoop(CombatMonitorService& svc, std::atomic<bool> const& stop, uint32_t seed)
{
    uint32_t tick = seed;
    while (!stop.load(std::memory_order_relaxed))
    {
        ++tick;
        g_hookPasses.fetch_add(1, std::memory_order_relaxed);
        svc.OnCast(7, "Sunder Armor", false);
        svc.OnDamage(11, 0, 500, "Magey", 11502, 777, "Magmadar");
        svc.OnDamage(0, 7, 300, "Magmadar", 11502, 777, "Magmadar");
        svc.OnHeal(9, 800, 200);
        svc.OnEnterCombat(7);
        if ((tick % 500) == 0)
            svc.OnDeath(7, 0, "Magmadar", 1.0f, 2.0f, 3.0f, 0);
        if ((tick % 700) == 0)
            svc.OnDeath(0, 11, "", 0.0f, 0.0f, 0.0f, 11502);
    }
}

int main()
{
    sPlayerbotAIConfig.combatMonitor = true;
    CombatMonitorService& svc = CombatMonitorService::Instance();
    svc.SyncConfig(true);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> payloads{0};
    uint64_t const locksBefore = CombatMonitorService::TestHookLockCount();

    std::thread hookA([&]() { HookLoop(svc, stop, 0); });
    std::thread hookB([&]() { HookLoop(svc, stop, 1000000); });
    std::thread drainer([&]() {
        uint32_t nowMs = 0;
        uint64_t seq = 1;
        std::vector<std::string> out;
        auto nextSeq = [&]() { return seq++; };
        for (int tick = 0; tick < 200 && !stop.load(std::memory_order_relaxed); ++tick)
        {
            // Let both hook threads make progress against this tick's state.
            uint64_t const passes = g_hookPasses.load(std::memory_order_relaxed);
            while (g_hookPasses.load(std::memory_order_relaxed) < passes + 2)
                std::this_thread::yield();
            nowMs += 1000;
            bool const inCombat = (tick % 10) < 6;
            svc.Refresh(55, Members(), inCombat, false, Ctx(), {"sunder"}, nowMs);
            svc.TakePayloads(out, 12, 77, nextSeq, 1791990020);
            payloads.fetch_add(out.size());
            for (std::string const& p : out)
                if (p.find("COMBAT_FIGHT") == std::string::npos)
                    errors.fetch_add(1);
            std::string window = svc.WindowJson(55);
            (void)window;
            if ((tick % 50) == 49)
                svc.Forget(55);
        }
        stop.store(true, std::memory_order_relaxed);
    });

    drainer.join();
    hookA.join();
    hookB.join();
    svc.Forget(55);
    if (errors.load() != 0)
    {
        std::printf("BAD PAYLOADS: %llu\n", (unsigned long long)errors.load());
        return 1;
    }
    // Fights must end and be taken under contention, and the hooks must have
    // reached the mutex (members bound) a fair number of times.
    uint64_t const locks = CombatMonitorService::TestHookLockCount() - locksBefore;
    if (payloads.load() == 0 || locks < 200)
    {
        std::printf("NO CONTENTION: payloads %llu, hook locks %llu\n", (unsigned long long)payloads.load(),
                    (unsigned long long)locks);
        return 1;
    }
    std::printf("combat monitor race test: %llu payloads, %llu hook locks\n", (unsigned long long)payloads.load(),
                (unsigned long long)locks);
    std::printf("combat monitor race test: clean\n");
    return 0;
}
