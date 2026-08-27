#pragma once

#include <cstdint>
#include <unordered_set>

class Player;
class Group;

namespace TortoiseBots
{

// Default-off autonomous WSG/AB/AV queue participation for live Headless
// random bots. Reuses the proven manual BattleGroundJoinAction path via
// WorldSession::HandleBattlemasterJoinOpcode (guid 1337 bypass). The core's
// BattleGroundMgr/BattleGroundQueue owns invite/queue updates; the existing
// PlayerbotAI SMSG_BATTLEFIELD_STATUS -> BGStatusAction -> HandleBattleFieldPortOpcode
// path accepts invites and sets +pvp strategies. No second queue, thread,
// arena, vehicle, expansion, DB tick scan, LFT, AH, or auto-create.
class BattlegroundQueueService
{
public:
    static BattlegroundQueueService& Instance();

    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

private:
    BattlegroundQueueService() = default;
    ~BattlegroundQueueService() = default;

    bool IsEligible(::Player* bot) const;
    bool TryQueue(::Player* bot);
    void ReconcileMasterQueue();
    bool IsGroupFullyBotOwned(::Group* group) const;
    bool HasLiveNonBotMember(::Group* group) const;
    void PruneOwnedQueueSet();

    bool m_initialized = false;
    uint32_t m_elapsedMs = 0;
    // In-memory ownership as (guidLow, queueType) pairs so an excluded
    // bracket/offline group member is never mistaken for a service-owned
    // entry, and a separate manual queueType for the same guid is not
    // cancelled during master reconciliation.
    std::unordered_set<uint64_t> m_ownedQueuedGuids;
    static uint64_t EncodeOwnedKey(uint32_t guidLow, uint32_t queueType) { return (uint64_t(guidLow) << 32) | queueType; }
};

} // namespace TortoiseBots
