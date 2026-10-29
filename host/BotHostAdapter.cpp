#include "BotHostAdapter.h"

#include "../behavior/PlayerConvenience.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/BotManager.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireGearCache.h"
#include "../runtime/RandomBotService.h"
#include "../runtime/AhMarketService.h"
#include "../runtime/BattlegroundQueueService.h"
#include "../runtime/BotLfgService.h"
#include "../runtime/ObservabilityEmitter.h"
#include "../runtime/CombatMonitorService.h"
#include "../runtime/RunTrace.h"
#include "BotChatAdapter.h"
#include "BotPacketAdapter.h"
#include "HireRecruiterScript.h"
#include "../commands/BotCommandQueue.h"
#include "../commands/BotCommands.h"
#include "../commands/BotCommandHandlers.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "Config/Config.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "ModuleLog.h"
#include "ModuleVersion.h"

#include <atomic>
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

// Set by a `.reload config` (OnAfterConfigLoad, on whichever thread ran the
// command); the next world tick reads the run leader's keys again (OnUpdate),
// on the thread that reads them.
static std::atomic<bool> s_dungeonRunKeysDue{false};
// The same for the dashboard's live stream keys: read on the world thread.
static std::atomic<bool> s_realtimeKeysDue{false};

void BotHostAdapter::OnAfterConfigLoad(bool reload)
{
    ModuleLog::Instance().ApplyConfig();
    if (reload)
    {
        s_dungeonRunKeysDue.store(true);
        s_realtimeKeysDue.store(true);
    }
}

void BotHostAdapter::OnStartup()
{
    // World::LoadConfigSettings() runs before module scripts are constructed,
    // so the startup pass of OnAfterConfigLoad never reaches this script. Read
    // the level here; the hook then covers `.reload config`.
    ModuleLog::Instance().ApplyConfig();

    bool configured = sPlayerbotAIConfig.Initialize();
    // The dashboard's boss list: one pass over the creature spawns, here at start (the plans were just sealed), never in a 2 s snapshot.
    // Only for the dungeon features that read it; everyone else skips the scan and the readers fall back.
    if (sPlayerbotAIConfig.dungeonTactics || sPlayerbotAIConfig.dungeonSelfClear)
        BotCommands::BuildBossSpawnCache();
    HireRecruiterScript::SyncUnlockFees();
    HireRecruiterScript::SyncHireUnlockChain();
    // Issue #490: static hire-gear sets, one pass over the table rows.
    HireGearCache::Instance().Load();
    RandomBotService::Instance().Initialize();
    BattlegroundQueueService::Instance().Initialize();
    ClaimLifecycle::Instance().Initialize();
    // Run trace: mark the summaries of runs the last server never finalised.
    RunTraceService::Instance().Initialize();

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
    // The dashboard's Commanders tab: the standing remote-commander bindings.
    ObservabilityEmitter::Instance().SetCommanderProvider(
        [](std::vector<CommanderTelemetry::BindingRef>& out) { BotCommands::CollectCommanderTelemetry(out); });
    // Finished combat fights: the monitor's TakePayloads fills the payloads, the emitter sends each one.
    // The monitor's flag follows its config key and the emitter only: ungated here, retaken on the 1 Hz tick below.
    CombatMonitorService::Instance().SyncConfig(sPlayerbotAIConfig.combatMonitor &&
        ObservabilityEmitter::Instance().IsEnabled());
    ObservabilityEmitter::Instance().SetCombatProvider(
        [](std::vector<std::string>& out) {
            ObservabilityEmitter& emitter = ObservabilityEmitter::Instance();
            CombatMonitorService::Instance().TakePayloads(out, emitter.ProtocolVersion(), emitter.SessionId(),
                [&emitter]() { return emitter.SessionId() ? emitter.NextSeq() : 0; }, emitter.NowUnix());
        });
    // The live fight stream (COMBAT_EVENTS) on its own 100 ms cadence, and the
    // elite/boss cast poller every 250 ms while a fight runs.
    ObservabilityEmitter::Instance().SetCombatEventProvider(
        [](std::vector<std::string>& out, uint32_t nowMs) {
            ObservabilityEmitter& emitter = ObservabilityEmitter::Instance();
            CombatMonitorService::Instance().TakeEventPayloads(out, emitter.ProtocolVersion(), emitter.SessionId(),
                [&emitter]() { return emitter.SessionId() ? emitter.NextSeq() : 0; }, emitter.NowUnix(),
                emitter.NowUnixMs(), nowMs, emitter.EventGapMs());
        });
    ObservabilityEmitter::Instance().SetEnemyCastPoller([]() { BotCommands::PollEnemyCasts(); });
    // The live positions (COMMANDER_POS, task S3): the bound groups' bots and the lead's last-reported enemies.
    ObservabilityEmitter::Instance().SetPositionProvider(
        [](std::vector<CommanderTelemetry::PosRef>& out, bool wantCombat, bool wantIdle) {
            BotCommands::CollectCommanderPositions(out, wantCombat, wantIdle);
        });
    // The Dungeons tab (issue #1101): the mobs of every dungeon run.
    ObservabilityEmitter::Instance().SetDungeonMobProvider(
        [](std::vector<DungeonMobTelemetry::RunRef>& runs) { BotCommands::CollectDungeonMobs(runs); });
}
void BotHostAdapter::OnUpdate(uint32 diff)
{
    // Issue #844: before anything that can send packets to bots (the command
    // drain included: a `.bot hire` drops its level-up echo right after the
    // jump), so their packet hooks on this thread reach the AI inline.
    BotPacketAdapter::MarkWorldThread();

    // A `.reload config` came: the run leader's keys (AiPlayerbot.DungeonSelfClear,
    // AiPlayerbot.DungeonRun*) are read again from aiplayerbot.conf, only those.
    if (s_dungeonRunKeysDue.exchange(false))
        sPlayerbotAIConfig.ReloadDungeonRunKeys();
    // The dashboard's live stream keys: read on this thread, live on `.reload config`.
    if (s_realtimeKeysDue.exchange(false))
        ObservabilityEmitter::Instance().ReloadRealtimeKeys();

    // Initialization stops before service configuration when AI is disabled.
    // Do not tick those services against an uninitialized registry/config.
    if (sPlayerbotAIConfig.enabled)
    {
        ++m_ticks;
        // Expire stale leases before services select candidates (issue #89).
        BotActivityLeaseManager::Instance().Update(diff);
    }

    // Issue #833: players' `.bot` / TBM commands arrive on map and
    // AsyncPackets threads and are queued; run them here, on the world thread
    // and before the bot update. This runs even with the AI disabled, so no
    // command ever executes off the world thread. Issue #846: before each
    // command it runs the bot chat and packets queued ahead of it.
    uint64_t const deferredLimit = BotChatAdapter::DrainQueuedCommands();

    // Read again: a command may have reloaded the configuration.
    if (!sPlayerbotAIConfig.enabled)
    {
        // A run trace still going ends here ("disabled"); with none, this is
        // its usual one atomic load.
        RunTraceService::Instance().Update(diff);
        return;
    }

    // Issue #839 / #844: the rest of the chat and packets that reached bots on
    // other threads (party / whisper commands included), before the bot update;
    // anything queued behind a command that arrived meanwhile waits for it
    // (issue #846).
    BotPacketAdapter::DrainDeferredPackets(deferredLimit, true);
    BotManager::Instance().OnWorldUpdate(diff);
    // Issue #884: release remote-commander bindings whose commander logged
    // out or whose group broke up (nothing runs while none is bound).
    BotCommands::UpdateRemoteCommanders(diff);
    PlayerConvenience::Instance().Update(diff);
    RandomBotService::Instance().Update(diff);
    // Issue #192: complete queued hire provisions, then sweep grace timers.
    HireProvisionService::Instance().Update(diff);
    HireLifecycle::Instance().Update(diff);
    AhMarketService::Instance().Update(diff);
    BattlegroundQueueService::Instance().Update(diff);
    ObservabilityEmitter::Instance().Update(diff);
    // Item-leg skip (spec D7): first world tick after startup: the crash
    // ledger cleanup for bots already in the world.
    BotCommands::CleanupRunGivenItemsTick();
    // Opt-in run trace (one atomic load per tick while no run is active).
    RunTraceService::Instance().Update(diff);
}

void BotHostAdapter::OnShutdown()
{
    BotLfgService::Instance().Shutdown();
    RunTraceService::Instance().Shutdown();
    ObservabilityEmitter::Instance().Shutdown();
    BattlegroundQueueService::Instance().Shutdown();
    BotCommandQueue::Instance().Clear();
    BotCommands::ClearRemoteCommanders();
    BotPacketAdapter::ClearDeferredPackets();
    BotManager::Instance().Shutdown();
    RandomBotService::Instance().Shutdown();
    BotActivityLeaseManager::Instance().Clear();
    TB_LOG_BASIC("TortoiseBots: native module shutting down after %u world ticks", m_ticks);
}

} // namespace TortoiseBots
