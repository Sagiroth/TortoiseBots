#include "BotHostAdapter.h"

#include "../runtime/BotManager.h"
#include "../runtime/RandomBotService.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "Config/Config.h"
#include "Log.h"

namespace TortoiseBots {

BotHostAdapter::BotHostAdapter()
    : WorldScript("tortoisebots_world", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN })
{
}

void BotHostAdapter::OnStartup()
{
    bool configured = sPlayerbotAIConfig.Initialize();
    RandomBotService::Instance().Initialize();

    if (sConfig.GetBoolDefault("TortoiseBots.PendingAddRemoveTest", false))
    {
        uint32 accountId = sConfig.GetIntDefault("TortoiseBots.PendingAddRemoveTest.AccountId", 0);
        uint32 guidLow = sConfig.GetIntDefault("TortoiseBots.PendingAddRemoveTest.CharacterGuid", 0);
        if (accountId && guidLow)
            BotManager::Instance().RunPendingAddRemoveTest(accountId, ObjectGuid(HIGHGUID_PLAYER, guidLow));
        else
            sLog.outError("TortoiseBots: PendingAddRemoveTest enabled without AccountId/CharacterGuid");
    }

    if (sConfig.GetBoolDefault("TortoiseBots.AutoTest", false))
    {
        uint32 accountId = sConfig.GetIntDefault("TortoiseBots.AutoTest.AccountId", 0);
        uint32 guidLow = sConfig.GetIntDefault("TortoiseBots.AutoTest.CharacterGuid", 0);
        if (accountId && guidLow)
            BotManager::Instance().SetAutoTestEnabled(true, accountId, ObjectGuid(HIGHGUID_PLAYER, guidLow));
        else
            sLog.outError("TortoiseBots: AutoTest enabled without AccountId/CharacterGuid");
    }

    sLog.outString("TortoiseBots: native module loaded (AI %s)", configured ? "enabled" : "disabled");
}

void BotHostAdapter::OnUpdate(uint32 diff)
{
    ++m_ticks;
    BotManager::Instance().OnWorldUpdate(diff);
    RandomBotService::Instance().Update(diff);
}

void BotHostAdapter::OnShutdown()
{
    RandomBotService::Instance().Shutdown();
    sLog.outString("TortoiseBots: native module shutting down after %u world ticks", m_ticks);
}

} // namespace TortoiseBots
