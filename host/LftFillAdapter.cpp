#include "LftFillAdapter.h"

#include "../runtime/LftBotFillService.h"

namespace TortoiseBots {

LftFillAdapter::LftFillAdapter()
    : WorldScript("tortoisebots_lft_fill", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN })
{
}

void LftFillAdapter::OnStartup()
{
    LftBotFillService::Instance().Initialize();
}

void LftFillAdapter::OnUpdate(uint32 diff)
{
    LftBotFillService::Instance().Update(diff);
}

void LftFillAdapter::OnShutdown()
{
    LftBotFillService::Instance().Shutdown();
}

} // namespace TortoiseBots
