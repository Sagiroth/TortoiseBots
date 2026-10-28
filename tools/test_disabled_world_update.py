"""Exercise the actual world-update hook with stand-in service counters (#508).

Compile only the production OnUpdate definition, avoiding a live server or DB.
The fixture checks the disabled path and the enabled service order; it does not
reimplement the hook's control flow. Requires g++ (skipped when unavailable).
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class DisabledWorldUpdateTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_disabled_hook_is_inert_and_enabled_hook_still_ticks_services(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "host/BotHostAdapter.cpp").read_text()
        start = source.index("void BotHostAdapter::OnUpdate(uint32 diff)")
        end = source.index("void BotHostAdapter::OnShutdown()", start)
        hook = source[start:end]
        fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>
using uint32 = uint32_t;
// A `.reload config` (OnAfterConfigLoad) marks the run leader's keys due; the tick reads them again.
struct { bool enabled = false; unsigned reloads = 0; bool ReloadDungeonRunKeys() { ++reloads; return enabled; } } sPlayerbotAIConfig;
namespace TortoiseBots {
std::atomic<bool> s_dungeonRunKeysDue{false};
std::vector<unsigned> calls;
template<unsigned Id> struct Service {
    static Service& Instance() { static Service service; return service; }
    void Update(uint32 diff) { assert(diff == 50); calls.push_back(Id); }
    void OnWorldUpdate(uint32 diff) { Update(diff); }
    static uint64_t DrainQueuedCommands() { calls.push_back(Id); return 7; }
    static void DrainDeferredPackets(uint64_t limit, bool endOfTick) { assert(limit == 7 && endOfTick); calls.push_back(Id); }
    static void MarkWorldThread() {}
};
using BotActivityLeaseManager = Service<1>;
using BotManager = Service<2>;
using PlayerConvenience = Service<3>;
using RandomBotService = Service<4>;
using HireProvisionService = Service<5>;
using HireLifecycle = Service<6>;
using AhMarketService = Service<7>;
using BattlegroundQueueService = Service<8>;
using ObservabilityEmitter = Service<9>;
using BotChatAdapter = Service<10>;
using BotPacketAdapter = Service<11>;
// The opt-in run trace: one atomic load per tick while no run is active.
using RunTraceService = Service<13>;
// Guild orders: the per-tick order update (give-up clock, progress, group gate).
namespace GuildOrders { inline void Update(uint32 diff) { assert(diff == 50); calls.push_back(15); } }
// Issue #884: the remote commander's binding sweep.
namespace BotCommands { inline void UpdateRemoteCommanders(uint32 diff) { assert(diff == 50); calls.push_back(12); } inline void CleanupRunGivenItemsTick() { calls.push_back(14); } }
struct BotHostAdapter { uint32 m_ticks = 0; void OnUpdate(uint32 diff); };
'''
        fixture += hook + r'''
}
int main() {
    using namespace TortoiseBots;
    BotHostAdapter adapter;
    // More than two 60 s recovery / four 30 s reporting windows.
    // Disabled: only the queued-command drain (#833) runs, every tick, so
    // player commands never execute off the world thread, and the run trace,
    // so a running trace ends ("disabled"); other services stay inert.
    // The run trace (13) is ticked too, so a trace cannot outlive the AI.
    for (unsigned i = 0; i < 2500; ++i) adapter.OnUpdate(50);
    assert(calls.size() == 5000);
    for (size_t i = 0; i < calls.size(); i += 2) assert(calls[i] == 10 && calls[i + 1] == 13);
    assert(adapter.m_ticks == 0);
    calls.clear();
    sPlayerbotAIConfig.enabled = true;
    // Enabled: lease expiry, then the commands (#846: they run the deferred
    // bot items queued ahead of each), then the rest of the deferred items
    // up to the bound the command drain returned, then the services (the
    // remote commander sweep, 12, right after the bot update; the run trace, 13, last).
    adapter.OnUpdate(50);
    assert((calls == std::vector<unsigned>{1,10,11,2,12,3,4,5,6,15,7,8,9,14,13}));
    assert(adapter.m_ticks == 1);
    sPlayerbotAIConfig.enabled = false;
    adapter.OnUpdate(50);
    assert((calls == std::vector<unsigned>{1,10,11,2,12,3,4,5,6,15,7,8,9,14,13,10,13}));
    assert(adapter.m_ticks == 1);
    // A config reload: the run leader's keys are read again once, on the next tick, enabled or not.
    assert(sPlayerbotAIConfig.reloads == 0);
    s_dungeonRunKeysDue.store(true);
    adapter.OnUpdate(50);
    adapter.OnUpdate(50);
    assert(sPlayerbotAIConfig.reloads == 1 && !s_dungeonRunKeysDue.load());
}
'''
        with tempfile.TemporaryDirectory(prefix="disabled-world-update-") as folder:
            source_path = pathlib.Path(folder) / "test.cpp"
            binary_path = pathlib.Path(folder) / "test"
            source_path.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source_path), "-o", str(binary_path)], check=True)
            subprocess.run([str(binary_path)], check=True)


if __name__ == "__main__":
    unittest.main()
