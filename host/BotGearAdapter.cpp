#include "BotGearAdapter.h"

#include "../runtime/BotGearService.h"

namespace TortoiseBots {

BotGearAdapter::BotGearAdapter()
    : WorldScript("tortoisebots_bot_gear", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN })
{
}

void BotGearAdapter::OnStartup()
{
}

void BotGearAdapter::OnUpdate(uint32 diff)
{
    BotGearService::Instance().Update(diff);
}

void BotGearAdapter::OnShutdown()
{
}

} // namespace TortoiseBots
