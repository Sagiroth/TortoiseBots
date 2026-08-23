#include "BotHostAdapter.h"

#include "../runtime/BotManager.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "Log.h"

namespace TortoiseBots {

BotHostAdapter::BotHostAdapter()
    : WorldScript("tortoisebots_world", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN })
{
}

void BotHostAdapter::OnStartup()
{
    bool configured = sPlayerbotAIConfig.Initialize();
    sLog.outString("TortoiseBots: native module loaded (AI %s)", configured ? "enabled" : "disabled");
}

void BotHostAdapter::OnUpdate(uint32 diff)
{
    ++m_ticks;
    BotManager::Instance().OnWorldUpdate(diff);
}

void BotHostAdapter::OnShutdown()
{
    sLog.outString("TortoiseBots: native module shutting down after %u world ticks", m_ticks);
}

} // namespace TortoiseBots
