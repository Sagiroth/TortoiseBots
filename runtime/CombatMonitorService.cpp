// Per-fight combat numbers behind the core's hooks: the books aggregate one
// fight per bound group; the world thread ticks them at 1 Hz and the emitter
// takes the finished fights and the live window. Hook paths return on the
// member count, then on an atomic member bitset (one acquire load per guid, no
// lock, no refcount, no allocation); only a bitset hit (a member or a hash
// collision) loads the shared member snapshot, and the mutex is only taken for
// a member event.
#include "CombatMonitorService.h"
// Test seam: compile the service TU with -DCOMBAT_MONITOR_TEST_CONFIG and put a
// stub "playerbot/PlayerbotAIConfig.h" (struct PlayerbotAIConfig { bool
// combatMonitor; } + sPlayerbotAIConfig) on the include path.
#ifdef COMBAT_MONITOR_TEST_CONFIG
#include "playerbot/PlayerbotAIConfig.h"
#else
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "Log.h"
#endif

namespace TortoiseBots {

namespace {

#ifdef COMBAT_MONITOR_TEST_CONFIG
std::atomic<uint64_t> g_hookLocks{0};
std::atomic<uint64_t> g_snapshotLoads{0};
std::atomic<uint64_t> g_oversizeDrops{0};
std::atomic<uint64_t> g_publishes{0};
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
    bool const was = m_enabled.exchange(on, std::memory_order_relaxed);
    if (on || !was)
        return;   // only the on->off turn drops the books (the 1 Hz callers sync every tick)
    std::lock_guard<std::mutex> lock(m_mutex);
    m_books.clear();
    m_retired.clear();
    m_retiredMeta.clear();
    m_lastEventSend.clear();
    m_memberGroup.clear();
    m_ctx.clear();
    m_inFightCount.store(0, std::memory_order_relaxed);
    PublishMembers();
}

// Readers never see a false negative for a current member: the new members'
// bits are OR-ed in before the snapshot holds them, and the bits of removed
// members are cleared only after the snapshot dropped them (a reader in
// between sees a hit and misses in the snapshot).
void CombatMonitorService::PublishMembers()
{
    std::array<uint64_t, kBitsetWords> bits{};
    for (auto const& kv : m_memberGroup)
        bits[BitOf(kv.first) / 64] |= uint64_t(1) << (BitOf(kv.first) % 64);
    for (uint32_t i = 0; i < kBitsetWords; ++i)
        if (bits[i])
            m_memberBits[i].fetch_or(bits[i], std::memory_order_release);
    std::atomic_store(&m_snapshot, std::make_shared<MemberMap const>(m_memberGroup));
    m_memberCount.store(static_cast<uint32_t>(m_memberGroup.size()), std::memory_order_relaxed);
    for (uint32_t i = 0; i < kBitsetWords; ++i)
        m_memberBits[i].store(bits[i], std::memory_order_release);
#ifdef COMBAT_MONITOR_TEST_CONFIG
    g_publishes.fetch_add(1, std::memory_order_relaxed);
#endif
}

bool CombatMonitorService::MaybeMember(uint32_t guidLow) const
{
    if (guidLow == 0)
        return false;
    uint32_t const bit = BitOf(guidLow);
    return (m_memberBits[bit / 64].load(std::memory_order_acquire) >> (bit % 64)) & 1;
}

void CombatMonitorService::NoteSnapshotLoad()
{
#ifdef COMBAT_MONITOR_TEST_CONFIG
    g_snapshotLoads.fetch_add(1, std::memory_order_relaxed);
#endif
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

uint64_t CombatMonitorService::TestSnapshotLoadCount()
{
    return g_snapshotLoads.load(std::memory_order_relaxed);
}

uint64_t CombatMonitorService::TestOversizeDropCount()
{
    return g_oversizeDrops.load(std::memory_order_relaxed);
}

uint64_t CombatMonitorService::TestPublishCount()
{
    return g_publishes.load(std::memory_order_relaxed);
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
    // The member set (and with it the bitset and snapshot) is republished only when it changed.
    size_t held = 0;
    for (auto const& kv : m_memberGroup)
        held += kv.second == groupId ? 1 : 0;
    std::unordered_map<uint32_t, uint32_t> wanted;
    bool changed = false;
    for (CombatMonitor::MemberInfo const& m : members)
    {
        if (!m.guid || !wanted.emplace(m.guid, groupId).second)
            continue;
        auto const it = m_memberGroup.find(m.guid);
        changed = changed || it == m_memberGroup.end() || it->second != groupId;
    }
    changed = changed || wanted.size() != held;
    book.SetMembers(members);
    if (changed)
    {
        for (auto it = m_memberGroup.begin(); it != m_memberGroup.end();)
            it = it->second == groupId ? m_memberGroup.erase(it) : std::next(it);
        for (auto const& kv : wanted)
            m_memberGroup[kv.first] = groupId;
        PublishMembers();
    }
    bool const wasFighting = book.InFight();
    if (anyInCombat && !wasFighting)
        StartBook(groupId, book, ctx);
    book.SampleDebuffs(debuffKeysUp);
    book.SetInCombat(anyInCombat, allDead, nowMs);
    NoteFightCount(wasFighting, book.InFight());
}

// The sampler's boss: the first pick of a fight wins, and a run's own boss is
// never replaced (Book::NoteBoss).
void CombatMonitorService::NoteBoss(uint32_t groupId, uint32_t entry, char const* name)
{
    if (!Enabled() || groupId == 0 || entry == 0 || !name)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const it = m_books.find(groupId);
    if (it == m_books.end())
        return;
    it->second.NoteBoss(entry, name);
}

// The sampler's lead stayed away past its grace: the open fight is kept, with
// outcome unfinished unless it was a wipe or the boss already fell.
void CombatMonitorService::CloseUnfinished(uint32_t groupId, uint32_t nowMs)
{
    if (!Enabled() || groupId == 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const it = m_books.find(groupId);
    if (it == m_books.end())
        return;
    bool const wasFighting = it->second.InFight();
    it->second.CloseUnfinished(nowMs);
    NoteFightCount(wasFighting, it->second.InFight());
}

std::vector<CombatMonitor::EngagedUnit> CombatMonitorService::EngagedNow(uint32_t groupId) const
{
    std::vector<CombatMonitor::EngagedUnit> out;
    if (!Enabled() || groupId == 0)
        return out;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const it = m_books.find(groupId);
    if (it == m_books.end())
        return out;
    return it->second.EngagedNow();
}

bool CombatMonitorService::InFight(uint32_t groupId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const it = m_books.find(groupId);
    return it != m_books.end() && it->second.InFight();
}

bool CombatMonitorService::AnyInFight() const
{
    return m_inFightCount.load(std::memory_order_relaxed) != 0;
}

void CombatMonitorService::NoteFightCount(bool before, bool after)
{
    if (before == after)
        return;
    if (after)
        m_inFightCount.fetch_add(1, std::memory_order_relaxed);
    else
        m_inFightCount.fetch_sub(1, std::memory_order_relaxed);
}

void CombatMonitorService::Forget(uint32_t groupId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool retired = false;
    if (auto const it = m_books.find(groupId); it != m_books.end())
    {
        bool const wasFighting = it->second.InFight();
        it->second.Close(m_lastNowMs);
        NoteFightCount(wasFighting, it->second.InFight());
        if (it->second.HasTakeablePayloads() || it->second.HasEvents())
        {
            // The open fight was just closed and still has to go out, so the book
            // moves aside and drains like a live one instead of being erased with it.
            RetiredMeta meta;
            if (auto const v = m_commander.find(groupId); v != m_commander.end())
                meta.commander = v->second;
            if (auto const v = m_commanderGuid.find(groupId); v != m_commanderGuid.end())
                meta.commanderGuid = v->second;
            if (auto const v = m_mapId.find(groupId); v != m_mapId.end())
                meta.mapId = v->second;
            m_retiredMeta[groupId] = std::move(meta);
            m_retired[groupId] = std::move(it->second);
            retired = true;
        }
        m_books.erase(it);
    }
    for (auto it = m_memberGroup.begin(); it != m_memberGroup.end();)
        it = it->second == groupId ? m_memberGroup.erase(it) : std::next(it);
    m_ctx.erase(groupId);
    PublishMembers();
    if (!retired)
    {
        m_commander.erase(groupId);
        m_commanderGuid.erase(groupId);
        m_mapId.erase(groupId);
        m_lastEventSend.erase(groupId);
    }
}

void CombatMonitorService::OnCast(uint32_t casterGuidLow, char const* spell, bool heal, char castKind,
                                  uint64_t target, char const* targetName)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    if (!MaybeMember(casterGuidLow))
        return;   // no member guid: no lock, no refcount, no allocation
    NoteSnapshotLoad();
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
    book->second.OnCast(casterGuidLow, spell ? spell : "", heal, m_lastNowMs, castKind, target,
                        targetName ? targetName : "");
}

void CombatMonitorService::NoteEnemyCast(uint32_t groupId, uint32_t entry, uint64_t guid, char const* name,
                                          char const* spell, char rank)
{
    if (!Enabled() || groupId == 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto const book = m_books.find(groupId);
    if (book == m_books.end())
        return;
    book->second.NoteEnemyCast(entry, guid, name ? name : "", spell ? spell : "", rank, m_lastNowMs);
}

void CombatMonitorService::OnDamage(uint32_t attackerOwnerGuidLow, uint32_t victimOwnerGuidLow, uint32_t amount,
                                     char const* attackerName, uint32_t enemyEntry, uint64_t enemyGuid,
                                     char const* enemyName)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    if (!MaybeMember(attackerOwnerGuidLow) && !MaybeMember(victimOwnerGuidLow))
        return;   // no member guid: no lock, no refcount, no allocation
    NoteSnapshotLoad();
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
    if (!MaybeMember(healerOwnerGuidLow))
        return;   // no member guid: no lock, no refcount, no allocation
    NoteSnapshotLoad();
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
    if (!MaybeMember(memberGuidLow))
        return;   // no member guid: no lock, no refcount, no allocation
    NoteSnapshotLoad();
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
        NoteFightCount(false, book->second.InFight());
    }
}

void CombatMonitorService::OnDeath(uint32_t victimGuidLow, uint32_t killerOwnerGuidLow, char const* killer, float x,
                                   float y, float z, uint32_t enemyEntryDied)
{
    if (m_memberCount.load(std::memory_order_relaxed) == 0)
        return;
    if (victimGuidLow ? !MaybeMember(victimGuidLow)
                     : (enemyEntryDied == 0 || !MaybeMember(killerOwnerGuidLow)))
        return;   // no member guid: no lock, no refcount, no allocation
    NoteSnapshotLoad();
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
        bool const wasFighting = book->second.InFight();
        book->second.OnDeath(victimGuidLow, killer ? killer : "", x, y, z, m_lastNowMs);
        NoteFightCount(wasFighting, book->second.InFight());
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
    std::vector<std::string> oversize;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto drain = [&](uint32_t groupId, CombatMonitor::Book& book, std::string const& commander, uint32_t commanderGuid,
                     uint32_t mapId) {
        if (!book.HasTakeablePayloads())
            return;
        std::vector<std::string> payloads = book.TakeFightPayloads(protocolVersion, session, nextSeq, ts, commander,
                                                                   commanderGuid, groupId, mapId, &oversize);
        for (std::string& payload : payloads)
            out.push_back(std::move(payload));
    };
    auto commanderOf = [this](uint32_t groupId) -> std::string const& {
        static std::string const empty;
        auto const it = m_commander.find(groupId);
        return it != m_commander.end() ? it->second : empty;
    };
    auto guidOf = [this](uint32_t groupId) -> uint32_t {
        auto const it = m_commanderGuid.find(groupId);
        return it != m_commanderGuid.end() ? it->second : 0;
    };
    auto mapOf = [this](uint32_t groupId) -> uint32_t {
        auto const it = m_mapId.find(groupId);
        return it != m_mapId.end() ? it->second : 0;
    };
    for (auto& kv : m_books)
        drain(kv.first, kv.second, commanderOf(kv.first), guidOf(kv.first), mapOf(kv.first));
    for (auto it = m_retired.begin(); it != m_retired.end();)
    {
        RetiredMeta const& meta = m_retiredMeta[it->first];
        drain(it->first, it->second, meta.commander, meta.commanderGuid, meta.mapId);
        if (!it->second.HasTakeablePayloads() && !it->second.HasEvents())
        {
            if (!m_books.count(it->first))
            {
                m_commander.erase(it->first);
                m_commanderGuid.erase(it->first);
                m_mapId.erase(it->first);
                m_ctx.erase(it->first);
                m_lastEventSend.erase(it->first);
            }
            m_retiredMeta.erase(it->first);
            it = m_retired.erase(it);
        }
        else
            ++it;
    }
    for (std::string const& id : oversize)
    {
#ifdef COMBAT_MONITOR_TEST_CONFIG
        (void)id;
        g_oversizeDrops.fetch_add(1, std::memory_order_relaxed);
#else
        sLog.outError("TortoiseBots: combat monitor dropped fight %s: no payload form fits %zu bytes", id.c_str(),
                      CombatMonitor::kPayloadByteBudget);
#endif
    }
}

void CombatMonitorService::TakeEventPayloads(std::vector<std::string>& out, int protocolVersion, uint64_t session,
                                             std::function<uint64_t()> const& nextSeq, int64_t tsUnix, int64_t tsMs,
                                             uint32_t nowMs, uint32_t minGapMs)
{
    out.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto drain = [&](uint32_t groupId, CombatMonitor::Book& book, std::string const& commander, uint32_t commanderGuid,
                     uint32_t mapId) {
        bool const inFight = book.InFight();
        auto const sent = m_lastEventSend.find(groupId);
        uint32_t const last = sent != m_lastEventSend.end() ? sent->second : 0;
        bool const keepalive = inFight && nowMs - last >= CombatMonitor::kEventKeepaliveMs;
        if (!book.HasEvents() && !keepalive)
            return;
        if (nowMs - last < minGapMs)
            return;
        std::vector<CombatMonitor::LiveEvent> events;
        uint32_t dropped = 0;
        book.TakeEvents(events, dropped);
        CombatMonitor::EventsFightHeader header;
        header.inFight = inFight;
        header.number = book.FightNumber();
        header.startMs = book.StartMs();
        header.startUnix = book.StartUnix();
        header.fightMs = inFight && nowMs >= book.StartMs() ? nowMs - book.StartMs() : book.LastFightMs();
        header.ctx = book.Context();
        size_t emitted = 0;
        out.push_back(CombatMonitor::BuildEventsJson(protocolVersion, session, nextSeq(), tsUnix, tsMs, commander,
                                                     commanderGuid, groupId, mapId, header, events, dropped, &emitted));
        // The budget may have shrunk the datagram below what was taken: the rest
        // goes back in order for the next one instead of being lost.
        if (emitted < events.size())
            book.RequeueEvents(events, emitted);
        m_lastEventSend[groupId] = nowMs;
    };
    for (auto& kv : m_books)
    {
        auto const c = m_commander.find(kv.first);
        auto const g = m_commanderGuid.find(kv.first);
        auto const m = m_mapId.find(kv.first);
        drain(kv.first, kv.second, c != m_commander.end() ? c->second : std::string(),
              g != m_commanderGuid.end() ? g->second : 0, m != m_mapId.end() ? m->second : 0);
    }
    for (auto& kv : m_retired)
    {
        auto const meta = m_retiredMeta.find(kv.first);
        if (meta == m_retiredMeta.end())
            continue;
        drain(kv.first, kv.second, meta->second.commander, meta->second.commanderGuid, meta->second.mapId);
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
