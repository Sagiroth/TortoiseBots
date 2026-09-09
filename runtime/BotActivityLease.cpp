#include "BotActivityLease.h"

#include "AhMarketService.h"
#include "BattlegroundQueueService.h"
#include "LftBotFillService.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"

#include <sstream>

namespace TortoiseBots
{

BotActivityLeaseManager& BotActivityLeaseManager::Instance()
{
    static BotActivityLeaseManager instance;
    return instance;
}

bool BotActivityLeaseManager::TryAcquire(uint32_t guidLow, BotActivity activity, uint32_t maxDurationMs)
{
    if (!guidLow || activity == BotActivity::Idle)
        return false;
    // PlayerMaster is absolute and can only be entered via ClaimForMaster.
    if (activity == BotActivity::PlayerMaster)
        return false;

    auto it = m_leases.find(guidLow);
    if (it == m_leases.end())
    {
        m_leases.emplace(guidLow, ActivityLease{activity, m_elapsedMs, maxDurationMs});
        return true;
    }

    BotActivity current = it->second.activity;
    if (current == BotActivity::Idle)
    {
        it->second = ActivityLease{activity, m_elapsedMs, maxDurationMs};
        return true;
    }
    if (current == activity)
    {
        // Idempotent re-grant: extend duration.
        it->second.acquiredTick = m_elapsedMs;
        it->second.maxDurationMs = maxDurationMs;
        return true;
    }
    if (current == BotActivity::PlayerMaster)
        return false;
    // Grinding is soft: structured background work preempts it.
    if (current == BotActivity::Grinding)
    {
        EvictService(guidLow, current);
        it->second = ActivityLease{activity, m_elapsedMs, maxDurationMs};
        return true;
    }
    // Trading / LftQueued / BgQueued are mutually exclusive.
    return false;
}

void BotActivityLeaseManager::Release(uint32_t guidLow, BotActivity expectedActivity)
{
    auto it = m_leases.find(guidLow);
    if (it != m_leases.end() && it->second.activity == expectedActivity)
        m_leases.erase(it);
}

void BotActivityLeaseManager::ClaimForMaster(uint32_t guidLow)
{
    if (!guidLow)
        return;
    auto it = m_leases.find(guidLow);
    if (it != m_leases.end() && it->second.activity != BotActivity::Idle &&
        it->second.activity != BotActivity::PlayerMaster)
        EvictService(guidLow, it->second.activity);
    m_leases[guidLow] = ActivityLease{BotActivity::PlayerMaster, m_elapsedMs, 0};
}

void BotActivityLeaseManager::ReleaseMaster(uint32_t guidLow)
{
    auto it = m_leases.find(guidLow);
    if (it != m_leases.end() && it->second.activity == BotActivity::PlayerMaster)
        m_leases.erase(it);
}

BotActivity BotActivityLeaseManager::GetActivity(uint32_t guidLow) const
{
    auto it = m_leases.find(guidLow);
    if (it == m_leases.end())
        return BotActivity::Idle;
    return it->second.activity;
}

bool BotActivityLeaseManager::IsIdle(uint32_t guidLow) const
{
    return GetActivity(guidLow) == BotActivity::Idle;
}

bool BotActivityLeaseManager::IsAvailableForBackground(uint32_t guidLow) const
{
    BotActivity activity = GetActivity(guidLow);
    return activity == BotActivity::Idle || activity == BotActivity::Grinding;
}

uint32_t BotActivityLeaseManager::GetRemainingMs(uint32_t guidLow) const
{
    auto it = m_leases.find(guidLow);
    if (it == m_leases.end() || !it->second.maxDurationMs)
        return 0;
    uint32_t elapsed = m_elapsedMs - it->second.acquiredTick;
    if (elapsed >= it->second.maxDurationMs)
        return 0;
    return it->second.maxDurationMs - elapsed;
}

void BotActivityLeaseManager::GetActivityCounts(uint32_t& grinding, uint32_t& trading,
    uint32_t& lft, uint32_t& bg, uint32_t& master) const
{
    grinding = trading = lft = bg = master = 0;
    for (auto const& kv : m_leases)
    {
        switch (kv.second.activity)
        {
            case BotActivity::Grinding: ++grinding; break;
            case BotActivity::Trading: ++trading; break;
            case BotActivity::LftQueued: ++lft; break;
            case BotActivity::BgQueued: ++bg; break;
            case BotActivity::PlayerMaster: ++master; break;
            case BotActivity::Idle: break;
        }
    }
}

std::string BotActivityLeaseManager::GetStatusString() const
{
    uint32_t grinding, trading, lft, bg, master;
    GetActivityCounts(grinding, trading, lft, bg, master);
    std::ostringstream out;
    out << "Leases: Grinding " << grinding << ", Trading " << trading
        << ", LftQueued " << lft << ", BgQueued " << bg
        << ", PlayerMaster " << master << " (tracked " << m_leases.size() << ")";
    return out.str();
}

void BotActivityLeaseManager::Update(uint32_t diff)
{
    m_elapsedMs += diff;
    for (auto it = m_leases.begin(); it != m_leases.end(); )
    {
        if (!it->second.maxDurationMs ||
            (m_elapsedMs - it->second.acquiredTick) <= it->second.maxDurationMs)
        {
            ++it;
            continue;
        }
        uint32_t guidLow = it->first;
        BotActivity activity = it->second.activity;
        sLog.outError("TortoiseBots: activity lease %s for bot %u expired after %u ms, evicting",
            BotActivityName(activity), guidLow, it->second.maxDurationMs);
        // Evict first so the owning service cancels native queue/teleport
        // state, then drop the lease back to Idle.
        EvictService(guidLow, activity);
        it = m_leases.erase(it);
    }
}

void BotActivityLeaseManager::Clear()
{
    m_leases.clear();
    m_elapsedMs = 0;
}

void BotActivityLeaseManager::EvictService(uint32_t guidLow, BotActivity evictedActivity)
{
    // Active cleanup: a queued bot could pop a match before the evicted
    // service's next tick, so cancel synchronously. Hooks must not touch
    // the lease map (the manager owns the transition).
    switch (evictedActivity)
    {
        case BotActivity::LftQueued:
            LftBotFillService::Instance().OnLeaseEvicted(guidLow);
            break;
        case BotActivity::BgQueued:
            BattlegroundQueueService::Instance().OnLeaseEvicted(guidLow);
            break;
        case BotActivity::Trading:
            AhMarketService::Instance().OnLeaseEvicted(guidLow);
            break;
        case BotActivity::Grinding:
        case BotActivity::Idle:
        case BotActivity::PlayerMaster:
            break;
    }
}

} // namespace TortoiseBots
