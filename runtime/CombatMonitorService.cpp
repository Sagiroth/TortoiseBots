// Per-fight combat numbers behind the core's hooks: the books aggregate one
// fight per bound group; the world thread ticks them at 1 Hz and the emitter
// takes the finished fights and the live window. Hook paths return on the
// member count before any allocation, then on a lock-free member snapshot;
// the mutex is only taken for a member event.
#include "CombatMonitorService.h"
// Test seam: compile the service TU with -DCOMBAT_MONITOR_TEST_CONFIG and put a
// stub "playerbot/PlayerbotAIConfig.h" (struct PlayerbotAIConfig { bool
// combatMonitor; } + sPlayerbotAIConfig) on the include path.
#ifdef COMBAT_MONITOR_TEST_CONFIG
#include "playerbot/PlayerbotAIConfig.h"
#else
#include "../ai/playerbot/PlayerbotAIConfig.h"
#endif

namespace TortoiseBots {

namespace {

#ifdef COMBAT_MONITOR_TEST_CONFIG
std::atomic<uint64_t> g_hookLocks{0};
#endif

} // namespace

CombatMonitorService& CombatMonitorService::Instance()
{
    static CombatMonitorService instance;
    return instance;
}

bool CombatMonitorService::Enabled() const
{
    return m_enabled.load(std::memory_order_relaxed);
}

void CombatMonitorService::SyncConfig(bool on)
{
    m_enabled.store(on, std::memory_order_relaxed);
    if (on)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_books.clear();
    m_memberGroup.clear();
    m_ctx.clear();
    PublishMembers();
}

void CombatMonitorService::PublishMembers()
{
    m_snapshot = std::make_shared<MemberMap const>(m_memberGroup);
    m_memberCount.store(static_cast<uint32_t>(m_memberGroup.size()), std::memory_order_relaxed);
}

void CombatMonitorService::NoteHookLock()
{
#ifdef COMBAT_MONITOR_TEST_CONFIG
    g_hookLocks.fetch_add(1, std::memory_order_relaxed);
#endif
}

#ifdef COMBAT_MONITOR_TEST_CONFIG
uint64_t CombatMonitorService::TestHookLockCount()
{
    return g_hookLocks.load(std::memory_order_relaxed);
}
#endif

void CombatMonitorService::StartBook(uint32_t groupId, CombatMonitor::Book& book,
                                     CombatMonitor::FightContext const& ctx)
{
    book.Start(m_lastNowMs, m_lastUnixNow, ctx, ++m_fightSeq[groupId]);
}

void CombatMonitorService::Refresh(uint32_t groupId, std::vector<CombatMonitor::MemberInfo> members, bool anyInCombat,
                                   bool allDead, CombatMonitor::FightContext const& ctx,
                                   std::vector<std::string> const& debuffKeysUp, uint32_t nowMs)
{
    if (!Enabled() || groupId == 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastNowMs = nowMs;
    m_lastUnixNow = static_cast<int64_t>(time(nullptr));
    m_ctx[groupId] = ctx;
    CombatMonitor::Book& book = m_books[groupId];
    for (auto it = m_memberGroup.begin(); it != m_memberGroup.end();)
        it = it->second == groupId ? m_memberGroup.erase(it) : std::next(it);
    book.SetMembers(members);
    for (CombatMonitor::MemberInfo const& m : members)
        if (m.guid)
            m_memberGroup[m.guid] = groupId;
    PublishMembers();
    if (anyInCombat && !book.InFight())
        StartBook(groupId, book, ctx);
    book.SampleDebuffs(debuffKeysUp);
    book.SetInCombat(anyInCombat, allDead, nowMs);
}

void CombatMonitorService::Forget(uint32_t groupId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_books.erase(groupId);
    for (auto it = m_memberGroup.begin(); it != m_memberGroup.end();)
        it = it->second == groupId ? m_memberGroup.erase(it) : std::next(it);
    m_ctx.erase(groupId);
    PublishMembers();
    m_commander.erase(groupId);
    m_commanderGuid.erase(groupId);
    m_mapId.erase(groupId);
}

void CombatMonitorService::OnCast(uint32_t casterGuidLow, char const* spell, bool heal)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    std::shared_ptr<MemberMap const> members = std::atomic_load(&m_snapshot);
    auto const member = members->find(casterGuidLow);
    if (member == members->end())
        return;
    uint32_t const group = member->second;
    NoteHookLock();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(group);
    if (book == m_books.end())
        return;
    book->second.OnCast(casterGuidLow, spell ? spell : "", heal, m_lastNowMs);
}

void CombatMonitorService::OnDamage(uint32_t attackerOwnerGuidLow, uint32_t victimOwnerGuidLow, uint32_t amount,
                                     char const* attackerName, uint32_t enemyEntry, uint64_t enemyGuid,
                                     char const* enemyName)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    std::shared_ptr<MemberMap const> members = std::atomic_load(&m_snapshot);
    auto const atk = attackerOwnerGuidLow ? members->find(attackerOwnerGuidLow) : members->end();
    auto const vic = victimOwnerGuidLow ? members->find(victimOwnerGuidLow) : members->end();
    if (atk == members->end() && vic == members->end())
        return;
    uint32_t const group = atk != members->end() ? atk->second : vic->second;
    NoteHookLock();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(group);
    if (book == m_books.end())
        return;
    book->second.OnDamage(atk != members->end() ? attackerOwnerGuidLow : 0,
                          vic != members->end() ? victimOwnerGuidLow : 0, amount,
                          attackerName ? attackerName : "", enemyEntry, enemyGuid, enemyName ? enemyName : "",
                          m_lastNowMs);
}

void CombatMonitorService::OnHeal(uint32_t healerOwnerGuidLow, uint32_t amount, uint32_t overheal)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    std::shared_ptr<MemberMap const> members = std::atomic_load(&m_snapshot);
    auto const member = members->find(healerOwnerGuidLow);
    if (member == members->end())
        return;
    uint32_t const group = member->second;
    NoteHookLock();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(group);
    if (book == m_books.end())
        return;
    book->second.OnHeal(healerOwnerGuidLow, amount, overheal, m_lastNowMs);
}

void CombatMonitorService::OnEnterCombat(uint32_t memberGuidLow)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    std::shared_ptr<MemberMap const> members = std::atomic_load(&m_snapshot);
    auto const member = members->find(memberGuidLow);
    if (member == members->end())
        return;
    uint32_t const group = member->second;
    NoteHookLock();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(group);
    if (book == m_books.end())
        return;
    if (!book->second.InFight())
    {
        auto const ctx = m_ctx.find(group);
        StartBook(group, book->second, ctx != m_ctx.end() ? ctx->second : CombatMonitor::FightContext{});
    }
}

void CombatMonitorService::OnDeath(uint32_t victimGuidLow, uint32_t killerOwnerGuidLow, char const* killer, float x,
                                   float y, float z, uint32_t enemyEntryDied)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    std::shared_ptr<MemberMap const> members = std::atomic_load(&m_snapshot);
    if (victimGuidLow)
    {
        auto const member = members->find(victimGuidLow);
        if (member == members->end())
            return;
        uint32_t const group = member->second;
        NoteHookLock();
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const book = m_books.find(group);
        if (book == m_books.end())
            return;
        book->second.OnDeath(victimGuidLow, killer ? killer : "", x, y, z, m_lastNowMs);
        return;
    }
    if (enemyEntryDied == 0)
        return;
    auto const killerMember = killerOwnerGuidLow ? members->find(killerOwnerGuidLow) : members->end();
    if (killerMember == members->end())
        return;
    uint32_t const group = killerMember->second;
    NoteHookLock();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(group);
    if (book == m_books.end())
        return;
    book->second.NoteBossDied(enemyEntryDied);
}

void CombatMonitorService::TakePayloads(std::vector<std::string>& out, int protocolVersion, uint64_t session,
                                         std::function<uint64_t()> const& nextSeq, int64_t ts)
{
    out.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& kv : m_books)
    {
        uint32_t commanderGuid = 0;
        if (auto const it = m_commanderGuid.find(kv.first); it != m_commanderGuid.end())
            commanderGuid = it->second;
        uint32_t mapId = 0;
        if (auto const it = m_mapId.find(kv.first); it != m_mapId.end())
            mapId = it->second;
        if (!kv.second.HasTakeablePayloads())
            continue;
        std::vector<std::string> payloads = kv.second.TakeFightPayloads(protocolVersion, session, nextSeq, ts,
                                                                         m_commander[kv.first], commanderGuid,
                                                                         kv.first, mapId);
        for (std::string& payload : payloads)
            out.push_back(std::move(payload));
    }
}

void CombatMonitorService::NoteBinding(uint32_t groupId, std::string const& commander,
                              uint32_t commanderGuid, uint32_t mapId)
{
    if (!Enabled() || groupId == 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_commander[groupId] = commander;
    m_commanderGuid[groupId] = commanderGuid;
    m_mapId[groupId] = mapId;
}

std::string CombatMonitorService::WindowJson(uint32_t groupId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(groupId);
    if (book == m_books.end())
        return "";
    return book->second.WindowJson(m_lastNowMs);
}

} // namespace TortoiseBots
