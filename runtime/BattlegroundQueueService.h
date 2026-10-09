#pragma once

#include <cstdint>
#include <unordered_set>

class Player;
class Group;

namespace TortoiseBots
{

// Default-on demand-aware WSG/AB/AV queue participation for live Headless
// random bots. The service uses the core's copy-only queued-participant demand
// snapshot to match human-waiting queue type/bracket and underrepresented team.
// It joins through WorldSession::HandleBattlemasterJoinOpcode (guid 1337 bypass)
// and leaves through HandleBattleFieldPortOpcode action=0; core owns queue,
// invites, and port events. InBattleGround, LFT, group, master, and owned-pair
// guards remain module policy without exposing queue internals. No blind
// periodic queueing, second queue, thread, arena, vehicle, expansion, or DB scan.
//
// Opt-in autonomous bot-only WSG (AiPlayerbot.RandomBotBgAutonomous, default
// off): when no human demand exists, seeds one 10v10 WSG in the bracket
// nearest the pool median level, capped at one concurrent instance for an
// average PC. Queued-but-unstarted seeds block new seeds (anti-over-queue).
class BattlegroundQueueService
{
public:
    static BattlegroundQueueService& Instance();

    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

    // Activity-lease eviction hook (issue #89): cancels owned native BG queue
    // entries and prunes ownership. Must not touch the lease map.
    void OnLeaseEvicted(uint32_t guidLow);

private:
    BattlegroundQueueService() = default;
    ~BattlegroundQueueService() = default;

    bool IsEligible(::Player* bot) const;
    bool TryQueue(::Player* bot, uint32_t queueType);
    void ReconcileMasterQueue();
    bool IsGroupFullyBotOwned(::Group* group) const;
    bool HasLiveNonBotMember(::Group* group) const;
    void PruneOwnedQueueSet();
    // Autonomous seeding: counts and queue helpers. Pure counting over the
    // in-memory bot snapshot and the copy-only participant snapshot.
    void UpdateAutonomousSeeding();
    uint32_t CountRunningBotOnlyWsg() const;
    uint32_t CountOwnedQueuedFor(uint32_t queueTypeValue, uint32_t bracketIndex) const;
    uint32_t CountSeededForTeam(uint32_t queueTypeValue, uint32_t bracketIndex, uint32_t team) const;

    bool m_initialized = false;
    uint32_t m_elapsedMs = 0;
    uint32_t m_reconcileElapsedMs = 0;
    // In-memory ownership as (guidLow, queueType) pairs so an excluded
    // bracket/offline group member is never mistaken for a service-owned
    // entry, and a separate manual queueType for the same guid is not
    // cancelled during master reconciliation.
    std::unordered_set<uint64_t> m_ownedQueuedGuids;
    static uint64_t EncodeOwnedKey(uint32_t guidLow, uint32_t queueType) { return (uint64_t(guidLow) << 32) | queueType; }
};

} // namespace TortoiseBots
