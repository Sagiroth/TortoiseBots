// A8b: the combat monitor service under ThreadSanitizer. Thread A records
// hook events (casts, damage, heals, combat, deaths) while thread B drains
// payloads and windows on the "world thread" and refreshes the books; every
// access must stay race-free (the hooks run on map threads, the tick and the
// emitter on the world thread):
//   g++ -std=c++17 -O1 -g -fsanitize=thread -DCOMBAT_MONITOR_TEST_CONFIG -I. -Itools/tsan_stubs \
//       tools/test_combat_monitor_service_race.cpp runtime/CombatMonitorService.cpp \
//       -o /tmp/x -lpthread && setarch "$(uname -m)" -R /tmp/x
#include <atomic>
#include <cassert>
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

int main()
{
    sPlayerbotAIConfig.combatMonitor = true;
    CombatMonitorService& svc = CombatMonitorService::Instance();
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> errors{0};

    std::thread recorder([&]() {
        uint32_t tick = 0;
        while (!stop.load())
        {
            ++tick;
            svc.OnCast(7, "Sunder Armor", false);
            svc.OnDamage(11, 0, 500, "Magey", 11502, 777, "Magmadar");
            svc.OnDamage(0, 7, 300, "Magmadar", 11502, 777, "Magmadar");
            svc.OnHeal(9, 800, 200);
            svc.OnEnterCombat(7);
            if ((tick % 500) == 0)
                svc.OnDeath(7, "Magmadar", 1.0f, 2.0f, 3.0f, 0);
            if ((tick % 700) == 0)
                svc.OnDeath(0, "", 0.0f, 0.0f, 0.0f, 11502);
        }
    });
    std::thread drainer([&]() {
        uint64_t tick = 0;
        std::vector<std::string> out;
        while (!stop.load())
        {
            ++tick;
            svc.Refresh(55, Members(), true, false, Ctx(), {"sunder"}, tick);
            svc.TakePayloads(out, 12, 77, tick, 1791990020);
            for (std::string const& p : out)
                if (p.find("COMBAT_FIGHT") == std::string::npos)
                    errors.fetch_add(1);
            std::string window = svc.WindowJson(55);
            (void)window;
            if ((tick % 1000) == 0)
                svc.Forget(55);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    stop.store(true);
    recorder.join();
    drainer.join();
    svc.Forget(55);
    if (errors.load() != 0)
    {
        std::printf("BAD PAYLOADS: %llu\n", (unsigned long long)errors.load());
        return 1;
    }
    std::printf("combat monitor race test: clean\n");
    return 0;
}
