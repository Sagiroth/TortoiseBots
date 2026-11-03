#include "ViscidusDiagnostics.h"
#include "playerbot/playerbot.h"
#include "playerbot/WorldPosition.h"
#include "../runtime/policy/infra/RaidBossLogPolicy.h"
#include <map>
#include <set>
#include <mutex>
#include <ctime>

using namespace TortoiseBots;
namespace
{
    struct Cycle
    {
        uint32 seen = 0, sampled = 0, frozenAt = 0, hits = 0, wave = 0, kills = 0, splitAt = 0;
        int stage = -1;
        bool frozen = false, shattered = false;
        std::set<uint64> globs;
        std::map<uint64, uint32> born;
    };
    std::map<uint32, Cycle> cycles;
    std::mutex guard;
    bool Enabled(Unit* unit)
    {
        return unit && unit->GetMapId() == 531 && (unit->GetEntry() == 15299 || unit->GetEntry() == 15667) &&
            sPlayerbotAIConfig.raidBossDiagnostics && sPlayerbotAIConfig.hasLog("bot_events.csv");
    }
    void Write(Unit* unit, char const* event, std::string detail)
    {
        std::time_t now = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &now);
#else
        gmtime_r(&now, &tm);
#endif
        char stamp[20]; std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
        std::ostringstream pos; ai::WorldPosition(unit).printWKT(pos);
        detail = "map=531|instance=" + std::to_string(unit->GetInstanceId()) + "|guid=" +
            std::to_string(unit->GetObjectGuid().GetRawValue()) + "|" + detail;
        auto row = RaidBossLog::Row(stamp, unit->GetName(), event, pos.str(), 0, 0, float(unit->GetLevel()), "Viscidus", detail);
        sPlayerbotAIConfig.log("bot_events.csv", row.c_str());
    }
    void EndWindow(Unit* unit, Cycle& cycle, char const* reason, uint32 now)
    {
        if (!cycle.frozen) return;
        Write(unit, "ViscidusFrozenEnd", "reason=" + std::string(reason) + "|window_ms=" +
            std::to_string(uint32(now - cycle.frozenAt)) + "|last_sampled_physical_hits=" + std::to_string(cycle.hits));
        cycle.frozen = false;
    }
}
void ViscidusDiagnostics::OnUnitUpdate(Unit* unit, uint32)
{
    if (!Enabled(unit) || !unit->IsAlive()) return;
    uint32 now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(guard);
    for (auto it = cycles.begin(); it != cycles.end();)
        if (uint32(now - it->second.seen) > 300000) it = cycles.erase(it); else ++it;
    Cycle& cycle = cycles[unit->GetInstanceId()]; cycle.seen = now;
    if (unit->GetEntry() == 15667)
    {
        if (!cycle.globs.insert(unit->GetObjectGuid().GetRawValue()).second) return;
        cycle.born[unit->GetObjectGuid().GetRawValue()] = now;
        if (!cycle.shattered)
        {
            cycle.shattered = true; ++cycle.wave; cycle.kills = 0; cycle.splitAt = now;
            EndWindow(unit, cycle, "shatter_glob_observed", now);
            Write(unit, "ViscidusShatter", "wave=" + std::to_string(cycle.wave) +
                "|evidence=glob_summon|required_physical_hits=150|last_sampled_physical_hits=" + std::to_string(cycle.hits));
        }
        Write(unit, "ViscidusGlobSpawn", "wave=" + std::to_string(cycle.wave) + "|glob_count=" + std::to_string(cycle.globs.size()));
        return;
    }
    if (uint32(now - cycle.sampled) < 250) return;
    cycle.sampled = now;
    bool frozen = unit->HasAura(25937);
    int stage = frozen ? 3 : unit->GetVisibility() == VISIBILITY_OFF ? 4 :
        unit->HasAura(26036) ? 2 : unit->HasAura(26034) ? 1 : 0;
    if (stage != cycle.stage)
    {
        if (cycle.stage == 4 && stage != 4 && cycle.shattered)
            Write(unit, "ViscidusGlobPhaseEnd", "wave=" + std::to_string(cycle.wave) +
                "|phase_ms=" + std::to_string(uint32(now - cycle.splitAt)) +
                "|spawned=" + std::to_string(cycle.globs.size()) + "|killed=" + std::to_string(cycle.kills));
        Write(unit, "ViscidusStage", "from=" + std::to_string(cycle.stage) + "|stage=" + std::to_string(stage) +
            "|legend=0_normal_1_slow_2_freezing_3_frozen_4_split");
        cycle.stage = stage;
    }
    if (frozen && !cycle.frozen)
    {
        cycle.frozen = true; cycle.frozenAt = now; cycle.hits = 0;
        cycle.shattered = false; cycle.globs.clear(); cycle.born.clear();
        Write(unit, "ViscidusFrozenStart", "required_physical_hits=150");
    }
    if (frozen)
    {
        // Read the core's real physical-hit proc counter, not damage amounts,
        // attack orders or spell attempts. Sampling can miss the final hit;
        // the subsequent glob summon is independent evidence of shattering.
        if (Aura* aura = unit->GetAura(25937, EFFECT_INDEX_1))
        {
            uint32 hits = uint32(std::max(0, aura->GetModifier()->m_amount));
            if (hits != cycle.hits)
            {
                cycle.hits = hits;
                Write(unit, "ViscidusFrozenHits", "physical_hits=" + std::to_string(hits) +
                    "|window_ms=" + std::to_string(uint32(now - cycle.frozenAt)));
            }
        }
    }
    else EndWindow(unit, cycle, cycle.shattered ? "shatter" : "aura_removed_pending_shatter_or_thaw", now);
}
void ViscidusDiagnostics::OnUnitDeath(Unit* unit, Unit* killer)
{
    if (!Enabled(unit)) return;
    std::lock_guard<std::mutex> lock(guard);
    auto& cycle = cycles[unit->GetInstanceId()];
    if (unit->GetEntry() == 15667)
    {
        ++cycle.kills;
        auto born = cycle.born.find(unit->GetObjectGuid().GetRawValue());
        std::string ttk = born == cycle.born.end() ? "unknown" : std::to_string(uint32(WorldTimer::getMSTime() - born->second));
        Write(unit, "ViscidusGlobDeath", "ttk_ms=" + ttk + "|" + "wave=" + std::to_string(cycle.wave) + "|kills=" + std::to_string(cycle.kills) +
            "|glob_count=" + std::to_string(cycle.globs.size()) + "|killer_guid=" + std::to_string(killer ? killer->GetObjectGuid().GetRawValue() : 0));
    }
    else
    {
        EndWindow(unit, cycle, "boss_death", WorldTimer::getMSTime());
        Write(unit, "ViscidusDeath", "waves=" + std::to_string(cycle.wave));
        cycles.erase(unit->GetInstanceId());
    }
}
void ViscidusDiagnostics::OnUnitExitCombat(Unit* unit)
{
    if (!Enabled(unit) || unit->GetEntry() != 15299) return;
    std::lock_guard<std::mutex> lock(guard);
    auto it = cycles.find(unit->GetInstanceId());
    if (it == cycles.end()) return;
    EndWindow(unit, it->second, "combat_exit", WorldTimer::getMSTime());
    Write(unit, "ViscidusReset", "waves=" + std::to_string(it->second.wave));
    cycles.erase(it);
}
