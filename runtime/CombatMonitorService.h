#pragma once
// Per-fight combat numbers behind the core's damage, heal, cast, combat and
// death hooks: plain data and the standard library only; the books themselves
// are runtime/CombatMonitorPolicy.h. The hooks run on map threads; the 1 Hz
// run tick and the emitter's snapshot cycle run on the world thread.
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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
    void SyncConfig(bool on);                               // ungated: stores the flag, drops the books on the on->off turn only
    // world thread, 1 Hz from the run tick, for every bound group with a run
    void Refresh(uint32_t groupId, std::vector<CombatMonitor::MemberInfo> members, bool anyInCombat, bool allDead,
                 CombatMonitor::FightContext const& ctx, std::vector<std::string> const& debuffKeysUp, uint32_t nowMs);
    void Forget(uint32_t groupId);                          // binding released or run gone
    // hook side (map threads): a guid that misses the member bitset returns with no lock, refcount or
    // allocation; a hit (member or hash collision) checks the member snapshot, and only a member locks
    void OnCast(uint32_t casterGuidLow, char const* spell, bool heal);
    void OnDamage(uint32_t attackerOwnerGuidLow, uint32_t victimOwnerGuidLow, uint32_t amount, char const* attackerName,
                  uint32_t enemyEntry, uint64_t enemyGuid, char const* enemyName);
    void OnHeal(uint32_t healerOwnerGuidLow, uint32_t amount, uint32_t overheal);
    void OnEnterCombat(uint32_t memberGuidLow);
    void OnDeath(uint32_t victimGuidLow, uint32_t killerOwnerGuidLow, char const* killerName, float x, float y, float z,
                 uint32_t enemyEntryDied);
    // emitter / telemetry (world thread)
    void TakePayloads(std::vector<std::string>& out, int protocolVersion, uint64_t session,
                      std::function<uint64_t()> const& nextSeq, int64_t ts);
    std::string WindowJson(uint32_t groupId);
    void NoteBinding(uint32_t groupId, std::string const& commander, uint32_t commanderGuid, uint32_t mapId);
#ifdef COMBAT_MONITOR_TEST_CONFIG
    static uint64_t TestHookLockCount();                    // hook-path mutex takes since process start (tests only)
    static uint64_t TestSnapshotLoadCount();                // hook-path snapshot loads (bitset hits) since process start
    static uint64_t TestOversizeDropCount();                // fights dropped as too big for any payload form
    static uint64_t TestPublishCount();                     // member-set republishes since process start
#endif
private:
    using MemberMap = std::unordered_map<uint32_t, uint32_t>;   // member guid low -> group id
    void PublishMembers();                                  // m_mutex held: bitset, snapshot and count follow the member map
    static constexpr uint32_t kBitsetBits = 8192;           // member pre-filter: one bit per hashed guid low
    static constexpr uint32_t kBitsetWords = kBitsetBits / 64;
    static uint32_t BitOf(uint32_t guidLow)                 // Fibonacci hash of the guid low to a bit index
    {
        return static_cast<uint32_t>(guidLow * 2654435769u) >> (32 - 13);
    }
    static_assert(kBitsetBits == (1u << 13), "BitOf() keeps the top 13 bits of the hash");
    bool MaybeMember(uint32_t guidLow) const;               // lock-free bitset test (acquire); false: surely no member
    static void NoteSnapshotLoad();                         // counts hook-path snapshot loads (test builds only)
    void StartBook(uint32_t groupId, CombatMonitor::Book& book,
                   CombatMonitor::FightContext const& ctx);  // m_mutex held: next per-group number when a fight starts
    static void NoteHookLock();                             // counts hook-path mutex takes (test builds only)
    CombatMonitorService() = default;
    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, CombatMonitor::Book> m_books;   // by group id
    MemberMap m_memberGroup;                                     // member guid low -> group id
    // Hook-side copy of the member map, read with std::atomic_load (libstdc++ guards it with a pooled
    // mutex, so hooks reach it only after a bitset hit) and replaced under m_mutex.
    std::shared_ptr<MemberMap const> m_snapshot = std::make_shared<MemberMap>();
    std::array<std::atomic<uint64_t>, kBitsetWords> m_memberBits{};   // bit BitOf(guid) set for every member guid
    std::atomic<uint32_t> m_memberCount{0};
    std::unordered_map<uint32_t, CombatMonitor::FightContext> m_ctx;   // group's latest context, for enter-combat starts
    std::unordered_map<uint32_t, uint32_t> m_fightSeq;      // per group, survives Forget()
    std::unordered_map<uint32_t, std::string> m_commander;       // group id -> commander name (+ guid, map) for the payload
    std::unordered_map<uint32_t, uint32_t> m_commanderGuid;      // group id -> commander guid low
    std::unordered_map<uint32_t, uint32_t> m_mapId;               // group id -> the lead's map
    uint32_t m_lastNowMs = 0;
    int64_t m_lastUnixNow = 0;
    std::atomic<bool> m_enabled{false};
};

} // namespace TortoiseBots
