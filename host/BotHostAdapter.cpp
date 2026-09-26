#include "BotHostAdapter.h"

#include "../behavior/PlayerConvenience.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/BotManager.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/RandomBotService.h"
#include "../runtime/AhMarketService.h"
#include "../runtime/BattlegroundQueueService.h"
#include "../runtime/ObservabilityEmitter.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "Config/Config.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "ModuleLog.h"
#include "ModuleVersion.h"

#include <cctype>
#include <string>

namespace TortoiseBots {

namespace
{
bool IsDisposableFixture(uint32 accountId, uint32 guidLow, char const* testName)
{
    PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(guidLow);
    if (!data)
    {
        sLog.outError("TortoiseBots: %s fixture guid %u does not exist", testName, guidLow);
        return false;
    }

    if (data->uiAccount != accountId)
    {
        sLog.outError("TortoiseBots: %s fixture %s belongs to account %u, not configured account %u",
            testName, data->sName.c_str(), data->uiAccount, accountId);
        return false;
    }

    std::string name = data->sName;
    for (char& character : name)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    bool disposableCharacter = name.rfind("TBPLAY", 0) == 0;
    bool disposableAccount = false;
    if (auto account = LoginDatabase.PQuery("SELECT username FROM account WHERE id = '%u'", accountId))
    {
        std::string accountName = account->Fetch()[0].GetCppString();
        for (char& character : accountName)
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        disposableAccount = accountName == "TBPLAY";
    }

    if (!disposableCharacter && !disposableAccount)
    {
        sLog.outError("TortoiseBots: %s fixture %s is not disposable; use the TBPLAY account or a TBPLAY-prefixed character",
            testName, data->sName.c_str());
        return false;
    }

    return true;
}
}

BotHostAdapter::BotHostAdapter()
    : WorldScript("tortoisebots_world", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN,
        WORLDHOOK_ON_AFTER_CONFIG_LOAD })
{
}

void BotHostAdapter::OnAfterConfigLoad(bool /*reload*/)
{
    ModuleLog::Instance().ApplyConfig();
}

void BotHostAdapter::OnStartup()
{
    // World::LoadConfigSettings() runs before module scripts are constructed,
    // so the startup pass of OnAfterConfigLoad never reaches this script. Read
    // the level here; the hook then covers `.reload config`.
    ModuleLog::Instance().ApplyConfig();

    bool configured = sPlayerbotAIConfig.Initialize();
    RandomBotService::Instance().Initialize();
    BattlegroundQueueService::Instance().Initialize();

    if (sConfig.GetBoolDefault("TortoiseBots.PendingAddRemoveTest", false))
    {
        uint32 accountId = sConfig.GetIntDefault("TortoiseBots.PendingAddRemoveTest.AccountId", 0);
        uint32 guidLow = sConfig.GetIntDefault("TortoiseBots.PendingAddRemoveTest.CharacterGuid", 0);
        if (accountId && guidLow && IsDisposableFixture(accountId, guidLow, "PendingAddRemoveTest"))
            BotManager::Instance().RunPendingAddRemoveTest(accountId, ObjectGuid(HIGHGUID_PLAYER, guidLow));
        else
            sLog.outError("TortoiseBots: PendingAddRemoveTest requires a valid disposable TBPLAY fixture");
    }

    if (sConfig.GetBoolDefault("TortoiseBots.AutoTest", false))
    {
        uint32 accountId = sConfig.GetIntDefault("TortoiseBots.AutoTest.AccountId", 0);
        uint32 guidLow = sConfig.GetIntDefault("TortoiseBots.AutoTest.CharacterGuid", 0);
        if (accountId && guidLow && IsDisposableFixture(accountId, guidLow, "AutoTest"))
            BotManager::Instance().SetAutoTestEnabled(true, accountId, ObjectGuid(HIGHGUID_PLAYER, guidLow));
        else
            sLog.outError("TortoiseBots: AutoTest requires a valid disposable TBPLAY fixture");
    }

    if (sConfig.GetBoolDefault("TortoiseBots.PacketBridgeTest", false))
    {
        uint32 accountId = sConfig.GetIntDefault("TortoiseBots.PacketBridgeTest.AccountId", 0);
        uint32 masterGuid = sConfig.GetIntDefault("TortoiseBots.PacketBridgeTest.MasterGuid", 0);
        uint32 botGuid = sConfig.GetIntDefault("TortoiseBots.PacketBridgeTest.BotGuid", 0);
        if (accountId && masterGuid && botGuid && masterGuid != botGuid &&
            IsDisposableFixture(accountId, masterGuid, "PacketBridgeTest master") &&
            IsDisposableFixture(accountId, botGuid, "PacketBridgeTest bot"))
            BotManager::Instance().SetPacketBridgeTestEnabled(true, accountId,
                ObjectGuid(HIGHGUID_PLAYER, masterGuid), ObjectGuid(HIGHGUID_PLAYER, botGuid));
        else
            sLog.outError("TortoiseBots: PacketBridgeTest requires two distinct disposable TBPLAY fixtures on one account");
    }

    TB_LOG_BASIC("TortoiseBots %s (AI %s)", BuildVersion().c_str(), configured ? "enabled" : "disabled");
    TB_LOG_BASIC("%s", AttributionLine().c_str());
    ObservabilityEmitter::Instance().Initialize();
}
void BotHostAdapter::OnUpdate(uint32 diff)
{
    ++m_ticks;
    // Expire stale leases before services select candidates (issue #89).
    BotActivityLeaseManager::Instance().Update(diff);
    BotManager::Instance().OnWorldUpdate(diff);
    PlayerConvenience::Instance().Update(diff);
    RandomBotService::Instance().Update(diff);
    // Issue #192: complete queued hire provisions, then sweep grace timers.
    HireProvisionService::Instance().Update(diff);
    HireLifecycle::Instance().Update(diff);
    AhMarketService::Instance().Update(diff);
    BattlegroundQueueService::Instance().Update(diff);
    ObservabilityEmitter::Instance().Update(diff);
}

void BotHostAdapter::OnShutdown()
{
    ObservabilityEmitter::Instance().Shutdown();
    BattlegroundQueueService::Instance().Shutdown();
    RandomBotService::Instance().Shutdown();
    BotActivityLeaseManager::Instance().Clear();
    TB_LOG_BASIC("TortoiseBots: native module shutting down after %u world ticks", m_ticks);
}

} // namespace TortoiseBots
