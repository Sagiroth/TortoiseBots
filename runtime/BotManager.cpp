// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access,clang:uninitialized,clang:undefined_identifier,clang:undeclared_identifier,clang:all
#include "BotManager.h"
#include "PlayerbotAIAdapter.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/RandomBotFacade.h"
#include "../host/BotSessionAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
#include "AccountMgr.h"
#include "WorldPacket.h"
#include "Group/Group.h"

namespace TortoiseBots {

BotEntry::~BotEntry() = default;

BotManager& BotManager::Instance()
{
    static BotManager instance;
    return instance;
}

void BotManager::OnPlayerLogin(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    ::WorldSession* session = player->GetSession();
    if (!session || !session->IsHeadless())
    {
        if (it != m_bots.end())
            ReleaseToClient(player);
        RebindOwnedBots(player);
        return;
    }

    if (it == m_bots.end())
        return;

    BotEntry& entry = it->second;
    BotRecord& record = entry.record;
    if (record.enteredWorld)
        return;

    record.enteredWorld = true;
    record.lifecycle = BotLifecycle::InWorld;

    if (!entry.aiAdapter)
    {
        ::Player* masterPlayer = nullptr;
        if (record.masterGuid && !record.masterGuid.IsEmpty())
            masterPlayer = sObjectAccessor.FindPlayer(record.masterGuid);

        entry.aiAdapter = std::make_unique<PlayerbotAIAdapter>(player, masterPlayer);
        if (!entry.aiAdapter->Initialize())
            sLog.outError("TortoiseBots: PlayerbotAI attach failed for %s", player->GetName());
    }

    sLog.outString("TortoiseBots: bot %s entered world through native PlayerScript", player->GetName());
}

void BotManager::OnPlayerBeforeLogout(::Player* player)
{
    if (!player)
        return;

    ::WorldSession* session = player->GetSession();
    if (!session || !session->IsHeadless())
        DetachOwnedBots(player);

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end())
        return;

    if (it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();

    if (it->second.record.lifecycle != BotLifecycle::Removing)
        it->second.record.lifecycle = BotLifecycle::Removing;
}

void BotManager::OnPlayerLogout(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it != m_bots.end() && it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();
}

void BotManager::DetachOwnedBots(::Player* master)
{
    if (!master)
        return;

    ObjectGuid masterGuid = master->GetObjectGuid();
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.masterGuid != masterGuid)
            continue;

        ::Player* bot = sObjectAccessor.FindPlayer(entry.record.characterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
            continue;

        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
            entry.aiAdapter->DetachMaster();
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
            sLog.outError("TortoiseBots: cannot detach master for %s because the module AI adapter is unavailable",
                bot->GetName());

        sLog.outDebug("TortoiseBots: detached live master pointer %s from bot %s; ownership GUID retained",
            master->GetName(), bot->GetName());
    }
}

void BotManager::RebindOwnedBots(::Player* master)
{
    if (!master || !master->GetSession() || master->GetSession()->IsHeadless())
        return;

    ObjectGuid masterGuid = master->GetObjectGuid();
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.masterGuid != masterGuid ||
            entry.record.lifecycle != BotLifecycle::InWorld)
            continue;

        ::Player* bot = sObjectAccessor.FindPlayer(entry.record.characterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
            continue;

        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
            entry.aiAdapter->RebindMaster(master);
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
            sLog.outError("TortoiseBots: cannot rebind master for %s because the module AI adapter is unavailable",
                bot->GetName());

        sLog.outString("TortoiseBots: rebound master %s to existing Headless bot %s; mature movement preserved",
            master->GetName(), bot->GetName());
    }
}

void BotManager::ReleaseToClient(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end())
        return;

    if (it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();

    sLog.outString("TortoiseBots: releasing module control of %s to a network client", player->GetName());
    m_bots.erase(it);
}

bool BotManager::RunPendingAddRemoveTest(uint32_t accountId, ::ObjectGuid guid)
{
    if (sWorld.FindHeadlessSession(guid) || sWorld.HasPendingHeadlessSession(guid) ||
        sObjectAccessor.FindPlayer(guid) || FindBot(guid))
    {
        sLog.outError("TortoiseBots: PendingAddRemoveTest precondition failed for acct %u guid %s",
            accountId, guid.GetString().c_str());
        return false;
    }

    ::WorldSession* session = AddBot(accountId, guid);
    bool queued = session && sWorld.HasPendingHeadlessSession(guid);
    bool removed = RemoveBot(guid, false);
    bool noActiveSession = !sWorld.FindHeadlessSession(guid);
    bool noPlayer = !sObjectAccessor.FindPlayer(guid);
    bool noRecord = !FindBot(guid);
    bool noPendingSession = !sWorld.HasPendingHeadlessSession(guid);

    if (!noPendingSession)
        sWorld.CancelPendingHeadlessSession(guid);

    bool passed = session && queued && removed && noActiveSession && noPlayer && noRecord && noPendingSession;
    sLog.outString("TortoiseBots: PendingAddRemoveTest %s acct %u queued %u active %u player %u record %u pending %u",
        passed ? "PASSED" : "FAILED", accountId, queued, !noActiveSession, !noPlayer, !noRecord, !noPendingSession);
    return passed;
}

::WorldSession* BotManager::AddBot(uint32_t accountId, ::ObjectGuid guid, ::ObjectGuid masterGuid)
{
    return AddBotWithMaster(accountId, guid, masterGuid);
}

::WorldSession* BotManager::AddRandomBot(uint32_t accountId, ::ObjectGuid guid)
{
    ::WorldSession* session = AddBotWithMaster(accountId, guid, ::ObjectGuid());
    if (BotRecord* record = FindBot(guid))
        record->random = true;
    return session;
}

::WorldSession* BotManager::AddBotWithMaster(uint32_t accountId, ::ObjectGuid guid, ::ObjectGuid masterGuid)
{
    uint32_t key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it != m_bots.end())
    {
        sLog.outString("TortoiseBots: AddBot guid %s already tracked (state %u enteredWorld %u)",
            guid.GetString().c_str(), static_cast<uint32_t>(it->second.record.lifecycle), it->second.record.enteredWorld);
        if (::WorldSession* sess = sWorld.FindHeadlessSession(guid))
            return sess;
        return nullptr;
    }

    if (sObjectAccessor.FindPlayer(guid) ||
        sWorld.FindHeadlessSession(guid) ||
        sWorld.HasPendingHeadlessSession(guid))
    {
        sLog.outError("TortoiseBots: AddBot guid %s rejected because the character already has a live or pending session",
            guid.GetString().c_str());
        return nullptr;
    }

    ::WorldSession* sess = BotSessionAdapter::CreateHeadlessSession(accountId, guid);
    if (!sess)
        return nullptr;

    BotEntry entry;
    entry.record.accountId = accountId;
    entry.record.characterGuid = guid;
    entry.record.masterGuid = masterGuid;
    entry.record.lifecycle = BotLifecycle::PendingAdd;
    m_bots.emplace(key, std::move(entry));
    sLog.outString("TortoiseBots: AddBot %s on acct %u master %s (PendingAdd, queued AddSession)",
        guid.GetString().c_str(), accountId, masterGuid.GetString().c_str());
    return sess;
}

bool BotManager::RemoveBot(::ObjectGuid guid, bool save)
{
    uint32_t key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it == m_bots.end())
        return false;

    BotRecord& rec = it->second.record;
    if (rec.lifecycle == BotLifecycle::PendingAdd && sWorld.CancelPendingHeadlessSession(guid))
    {
        m_bots.erase(it);
        sLog.outString("TortoiseBots: RemoveBot %s cancelled PendingAdd", guid.GetString().c_str());
        return true;
    }

    rec.lifecycle = BotLifecycle::Removing;

    ::WorldSession* sess = sWorld.FindHeadlessSession(guid);
    if (!sess)
    {
        if (::Player* p = sObjectAccessor.FindPlayer(guid))
            sess = p->GetSession();
    }
    if (sess && sess->IsHeadless())
        BotSessionAdapter::LogoutHeadlessSession(sess, guid, save);
    else if (sess)
        sLog.outError("TortoiseBots: RemoveBot %s found non-headless session acct %u — releasing on reclaim",
            guid.GetString().c_str(), sess->GetAccountId());

    if (sWorld.CancelPendingHeadlessSession(guid))
    {
        m_bots.erase(it);
        sLog.outString("TortoiseBots: RemoveBot %s cancelled pending session", guid.GetString().c_str());
        return true;
    }

    sLog.outString("TortoiseBots: RemoveBot %s (Removing; cleanup deferred)", guid.GetString().c_str());
    return true;
}

BotRecord* BotManager::FindBot(::ObjectGuid guid)
{
    auto it = m_bots.find(guid.GetCounter());
    if (it == m_bots.end())
        return nullptr;
    return &it->second.record;
}

bool BotManager::IsBot(::ObjectGuid guid) const
{
    return m_bots.find(guid.GetCounter()) != m_bots.end();
}

bool BotManager::IsRandomBot(::ObjectGuid guid) const
{
    auto it = m_bots.find(guid.GetCounter());
    return it != m_bots.end() && it->second.record.random;
}

std::vector<::Player*> BotManager::GetBotsForMaster(::ObjectGuid masterGuid) const
{
    std::vector<::Player*> result;
    for (auto const& entry : m_bots)
    {
        if (entry.second.record.masterGuid != masterGuid)
            continue;

        ::Player* player = sObjectAccessor.FindPlayer(entry.second.record.characterGuid);
        if (IsLiveHeadlessBot(entry.second, player))
            result.push_back(player);
    }
    return result;
}

std::vector<::Player*> BotManager::GetAllBots() const
{
    std::vector<::Player*> result;
    result.reserve(m_bots.size());
    for (auto const& entry : m_bots)
    {
        ::Player* player = sObjectAccessor.FindPlayer(entry.second.record.characterGuid);
        if (IsLiveHeadlessBot(entry.second, player))
            result.push_back(player);
    }
    return result;
}

bool BotManager::IsLiveHeadlessBot(BotEntry const& entry, ::Player* player) const
{
    if (!player || entry.record.lifecycle != BotLifecycle::InWorld || !player->IsInWorld())
        return false;

    ::WorldSession* session = player->GetSession();
    return session && session->IsHeadless() &&
        sWorld.FindHeadlessSession(entry.record.characterGuid) == session;
}

bool BotManager::BindBotMaster(::ObjectGuid botGuid, ::ObjectGuid masterGuid)
{
    if (masterGuid.IsEmpty() || botGuid == masterGuid)
        return false;

    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* master = sObjectAccessor.FindPlayer(masterGuid);
    auto masterEntry = m_bots.find(masterGuid.GetCounter());
    bool moduleHeadlessMaster = master && masterEntry != m_bots.end() &&
        IsLiveHeadlessBot(masterEntry->second, master);
    if (!master || !master->IsInWorld() || !master->GetSession() ||
        (master->GetSession()->IsHeadless() && !moduleHeadlessMaster))
    {
        sLog.outError("TortoiseBots: cannot bind bot %s to missing or unsupported master %s",
            botGuid.GetString().c_str(), masterGuid.GetString().c_str());
        return false;
    }

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = nullptr;
    if (bot)
    {
        if (!IsLiveHeadlessBot(it->second, bot))
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because its live session is not module-owned Headless",
                botGuid.GetString().c_str());
            return false;
        }

        ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because mature PlayerbotAI is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
        if (!it->second.aiAdapter || !it->second.aiAdapter->IsInitialized())
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because its module AI adapter is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
    }

    it->second.record.masterGuid = masterGuid;
    if (ai)
        it->second.aiAdapter->RebindMaster(master);

    return true;
}

bool BotManager::ClearBotMaster(::ObjectGuid botGuid)
{
    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = nullptr;
    if (bot)
    {
        if (!IsLiveHeadlessBot(it->second, bot))
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because its live session is not module-owned Headless",
                botGuid.GetString().c_str());
            return false;
        }

        ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because mature PlayerbotAI is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
        if (!it->second.aiAdapter || !it->second.aiAdapter->IsInitialized())
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because its module AI adapter is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
    }

    it->second.record.masterGuid = ObjectGuid();
    if (ai)
        it->second.aiAdapter->DetachMaster();

    return true;
}

bool BotManager::SetBotFollow(::ObjectGuid botGuid, ::ObjectGuid masterGuid)
{
    if (!BindBotMaster(botGuid, masterGuid))
        return false;

    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!bot || !IsLiveHeadlessBot(it->second, bot) || !ai)
    {
        sLog.outError("TortoiseBots: SetBotFollow bot %s rejected because Headless bot or mature AI is unavailable",
            botGuid.GetString().c_str());
        return false;
    }

    ::Player* master = sObjectAccessor.FindPlayer(masterGuid);
    ai::Event followEvent("follow", "", master);
    if (!ai->DoSpecificAction("follow chat shortcut", followEvent, true))
    {
        sLog.outError("TortoiseBots: mature follow action failed for bot %s",
            botGuid.GetString().c_str());
        return false;
    }

    sLog.outString("TortoiseBots: SetBotFollow bot %s -> master %s",
        botGuid.GetString().c_str(), masterGuid.GetString().c_str());
    return true;
}

void BotManager::SetAutoTestEnabled(bool enable, uint32_t accountId, ::ObjectGuid guid)
{
    m_autoTestEnabled = enable;
    m_autoTestAccount = accountId;
    m_autoTestGuid = guid;
    m_autoTestTicks = 0;
    m_autoState = AutoState::Idle;
    m_autoTestPassed = false;
    sLog.outString("TortoiseBots: AutoTest %s acct %u guid %s",
        enable ? "enabled" : "disabled", accountId, guid.GetString().c_str());
}

void BotManager::SetPacketBridgeTestEnabled(bool enable, uint32_t accountId,
    ::ObjectGuid masterGuid, ::ObjectGuid botGuid)
{
    m_packetTestEnabled = enable;
    m_packetTestAccount = accountId;
    m_packetTestMasterGuid = masterGuid;
    m_packetTestBotGuid = botGuid;
    m_packetTestTicks = 0;
    m_packetTestStage = 0;
    sLog.outString("TortoiseBots: PacketBridgeTest %s acct %u master %s bot %s",
        enable ? "enabled" : "disabled", accountId,
        masterGuid.GetString().c_str(), botGuid.GetString().c_str());
}

void BotManager::UpdateBots(uint32_t diff)
{
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.lifecycle != BotLifecycle::InWorld)
            continue;
        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
        {
            entry.aiAdapter->Update(diff);
        }
    }
}

void BotManager::OnWorldUpdate(uint32_t diff)
{
    // Headless registration is queued. Only dispatch LoginPlayer after World has
    // made the session visible by this bot's character guid.
    for (auto it = m_bots.begin(); it != m_bots.end(); )
    {
        BotRecord& rec = it->second.record;
        if (rec.lifecycle == BotLifecycle::PendingAdd)
        {
            ::WorldSession* sess = sWorld.FindHeadlessSession(rec.characterGuid);
            if (sess && !sess->PlayerLoading())
            {
                rec.lifecycle = BotLifecycle::PendingLogin;
                sLog.outString("TortoiseBots: BotManager dispatching LoginPlayer for %s (acct %u, sess %p)",
                    rec.characterGuid.GetString().c_str(), rec.accountId, (void*)sess);
                sess->LoginPlayer(rec.characterGuid);
            }
            else if (!sWorld.HasPendingHeadlessSession(rec.characterGuid))
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
        BotRecord& rec = it->second.record;
        ::Player* p = sObjectAccessor.FindPlayer(rec.characterGuid);

        if (rec.lifecycle == BotLifecycle::Removing)
        {
            if (p)
            {
                ::WorldSession* playerSess = p->GetSession();
                if (playerSess && playerSess == sWorld.FindHeadlessSession(rec.characterGuid))
                    BotSessionAdapter::LogoutHeadlessSession(playerSess, rec.characterGuid, true);
                else
                {
                    sLog.outString("TortoiseBots: Bot %s reclaimed by network during removal — releasing",
                        rec.characterGuid.GetString().c_str());
                    it = m_bots.erase(it);
                    continue;
                }
            }
            else if (sWorld.CancelPendingHeadlessSession(rec.characterGuid))
            {
                sLog.outString("TortoiseBots: Bot %s removed before session activation",
                    rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
                continue;
            }
            else if (!sWorld.FindHeadlessSession(rec.characterGuid))
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
            ::WorldSession* playerSess = p->GetSession();
            bool isOwnHeadless = playerSess && playerSess == sWorld.FindHeadlessSession(rec.characterGuid);
            if (!isOwnHeadless)
            {
                sLog.outString("TortoiseBots: Bot %s reclaimed by %s session acct %u (headless %u) — releasing",
                    rec.characterGuid.GetString().c_str(), playerSess && playerSess->IsHeadless() ? "headless" : "network",
                    playerSess ? playerSess->GetAccountId() : 0, playerSess && playerSess->IsHeadless());
                it = m_bots.erase(it);
                continue;
            }
            if (p->IsInWorld())
            {
                if (!rec.enteredWorld)
                {
                    OnPlayerLogin(p);
                }
                rec.enteredWorld = true;
                rec.lifecycle = BotLifecycle::InWorld;
                ++rec.ticksInWorld;
            }
        }
        else if (rec.lifecycle == BotLifecycle::PendingLogin)
        {
            if (!sWorld.FindHeadlessSession(rec.characterGuid) &&
                !sWorld.HasPendingHeadlessSession(rec.characterGuid))
            {
                sLog.outError("TortoiseBots: Bot %s login ended without a session",
                    rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
                continue;
            }
        }
        else if (!sWorld.FindHeadlessSession(rec.characterGuid) &&
                 !sWorld.HasPendingHeadlessSession(rec.characterGuid))
        {
            sLog.outString("TortoiseBots: Bot %s session ended — releasing",
                rec.characterGuid.GetString().c_str());
            it = m_bots.erase(it);
            continue;
        }
        ++it;
    }

    // Mature PlayerbotAI still exposes a donor-shaped population view for
    // perception/social queries. Refresh it from the authoritative native
    // records immediately before any AI update; it never owns sessions.
    PlayerbotAI::ProcessDelayedPackets();
    sRandomBotFacade.SyncNativePlayers();
    UpdateBots(diff);

    if (m_autoTestEnabled)
        UpdateAutoTest(diff);

    if (m_packetTestEnabled)
        UpdatePacketBridgeTest(diff);
}

void BotManager::UpdatePacketBridgeTest(uint32_t diff)
{
    (void)diff;
    ++m_packetTestTicks;

    if (!m_packetTestAccount || m_packetTestMasterGuid.IsEmpty() ||
        m_packetTestBotGuid.IsEmpty() || m_packetTestMasterGuid == m_packetTestBotGuid)
    {
        sLog.outError("TortoiseBots: PacketBridgeTest invalid fixture");
        m_packetTestEnabled = false;
        return;
    }

    if (m_packetTestStage == 0)
    {
        if (!FindBot(m_packetTestMasterGuid) && !FindBot(m_packetTestBotGuid))
        {
            AddBot(m_packetTestAccount, m_packetTestMasterGuid);
            AddBotWithMaster(m_packetTestAccount, m_packetTestBotGuid, m_packetTestMasterGuid);
            m_packetTestStage = 1;
            m_packetTestTicks = 0;
        }
        else
        {
            sLog.outError("TortoiseBots: PacketBridgeTest fixture is already active");
            m_packetTestEnabled = false;
        }
        return;
    }

    Player* master = sObjectAccessor.FindPlayer(m_packetTestMasterGuid);
    Player* bot = sObjectAccessor.FindPlayer(m_packetTestBotGuid);
    if (m_packetTestStage == 1)
    {
        if (master && bot && master->IsInWorld() && bot->IsInWorld())
        {
            WorldSession* originalSession = master->GetSession();
            WorldSession* syntheticNetwork = new WorldSession(
                m_packetTestAccount, nullptr, sAccountMgr.GetSecurity(m_packetTestAccount),
                time_t(0), LOCALE_enUS, "packet-test", 0, SessionTransport::Network);
            syntheticNetwork->SetPlayer(master);
            master->SetSession(syntheticNetwork);

            WorldPacket invite;
            invite << bot->GetName() << uint32(0);
            syntheticNetwork->HandleGroupInviteOpcode(invite);

            master->SetSession(originalSession);
            syntheticNetwork->SetPlayer(nullptr);
            delete syntheticNetwork;

            sLog.outString("TortoiseBots: PacketBridgeTest native master invite emitted; awaiting PlayerbotAI accept");
            m_packetTestStage = 2;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 600)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest login timeout");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 2)
    {
        if (master && bot && master->GetGroup() && bot->GetGroup() == master->GetGroup() &&
            master->GetGroup()->IsMember(bot->GetObjectGuid()))
        {
            sLog.outString("TortoiseBots: PacketBridgeTest group invite/accept PASSED — mature PlayerbotAI joined group");

            // Exercise the native uninvite handler for cleanup. Incoming packet
            // delivery is intentionally not injected here: the strict runtime
            // proof must come from a real Network session through Penqle's
            // ServerScript::CanPacketReceive hook.
            WorldSession* originalSession = master->GetSession();
            WorldSession* syntheticNetwork = new WorldSession(
                m_packetTestAccount, nullptr, sAccountMgr.GetSecurity(m_packetTestAccount),
                time_t(0), LOCALE_enUS, "packet-test", 0, SessionTransport::Network);
            syntheticNetwork->SetPlayer(master);
            master->SetSession(syntheticNetwork);

            WorldPacket uninvite(CMSG_GROUP_UNINVITE);
            uninvite << bot->GetName();
            syntheticNetwork->HandleGroupUninviteOpcode(uninvite);

            master->SetSession(originalSession);
            syntheticNetwork->SetPlayer(nullptr);
            delete syntheticNetwork;
            sLog.outString("TortoiseBots: PacketBridgeTest native uninvite cleanup applied; live incoming hook remains client-gated");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest group invite/accept FAILED");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 3)
    {
        if (FindBot(m_packetTestBotGuid))
            RemoveBot(m_packetTestBotGuid, true);
        if (FindBot(m_packetTestMasterGuid))
            RemoveBot(m_packetTestMasterGuid, true);

        if (!FindBot(m_packetTestBotGuid) && !FindBot(m_packetTestMasterGuid) &&
            !sWorld.FindHeadlessSession(m_packetTestBotGuid) &&
            !sWorld.FindHeadlessSession(m_packetTestMasterGuid) &&
            !sWorld.HasPendingHeadlessSession(m_packetTestBotGuid) &&
            !sWorld.HasPendingHeadlessSession(m_packetTestMasterGuid))
        {
            sLog.outString("TortoiseBots: PacketBridgeTest cleanup PASSED");
            m_packetTestEnabled = false;
        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest cleanup timed out");
            m_packetTestEnabled = false;
        }
    }
}

void BotManager::FinishAutoTest(bool passed)
{
    m_autoTestPassed = passed;
    if (FindBot(m_autoTestGuid))
        RemoveBot(m_autoTestGuid, true);
    m_autoState = AutoState::CleaningUp;
    m_autoTestTicks = 0;
}

void BotManager::UpdateAutoTest(uint32_t diff)
{
    (void)diff;
    ++m_autoTestTicks;

    switch (m_autoState)
    {
        case AutoState::Idle:
            if (m_autoTestTicks > 20)
            {
                sLog.outString("TortoiseBots: AutoTest step 1 — login bot %s", m_autoTestGuid.GetString().c_str());
                if (AddBot(m_autoTestAccount, m_autoTestGuid))
                {
                    m_autoState = AutoState::LoggingIn;
                    m_autoTestTicks = 0;
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest could not queue its fixture");
                    FinishAutoTest(false);
                }
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
                        ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        ::WorldSession* sess = sWorld.FindHeadlessSession(rec->characterGuid);
                        std::string sessInfo = sess ? "headless" : "<null>";
                        bool loading = sess ? sess->PlayerLoading() : false;
                        std::string playerInfo = p ? (p->IsInWorld() ? "IsInWorld" : "not InWorld") : "FindPlayer null";
                        sLog.outString("TortoiseBots: AutoTest LoggingIn tick %u sess %s (%p) acct %u loading %u player %s pending %u", m_autoTestTicks, sessInfo.c_str(), (void*)sess, rec->accountId, loading, playerInfo.c_str(), rec->lifecycle == BotLifecycle::PendingLogin);
                        if (sess)
                            sLog.outString("TortoiseBots:   sess details transport %u m_Socket %p IsHeadless %u", (uint32_t)sess->GetTransport(), (void*)sess->GetSocket(), sess->IsHeadless());
                    }
                    if (m_autoTestTicks > 400)
                    {
                        ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        ::WorldSession* sess = sWorld.FindHeadlessSession(rec->characterGuid);
                        sLog.outError("TortoiseBots: AutoTest login timeout after %u ticks (sess %s loading %u player %s)", m_autoTestTicks, sess ? "headless" : "null", sess ? sess->PlayerLoading() : 0, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                        FinishAutoTest(false);
                    }
                }
            }
            else
            {
                sLog.outError("TortoiseBots: AutoTest LoggingIn but FindBot null tick %u", m_autoTestTicks);
                if (m_autoTestTicks > 400)
                    FinishAutoTest(false);
            }
            break;
        case AutoState::InWorld:
            if (m_autoTestTicks > 600)
            {
                if (BotRecord* rec = FindBot(m_autoTestGuid))
                {
                    ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    if (p)
                    {
                        p->SaveToDB(false, false);
                        sLog.outString("TortoiseBots: AutoTest step 3 — saved bot %s", p->GetName());
                    }
                    else
                    {
                        sLog.outError("TortoiseBots: AutoTest expected an in-world fixture before save");
                        FinishAutoTest(false);
                        break;
                    }
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest lost its fixture before save");
                    FinishAutoTest(false);
                    break;
                }
                m_autoState = AutoState::Saving;
                m_autoTestTicks = 0;
            }
            break;
        case AutoState::Saving:
            if (m_autoTestTicks > 20)
            {
                sLog.outString("TortoiseBots: AutoTest step 4 — logout bot");
                if (RemoveBot(m_autoTestGuid, true))
                {
                    m_autoState = AutoState::LoggingOut;
                    m_autoTestTicks = 0;
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest could not request fixture logout");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::LoggingOut:
            if (m_autoTestTicks > 40)
            {
                if (!sObjectAccessor.FindPlayer(m_autoTestGuid) &&
                    !FindBot(m_autoTestGuid) &&
                    !sWorld.FindHeadlessSession(m_autoTestGuid) &&
                    !sWorld.HasPendingHeadlessSession(m_autoTestGuid))
                {
                    sLog.outString("TortoiseBots: AutoTest step 5 — re-login bot");
                    if (AddBot(m_autoTestAccount, m_autoTestGuid))
                    {
                        m_autoState = AutoState::Relogging;
                        m_autoTestTicks = 0;
                    }
                    else
                    {
                        sLog.outError("TortoiseBots: AutoTest could not queue fixture relogin");
                        FinishAutoTest(false);
                    }
                }
                else if (m_autoTestTicks > 400)
                {
                    sLog.outError("TortoiseBots: AutoTest logout timeout");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::Relogging:
            if (BotRecord* rec = FindBot(m_autoTestGuid))
            {
                if (rec->enteredWorld)
                {
                    sLog.outString("TortoiseBots: AutoTest step 6 — bot re-entered world, lifecycle PASSED; cleaning up");
                    FinishAutoTest(true);
                }
                else if (m_autoTestTicks > 400)
                {
                    sLog.outError("TortoiseBots: AutoTest relog timeout");
                    FinishAutoTest(false);
                }
                else if (m_autoTestTicks % 40 == 0)
                {
                    ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    ::WorldSession* sess = sWorld.FindHeadlessSession(rec->characterGuid);
                    sLog.outString("TortoiseBots: AutoTest Relogging tick %u sess %s pending %u player %s", m_autoTestTicks, sess ? "headless" : "null", rec->lifecycle == BotLifecycle::PendingLogin, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                }
            }
            else if (m_autoTestTicks > 400)
            {
                sLog.outError("TortoiseBots: AutoTest relog lost its fixture");
                FinishAutoTest(false);
            }
            break;
        case AutoState::CleaningUp:
            if (!FindBot(m_autoTestGuid) &&
                !sObjectAccessor.FindPlayer(m_autoTestGuid) &&
                !sWorld.FindHeadlessSession(m_autoTestGuid) &&
                !sWorld.HasPendingHeadlessSession(m_autoTestGuid))
            {
                sLog.outString("TortoiseBots: AutoTest cleanup %s; diagnostic disabled",
                    m_autoTestPassed ? "PASSED" : "FAILED");
                m_autoTestEnabled = false;
                m_autoState = AutoState::Done;
            }
            else if (m_autoTestTicks > 400)
            {
                sLog.outError("TortoiseBots: AutoTest cleanup timed out; diagnostic disabled with lifecycle state still active");
                m_autoTestEnabled = false;
                m_autoState = AutoState::Done;
            }
            break;
        case AutoState::Done:
            break;
    }
}

} // namespace TortoiseBots
