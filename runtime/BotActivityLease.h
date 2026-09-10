#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{

enum class BotActivity : uint8_t
{
    Idle = 0,
    Grinding,
    Trading,
    LftQueued,
    BgQueued,
    PlayerMaster,
};

inline char const* BotActivityName(BotActivity activity)
{
    switch (activity)
    {
        case BotActivity::Idle: return "Idle";
        case BotActivity::Grinding: return "Grinding";
        case BotActivity::Trading: return "Trading";
        case BotActivity::LftQueued: return "LftQueued";
        case BotActivity::BgQueued: return "BgQueued";
        case BotActivity::PlayerMaster: return "PlayerMaster";
    }
    return "Unknown";
}

struct ActivityLease
{
    BotActivity activity = BotActivity::Idle;
    uint32_t acquiredTick = 0;
    uint32_t maxDurationMs = 0;
};

struct ActivityLeaseInfo
{
    uint32_t guidLow = 0;
    ActivityLease lease;
};

// Centralized in-memory arbitration of background-service intent (issue #89).
//
// World-thread only: console/chat/RA commands and all background service ticks
// run on the single world thread, so no mutex is used. Authoritative host
// state (group, master, BG, LFT) remains the backstop inside each service;
// the lease only arbitrates intent among background services for unowned bots.
class BotActivityLeaseManager
{
public:
    static BotActivityLeaseManager& Instance();

    // Try to acquire a lease. Granted when Idle, on same-activity re-acquire
    // (extends duration), or via valid preemption over Grinding. PlayerMaster
    // can never be acquired or preempted through this path; use ClaimForMaster.
    bool TryAcquire(uint32_t guidLow, BotActivity activity, uint32_t maxDurationMs = 0);

    // Release only when the current owner matches (prevents stale releases
    // from clearing a newer owner). A failed preemption may restore a prior
    // Grinding lease instead of leaving an autonomous bot unowned.
    void Release(uint32_t guidLow, BotActivity expectedActivity,
        BotActivity restoreActivity = BotActivity::Idle);

    // Human master claim: evicts any background lease with active cleanup,
    // then locks to PlayerMaster (indefinite).
    void ClaimForMaster(uint32_t guidLow);

    // Release a master claim back to Idle.
    void ReleaseMaster(uint32_t guidLow);

    BotActivity GetActivity(uint32_t guidLow) const;
    bool IsIdle(uint32_t guidLow) const;
    // True when Idle or Grinding (grinding is soft and preemptable).
    bool IsAvailableForBackground(uint32_t guidLow) const;

    // Remaining lease time in ms, or 0 for indefinite / no lease.
    uint32_t GetRemainingMs(uint32_t guidLow) const;

    void GetActivityCounts(uint32_t& grinding, uint32_t& trading, uint32_t& lft,
        uint32_t& bg, uint32_t& master) const;
    std::vector<ActivityLeaseInfo> GetActiveLeases() const;

    void Update(uint32_t diff);
    void Clear();

private:
    BotActivityLeaseManager() = default;
    ~BotActivityLeaseManager() = default;

    void EvictService(uint32_t guidLow, BotActivity evictedActivity);

    std::unordered_map<uint32_t, ActivityLease> m_leases;
    uint32_t m_elapsedMs = 0;
};

} // namespace TortoiseBots
