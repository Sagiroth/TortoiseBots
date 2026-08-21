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

bool BotManager::RunPendingAddRemoveTest(uint32 accountId, ObjectGuid guid)
{
    if (sWorld.FindSession(accountId) || sWorld.HasPendingSession(accountId, SessionTransport::Headless) ||
        sObjectAccessor.FindPlayer(guid) || FindBot(guid))
    {
        sLog.outError("TortoiseBots: PendingAddRemoveTest precondition failed for acct %u guid %s",
            accountId, guid.GetString().c_str());
        return false;
    }

    WorldSession* session = AddBot(accountId, guid);
    bool queued = session && sWorld.HasPendingSession(accountId, SessionTransport::Headless);
    bool removed = RemoveBot(guid, false);
    bool noActiveSession = !sWorld.FindSession(accountId);
    bool noPlayer = !sObjectAccessor.FindPlayer(guid);
    bool noRecord = !FindBot(guid);
    bool noPendingSession = !sWorld.HasPendingSession(accountId, SessionTransport::Headless);

    if (!noPendingSession)
        sWorld.CancelPendingSession(accountId, SessionTransport::Headless);

    bool passed = session && queued && removed && noActiveSession && noPlayer && noRecord && noPendingSession;
    sLog.outString("TortoiseBots: PendingAddRemoveTest %s acct %u queued %u active %u player %u record %u pending %u",
        passed ? "PASSED" : "FAILED", accountId, queued, !noActiveSession, !noPlayer, !noRecord, !noPendingSession);
    return passed;
}

WorldSession* BotManager::AddBot(uint32 accountId, ObjectGuid guid)
{
    uint32 key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it != m_bots.end())
    {
        sLog.outString("TortoiseBots: AddBot guid %s already tracked (state %u enteredWorld %u)",
            guid.GetString().c_str(), static_cast<uint32>(it->second.lifecycle), it->second.enteredWorld);
        // Return the current session if any (lookup via FindSession, not stale pointer)
        if (WorldSession* sess = sWorld.FindSession(accountId))
            return sess;
        return nullptr;
    }

    // CreateHeadlessSession queues AddSession. LoginPlayer is dispatched only after
    // the queued session is visible through FindSession (see OnWorldUpdate).
    WorldSession* sess = BotSessionAdapter::CreateHeadlessSession(accountId, guid);
    if (!sess)
        return nullptr;

    BotRecord rec;
    rec.accountId = accountId;
    rec.characterGuid = guid;
    rec.lifecycle = BotLifecycle::PendingAdd;
    m_bots[key] = rec;
    sLog.outString("TortoiseBots: AddBot %s on acct %u (PendingAdd, queued AddSession)",
        guid.GetString().c_str(), accountId);
    return sess;
}

bool BotManager::RemoveBot(ObjectGuid guid, bool save)
{
    uint32 key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it == m_bots.end())
        return false;

    BotRecord& rec = it->second;
    uint32 accountId = rec.accountId;

    // PendingAdd owns a queue entry, not an m_sessions entry. Cancel it before
    // erasing the record so an immediate AddBot -> RemoveBot cannot orphan it.
    if (rec.lifecycle == BotLifecycle::PendingAdd &&
        sWorld.CancelPendingSession(accountId, SessionTransport::Headless))
    {
        m_bots.erase(it);
        sLog.outString("TortoiseBots: RemoveBot %s cancelled PendingAdd", guid.GetString().c_str());
        return true;
    }

    rec.lifecycle = BotLifecycle::Removing;

    // Lookup the live session via World, not a stale pointer. A pending login may
    // still have no Player; OnWorldUpdate keeps the Removing record until loading
    // finishes and the resulting Player is logged out.
    WorldSession* sess = sWorld.FindSession(accountId);
    if (!sess)
    {
        if (Player* p = sObjectAccessor.FindPlayer(guid))
            sess = p->GetSession();
    }
    if (sess && sess->IsHeadless())
        BotSessionAdapter::LogoutHeadlessSession(sess, save);
    else if (sess && !sess->IsHeadless())
        sLog.outError("TortoiseBots: RemoveBot %s found non-headless session acct %u — releasing on reclaim",
            guid.GetString().c_str(), sess->GetAccountId());

    // A concurrent queue drain may have moved the session after the first check.
    // Retry cancellation only while it is still pending; otherwise the next world
    // tick owns the Removing -> erased transition.
    if (sWorld.CancelPendingSession(accountId, SessionTransport::Headless))
    {
        m_bots.erase(it);
        sLog.outString("TortoiseBots: RemoveBot %s cancelled pending session", guid.GetString().c_str());
        return true;
    }

    sLog.outString("TortoiseBots: RemoveBot %s (Removing; cleanup deferred)", guid.GetString().c_str());
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
    // AddSession is queued. Only dispatch LoginPlayer after the queue has moved
    // the session into m_sessions, making the lifecycle explicit:
    // PendingAdd -> PendingLogin -> InWorld, or any state -> Removing.
    for (auto it = m_bots.begin(); it != m_bots.end(); )
    {
        BotRecord& rec = it->second;
        if (rec.lifecycle == BotLifecycle::PendingAdd)
        {
            WorldSession* sess = sWorld.FindSession(rec.accountId);
            if (sess && sess->IsHeadless() && sess->GetAccountId() == rec.accountId)
            {
                rec.lifecycle = BotLifecycle::PendingLogin;
                sLog.outString("TortoiseBots: BotManager dispatching LoginPlayer for %s (acct %u, sess %p)",
                    rec.characterGuid.GetString().c_str(), rec.accountId, (void*)sess);
                sess->LoginPlayer(rec.characterGuid);
            }
            else if (!sWorld.HasPendingSession(rec.accountId, SessionTransport::Headless) && !sess)
            {
                sLog.outError("TortoiseBots: Bot %s lost before LoginPlayer; dropping record",
                    rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
                continue;
            }
        }
        ++it;
    }

    for (auto it = m_bots.begin(); it != m_bots.end(); )
    {
        BotRecord& rec = it->second;
        Player* p = sObjectAccessor.FindPlayer(rec.characterGuid);

        if (rec.lifecycle == BotLifecycle::Removing)
        {
            if (p)
            {
                WorldSession* playerSess = p->GetSession();
                if (playerSess && playerSess->IsHeadless() && playerSess->GetAccountId() == rec.accountId)
                    BotSessionAdapter::LogoutHeadlessSession(playerSess, true);
                else
                {
                    sLog.outString("TortoiseBots: Bot %s reclaimed by network during removal — releasing",
                        rec.characterGuid.GetString().c_str());
                    it = m_bots.erase(it);
                    continue;
                }
            }
            else if (sWorld.CancelPendingSession(rec.accountId, SessionTransport::Headless))
            {
                sLog.outString("TortoiseBots: Bot %s removed before session activation",
                    rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
                continue;
            }
            else if (!sWorld.FindSession(rec.accountId))
            {
                sLog.outString("TortoiseBots: Bot %s removal complete",
                    rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
                continue;
            }
            ++it;
            continue;
        }

        if (p)
        {
            // Human reclaim detection: if the player's session is no longer the
            // headless session we created, stop controlling it.
            WorldSession* playerSess = p->GetSession();
            bool isHeadless = playerSess && playerSess->IsHeadless();
            bool isOurAccount = playerSess && playerSess->GetAccountId() == rec.accountId;
            if (!isHeadless || !isOurAccount)
            {
                sLog.outString("TortoiseBots: Bot %s reclaimed by %s session acct %u (headless %u) — releasing",
                    rec.characterGuid.GetString().c_str(), isHeadless ? "headless" : "network",
                    playerSess ? playerSess->GetAccountId() : 0, isHeadless);
                it = m_bots.erase(it);
                continue;
            }
            if (p->IsInWorld())
            {
                if (!rec.enteredWorld)
                    sLog.outString("TortoiseBots: Bot %s entered world", rec.characterGuid.GetString().c_str());
                rec.enteredWorld = true;
                rec.lifecycle = BotLifecycle::InWorld;
                ++rec.ticksInWorld;
            }
        }
        else if (rec.lifecycle == BotLifecycle::PendingLogin)
        {
            // Keep the record while the async LoginQueryHolder is in flight.
            // A removal request is handled on a later tick after the callback
            // either produces a Player or the headless session disappears.
            if (!sWorld.FindSession(rec.accountId) &&
                !sWorld.HasPendingSession(rec.accountId, SessionTransport::Headless))
            {
                sLog.outError("TortoiseBots: Bot %s login ended without a session",
                    rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
                continue;
            }
        }
        else if (!sWorld.FindSession(rec.accountId) &&
                 !sWorld.HasPendingSession(rec.accountId, SessionTransport::Headless))
        {
            sLog.outString("TortoiseBots: Bot %s session ended — releasing",
                rec.characterGuid.GetString().c_str());
            it = m_bots.erase(it);
            continue;
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
                        sLog.outString("TortoiseBots: AutoTest LoggingIn tick %u sess %s (%p) acct %u loading %u player %s pending %u", m_autoTestTicks, sessInfo.c_str(), (void*)sess, rec->accountId, loading, playerInfo.c_str(), rec->lifecycle == BotLifecycle::PendingLogin);
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
                if (!sObjectAccessor.FindPlayer(m_autoTestGuid) &&
                    !FindBot(m_autoTestGuid) &&
                    !sWorld.FindSession(m_autoTestAccount) &&
                    !sWorld.HasPendingSession(m_autoTestAccount, SessionTransport::Headless))
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
                    sLog.outString("TortoiseBots: AutoTest Relogging tick %u sess %s pending %u player %s", m_autoTestTicks, sess ? (sess->IsHeadless() ? "headless" : "network") : "null", rec->lifecycle == BotLifecycle::PendingLogin, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
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
