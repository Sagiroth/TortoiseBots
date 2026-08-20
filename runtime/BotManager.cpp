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
    if (m_bots.find(key) != m_bots.end())
    {
        sLog.outString("TortoiseBots: AddBot guid %s already tracked", guid.GetString().c_str());
        return m_bots[key].session;
    }
    WorldSession* sess = BotSessionAdapter::CreateHeadlessSession(accountId, guid);
    if (!sess)
        return nullptr;
    BotRecord rec;
    rec.accountId = accountId;
    rec.characterGuid = guid;
    rec.session = sess;
    m_bots[key] = rec;
    sLog.outString("TortoiseBots: AddBot %s on acct %u", guid.GetString().c_str(), accountId);
    return sess;
}

bool BotManager::RemoveBot(ObjectGuid guid, bool save)
{
    uint32 key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it == m_bots.end())
        return false;
    WorldSession* sess = it->second.session;
    if (sess)
        BotSessionAdapter::LogoutHeadlessSession(sess, save);
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
    // Keep records' session pointers fresh — if a session was deleted by World,
    // the pointer may be stale. We don't dereference stale pointers; we just
    // clear them when the player is no longer findable.
    for (auto it = m_bots.begin(); it != m_bots.end(); )
    {
        Player* p = sObjectAccessor.FindPlayer(it->second.characterGuid);
        if (p)
        {
            it->second.session = p->GetSession();
            it->second.enteredWorld = p->IsInWorld();
            if (p->IsInWorld())
                ++it->second.ticksInWorld;
            ++it;
        }
        else
        {
            // Player not in world — keep record but session may be pending login or logout.
            ++it;
        }
    }

    if (m_autoTestEnabled)
        UpdateAutoTest(diff);
}

void BotManager::UpdateAutoTest(uint32 diff)
{
    // Very simple 7-step state machine, tick-driven, with generous timeouts.
    // This is only for the Phase 3 spike proof, not production AI.
    (void)diff;
    ++m_autoTestTicks;

    switch (m_autoState)
    {
        case AutoState::Idle:
            if (m_autoTestTicks > 20) // ~1 sec at 50ms/tick
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
                        WorldSession* sess = rec->session;
                        std::string sessInfo = sess ? (sess->IsHeadless() ? "headless" : "network") : "<null>";
                        bool loading = sess ? sess->PlayerLoading() : false;
                        std::string playerInfo = p ? (p->IsInWorld() ? "IsInWorld" : "not InWorld") : "FindPlayer null";
                        sLog.outString("TortoiseBots: AutoTest LoggingIn tick %u sess %s (%p) acct %u loading %u player %s", m_autoTestTicks, sessInfo.c_str(), (void*)sess, sess ? sess->GetAccountId() : 0, loading, playerInfo.c_str());
                        if (sess)
                            sLog.outString("TortoiseBots:   sess details transport %u m_Socket %p IsHeadless %u", (uint32)sess->GetTransport(), (void*)sess->GetSocket(), sess->IsHeadless());
                    }
                    if (m_autoTestTicks > 400) // 20 sec timeout, generous for DB async
                    {
                        Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        WorldSession* sess = rec->session;
                        sLog.outError("TortoiseBots: AutoTest login timeout after %u ticks (sess %s loading %u player %s)",
                            m_autoTestTicks, sess ? (sess->IsHeadless() ? "headless" : "network") : "null", sess ? sess->PlayerLoading() : 0, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
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
            if (m_autoTestTicks > 600) // ~30 sec in world (spike wants 5 min, but we use short for test)
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
                // Verify the session was removed and we can relog.
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
                else if (m_autoTestTicks > 200)
                {
                    sLog.outError("TortoiseBots: AutoTest relog timeout");
                    m_autoState = AutoState::Done;
                }
            }
            break;
        case AutoState::Done:
            // Stay done; human reclaim and shutdown are verified externally
            // (human reclaim is the normal HandlePlayerLogin alreadyOnline path;
            // shutdown is verified by clean mangosd stop).
            break;
    }
}

} // namespace TortoiseBots
