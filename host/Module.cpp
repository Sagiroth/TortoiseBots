#include "Module.h"

#include "BotChatAdapter.h"
#include "BotHostAdapter.h"
#include "BotPacketAdapter.h"
#include "BotPlayerAdapter.h"

namespace TortoiseBots {

void RegisterScripts()
{
    new BotHostAdapter();
    new BotPacketAdapter();
    new BotPlayerAdapter();
    new BotChatAdapter();
}

} // namespace TortoiseBots
