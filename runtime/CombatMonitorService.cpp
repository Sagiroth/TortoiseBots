// Per-fight combat numbers behind the core's hooks: the books aggregate one
// fight per bound group; the world thread ticks them at 1 Hz and the emitter
// takes the finished fights and the live window. Hook paths allocate nothing
// past one map lookup while the guid is not a member.
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

CombatMonitorService& CombatMonitorService::Instance()
{
    static CombatMonitorService instance;
    return instance;
}

bool CombatMonitorService::Enabled() const
{
    return m_enabled.load(std::memory_order_relaxed);
}

void CombatMonitorService::Refresh(uint32_t groupId, std::vector<CombatMonitor::MemberInfo> members, bool anyInCombat,
                                   bool allDead, CombatMonitor::FightContext const& ctx,
                                   std::vector<std::string> const& debuffKeysUp, uint32_t nowMs)
{
    m_enabled.store(sPlayerbotAIConfig.combatMonitor, std::memory_order_relaxed);
    if (!Enabled() || groupId == 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastNowMs = nowMs;
    m_lastUnixNow = static_cast<int64_t>(time(nullptr));
    CombatMonitor::Book& book = m_books[groupId];
    for (auto it = m_memberGroup.begin(); it != m_memberGroup.end();)
        it = it->second == groupId ? m_memberGroup.erase(it) : std::next(it);
    book.SetMembers(members);
    for (CombatMonitor::MemberInfo const& m : members)
        if (m.guid)
            m_memberGroup[m.guid] = groupId;
    if (!book.InFight())
        book.Start(nowMs, m_lastUnixNow, ctx);
    book.SampleDebuffs(debuffKeysUp);
    book.SetInCombat(anyInCombat, allDead, nowMs);
}

void CombatMonitorService::Forget(uint32_t groupId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_books.erase(groupId);
    for (auto it = m_memberGroup.begin(); it != m_memberGroup.end();)
        it = it->second == groupId ? m_memberGroup.erase(it) : std::next(it);
    m_commander.erase(groupId);
    m_commanderGuid.erase(groupId);
    m_mapId.erase(groupId);
    m_enabled.store(sPlayerbotAIConfig.combatMonitor, std::memory_order_relaxed);
}

void CombatMonitorService::OnCast(uint32_t casterGuidLow, std::string const& spell, bool heal)
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const member = m_memberGroup.find(casterGuidLow);
    if (member == m_memberGroup.end())
        return;
    auto const book = m_books.find(member->second);
    if (book == m_books.end())
        return;
    book->second.OnCast(casterGuidLow, spell, heal, m_lastNowMs);
}

void CombatMonitorService::OnDamage(uint32_t attackerOwnerGuidLow, uint32_t victimOwnerGuidLow, uint32_t amount,
                                     std::string const& attackerName, uint32_t enemyEntry, uint64_t enemyGuid,
                                     std::string const& enemyName)
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const atk = attackerOwnerGuidLow ? m_memberGroup.find(attackerOwnerGuidLow) : m_memberGroup.end();
    auto const vic = victimOwnerGuidLow ? m_memberGroup.find(victimOwnerGuidLow) : m_memberGroup.end();
    if (atk == m_memberGroup.end() && vic == m_memberGroup.end())
        return;
    uint32_t const group = atk != m_memberGroup.end() ? atk->second : vic->second;
    auto const book = m_books.find(group);
    if (book == m_books.end())
        return;
    book->second.OnDamage(atk != m_memberGroup.end() ? attackerOwnerGuidLow : 0,
                          vic != m_memberGroup.end() ? victimOwnerGuidLow : 0, amount,
                          attackerName, enemyEntry, enemyGuid, enemyName, m_lastNowMs);
}

void CombatMonitorService::OnHeal(uint32_t healerOwnerGuidLow, uint32_t amount, uint32_t overheal)
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const member = m_memberGroup.find(healerOwnerGuidLow);
    if (member == m_memberGroup.end())
        return;
    auto const book = m_books.find(member->second);
    if (book == m_books.end())
        return;
    book->second.OnHeal(healerOwnerGuidLow, amount, overheal, m_lastNowMs);
}

void CombatMonitorService::OnEnterCombat(uint32_t memberGuidLow)
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const member = m_memberGroup.find(memberGuidLow);
    if (member == m_memberGroup.end())
        return;
    auto const book = m_books.find(member->second);
    if (book == m_books.end())
        return;
    if (!book->second.InFight())
        book->second.Start(m_lastNowMs, m_lastUnixNow, CombatMonitor::FightContext{});
}

void CombatMonitorService::OnDeath(uint32_t victimGuidLow, std::string const& killer, float x, float y, float z,
                                   uint32_t enemyEntryDied)
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (victimGuidLow)
    {
        auto const member = m_memberGroup.find(victimGuidLow);
        if (member == m_memberGroup.end())
            return;
        auto const book = m_books.find(member->second);
        if (book == m_books.end())
            return;
        book->second.OnDeath(victimGuidLow, killer, x, y, z, m_lastNowMs);
        return;
    }
    for (auto& kv : m_books)
        kv.second.NoteBossDied(enemyEntryDied);
}

void CombatMonitorService::TakePayloads(std::vector<std::string>& out, int protocolVersion, uint64_t session,
                                         uint64_t& seq, int64_t ts)
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
        std::vector<std::string> payloads = kv.second.TakeFightPayloads(protocolVersion, session, seq, ts,
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

std::string CombatMonitorService::WindowJson(uint32_t groupId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(groupId);
    if (book == m_books.end())
        return "";
    return book->second.WindowJson(m_lastNowMs);
}

} // namespace TortoiseBots
