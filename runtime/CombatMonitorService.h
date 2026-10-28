#pragma once
// Per-fight combat numbers behind the core's damage, heal, cast, combat and
// death hooks: plain data and the standard library only; the books themselves
// are runtime/CombatMonitorPolicy.h. The hooks run on map threads; the 1 Hz
// run tick and the emitter's snapshot cycle run on the world thread.
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "CombatMonitorPolicy.h"

class Player;
class Unit;

namespace TortoiseBots {

class CombatMonitorService
{
public:
    static CombatMonitorService& Instance();

    bool Enabled() const;                                   // read on the world thread, cached as std::atomic<bool>
    // world thread, 1 Hz from the run tick, for every bound group with a run
    void Refresh(uint32_t groupId, std::vector<CombatMonitor::MemberInfo> members, bool anyInCombat, bool allDead,
                 CombatMonitor::FightContext const& ctx, std::vector<std::string> const& debuffKeysUp, uint32_t nowMs);
    void Forget(uint32_t groupId);                          // binding released or run gone
    // hook side (map threads): return at once unless Enabled() and the guid is a member of some book
    void OnCast(uint32_t casterGuidLow, std::string const& spell, bool heal);
    void OnDamage(uint32_t attackerOwnerGuidLow, uint32_t victimOwnerGuidLow, uint32_t amount, std::string const& attackerName,
                  uint32_t enemyEntry, uint64_t enemyGuid, std::string const& enemyName);
    void OnHeal(uint32_t healerOwnerGuidLow, uint32_t amount, uint32_t overheal);
    void OnEnterCombat(uint32_t memberGuidLow);
    void OnDeath(uint32_t victimGuidLow, std::string const& killer, float x, float y, float z, uint32_t enemyEntryDied);
    // emitter / telemetry (world thread)
    void TakePayloads(std::vector<std::string>& out, int protocolVersion, uint64_t session, uint64_t& seq, int64_t ts);
    std::string WindowJson(uint32_t groupId) const;
    void NoteBinding(uint32_t groupId, std::string const& commander, uint32_t commanderGuid, uint32_t mapId);
private:
    CombatMonitorService() = default;
    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, CombatMonitor::Book> m_books;   // by group id
    std::unordered_map<uint32_t, uint32_t> m_memberGroup;        // member guid low -> group id
    std::unordered_map<uint32_t, std::string> m_commander;       // group id -> commander name (+ guid, map) for the payload
    std::unordered_map<uint32_t, uint32_t> m_commanderGuid;      // group id -> commander guid low
    std::unordered_map<uint32_t, uint32_t> m_mapId;               // group id -> the lead's map
    uint32_t m_lastNowMs = 0;
    int64_t m_lastUnixNow = 0;
    std::atomic<bool> m_enabled{false};
};

} // namespace TortoiseBots
