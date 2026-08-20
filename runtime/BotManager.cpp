#include "BotManager.h"
#include "../host/BotSessionAdapter.h"
#include "WorldSession.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Log.h"
#include "World.h"

namespace TortoiseBots {

BotManager& BotManager::Instance()
{
    static BotManager instance;
    return instance;
}

WorldSession* BotManager::AddBot(uint32 accountId, ObjectGuid guid)
{
    uint32 key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it != m_bots.end())
    {
        sLog.outString("TortoiseBots: AddBot guid %s already tracked (pendingLogin %u enteredWorld %u)", guid.GetString().c_str(), it->second.pendingLogin, it->second.enteredWorld);
        // Return the current session if any (lookup via FindSession, not stale pointer)
        if (WorldSession* sess = sWorld.FindSession(accountId))
            return sess;
        return nullptr;
    }
    // CreateHeadlessSession now only creates the session and queues AddSession;
    // LoginPlayer is deferred until FindSession succeeds (see OnWorldUpdate).
    WorldSession* sess = BotSessionAdapter::CreateHeadlessSession(accountId, guid);
    if (!sess)
        return nullptr;
    BotRecord rec;
    rec.accountId = accountId;
    rec.characterGuid = guid;
    rec.pendingLogin = true;
    rec.enteredWorld = false;
    rec.ticksInWorld = 0;
    m_bots[key] = rec;
    sLog.outString("TortoiseBots: AddBot %s on acct %u (pendingLogin, queued AddSession)", guid.GetString().c_str(), accountId);
    return sess;
}

bool BotManager::RemoveBot(ObjectGuid guid, bool save)
{
    uint32 key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it == m_bots.end())
        return false;
    uint32 accountId = it->second.accountId;
    // Lookup the live session via World, not a stale pointer.
    WorldSession* sess = sWorld.FindSession(accountId);
    // Fallback: try via player if session lookup fails (e.g. session already removed from m_sessions but player still in world)
    if (!sess)
    {
        if (Player* p = sObjectAccessor.FindPlayer(guid))
            sess = p->GetSession();
    }
    if (sess && sess->IsHeadless())
        BotSessionAdapter::LogoutHeadlessSession(sess, save);
    else if (sess)
        sLog.outError("TortoiseBots: RemoveBot %s found non-headless session acct %u — not logging out via BotSessionAdapter", guid.GetString().c_str(), sess->GetAccountId());
    m_bots.erase(it);
    sLog.outString("TortoiseBots: RemoveBot %s", guid.GetString().c_str());
    return true;
}

BotRecord* BotManager::FindBot(ObjectGuid guid)
{
    auto it = m_bots.find(guid.GetCounter());
    if (it == m_bots.end())
        return nullptr;
    return &it->second;
}

bool BotManager::IsBot(ObjectGuid guid) const
{
    return m_bots.find(guid.GetCounter()) != m_bots.end();
}

void BotManager::SetAutoTestEnabled(bool enable, uint32 accountId, ObjectGuid guid)
{
    m_autoTestEnabled = enable;
    m_autoTestAccount = accountId;
    m_autoTestGuid = guid;
    m_autoTestTicks = 0;
    m_autoState = AutoState::Idle;
    sLog.outString("TortoiseBots: AutoTest %s acct %u guid %s",
        enable ? "enabled" : "disabled", accountId, guid.GetString().c_str());
}

void BotManager::OnWorldUpdate(uint32 diff)
{
    // Dispatch pending LoginPlayer for newly queued headless sessions.
    // AddSession is queued; FindSession only succeeds after World::UpdateSessions
    // has moved the session from addSessQueue to m_sessions (next tick).
    for (auto& kv : m_bots)
    {
        BotRecord& rec = kv.second;
        if (!rec.pendingLogin)
            continue;
        WorldSession* sess = sWorld.FindSession(rec.accountId);
        if (sess && sess->IsHeadless() && sess->GetAccountId() == rec.accountId)
        {
            sLog.outString("TortoiseBots: BotManager dispatching LoginPlayer for %s (acct %u, sess %p)", rec.characterGuid.GetString().c_str(), rec.accountId, (void*)sess);
            sess->LoginPlayer(rec.characterGuid);
            rec.pendingLogin = false;
        }
    }

    // Update enteredWorld/ticksInWorld and detect human reclaim.
    for (auto it = m_bots.begin(); it != m_bots.end(); )
    {
        BotRecord& rec = it->second;
        Player* p = sObjectAccessor.FindPlayer(rec.characterGuid);
        if (p)
        {
            // Human reclaim detection: if the player's session is no longer the
            // headless session we created (e.g. a network session took ownership
            // via HandlePlayerLogin's alreadyOnline transfer), stop controlling.
            WorldSession* playerSess = p->GetSession();
            bool isHeadless = playerSess && playerSess->IsHeadless();
            bool isOurAccount = playerSess && playerSess->GetAccountId() == rec.accountId;
            if (!isHeadless || !isOurAccount)
            {
                sLog.outString("TortoiseBots: Bot %s reclaimed by %s session acct %u (headless %u) — releasing", rec.characterGuid.GetString().c_str(), isHeadless ? "headless" : "network", playerSess ? playerSess->GetAccountId() : 0, isHeadless);
                it = m_bots.erase(it);
                continue;
            }
            if (p->IsInWorld())
            {
                if (!rec.enteredWorld)
                    sLog.outString("TortoiseBots: Bot %s entered world", rec.characterGuid.GetString().c_str());
                rec.enteredWorld = true;
                ++rec.ticksInWorld;
            }
        }
        else
        {
            // Player not in world — may be pending login, being teleported, or logged out.
            // Keep the record; pendingLogin flag covers the pre-login case.
            // If the bot was previously enteredWorld and now FindPlayer is null, it has left the world (logout/teleport).
            if (rec.enteredWorld)
            {
                // Check if the session is still in World (not yet deleted). If FindSession is null, the session was removed.
                WorldSession* sess = sWorld.FindSession(rec.accountId);
                if (!sess)
                {
                    // Session no longer in world — likely logged out and deleted. Keep enteredWorld true for a bit
                    // so UpdateAutoTest can see the transition, but don't immediately erase.
                    // The auto-test's LoggingOut/Relogging states handle erasure via RemoveBot.
                }
            }
        }
        ++it;
    }

    if (m_autoTestEnabled)
        UpdateAutoTest(diff);
}

void BotManager::UpdateAutoTest(uint32 diff)
{
    (void)diff;
    ++m_autoTestTicks;

    switch (m_autoState)
    {
        case AutoState::Idle:
            if (m_autoTestTicks > 20)
            {
                sLog.outString("TortoiseBots: AutoTest step 1 — login bot %s", m_autoTestGuid.GetString().c_str());
                AddBot(m_autoTestAccount, m_autoTestGuid);
                m_autoState = AutoState::LoggingIn;
                m_autoTestTicks = 0;
            }
            break;
        case AutoState::LoggingIn:
            if (BotRecord* rec = FindBot(m_autoTestGuid))
            {
                if (rec->enteredWorld)
                {
                    sLog.outString("TortoiseBots: AutoTest step 2 — bot entered world, tick %u", rec->ticksInWorld);
                    m_autoState = AutoState::InWorld;
                    m_autoTestTicks = 0;
                }
                else
                {
                    if (m_autoTestTicks % 40 == 0)
                    {
                        Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        WorldSession* sess = sWorld.FindSession(rec->accountId);
                        std::string sessInfo = sess ? (sess->IsHeadless() ? "headless" : "network") : "<null>";
                        bool loading = sess ? sess->PlayerLoading() : false;
                        std::string playerInfo = p ? (p->IsInWorld() ? "IsInWorld" : "not InWorld") : "FindPlayer null";
                        sLog.outString("TortoiseBots: AutoTest LoggingIn tick %u sess %s (%p) acct %u loading %u player %s pending %u", m_autoTestTicks, sessInfo.c_str(), (void*)sess, rec->accountId, loading, playerInfo.c_str(), rec->pendingLogin);
                        if (sess)
                            sLog.outString("TortoiseBots:   sess details transport %u m_Socket %p IsHeadless %u", (uint32)sess->GetTransport(), (void*)sess->GetSocket(), sess->IsHeadless());
                    }
                    if (m_autoTestTicks > 400)
                    {
                        Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        WorldSession* sess = sWorld.FindSession(rec->accountId);
                        sLog.outError("TortoiseBots: AutoTest login timeout after %u ticks (sess %s loading %u player %s)", m_autoTestTicks, sess ? (sess->IsHeadless() ? "headless" : "network") : "null", sess ? sess->PlayerLoading() : 0, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                        m_autoState = AutoState::Done;
                    }
                }
            }
            else
            {
                sLog.outError("TortoiseBots: AutoTest LoggingIn but FindBot null tick %u", m_autoTestTicks);
                if (m_autoTestTicks > 400)
                    m_autoState = AutoState::Done;
            }
            break;
        case AutoState::InWorld:
            if (m_autoTestTicks > 600)
            {
                if (BotRecord* rec = FindBot(m_autoTestGuid))
                {
                    Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    if (p)
                    {
                        p->SaveToDB(false, false);
                        sLog.outString("TortoiseBots: AutoTest step 3 — saved bot %s", p->GetName());
                    }
                }
                m_autoState = AutoState::Saving;
                m_autoTestTicks = 0;
            }
            break;
        case AutoState::Saving:
            if (m_autoTestTicks > 20)
            {
                sLog.outString("TortoiseBots: AutoTest step 4 — logout bot");
                RemoveBot(m_autoTestGuid, true);
                m_autoState = AutoState::LoggingOut;
                m_autoTestTicks = 0;
            }
            break;
        case AutoState::LoggingOut:
            if (m_autoTestTicks > 40)
            {
                if (!sObjectAccessor.FindPlayer(m_autoTestGuid))
                {
                    sLog.outString("TortoiseBots: AutoTest step 5 — re-login bot");
                    AddBot(m_autoTestAccount, m_autoTestGuid);
                    m_autoState = AutoState::Relogging;
                    m_autoTestTicks = 0;
                }
            }
            break;
        case AutoState::Relogging:
            if (BotRecord* rec = FindBot(m_autoTestGuid))
            {
                if (rec->enteredWorld)
                {
                    sLog.outString("TortoiseBots: AutoTest step 6 — bot re-entered world, spike PASSED");
                    m_autoState = AutoState::Done;
                    m_autoTestTicks = 0;
                }
                else if (m_autoTestTicks > 400)
                {
                    sLog.outError("TortoiseBots: AutoTest relog timeout");
                    m_autoState = AutoState::Done;
                }
                else if (m_autoTestTicks % 40 == 0)
                {
                    Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    WorldSession* sess = sWorld.FindSession(rec->accountId);
                    sLog.outString("TortoiseBots: AutoTest Relogging tick %u sess %s pending %u player %s", m_autoTestTicks, sess ? (sess->IsHeadless() ? "headless" : "network") : "null", rec->pendingLogin, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                }
            }
            break;
        case AutoState::Done:
            // Stay done; human reclaim and shutdown are verified externally.
            // For the cleanup pass we also verify that a human reclaim would be
            // detected via the OnWorldUpdate check above (FindPlayer returns a
            // network session).
            break;
    }
}

} // namespace TortoiseBots
