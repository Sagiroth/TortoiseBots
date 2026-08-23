#include "Module.h"

#include "BotChatAdapter.h"
#include "BotHostAdapter.h"
#include "BotPlayerAdapter.h"

namespace TortoiseBots {

void RegisterScripts()
{
    new BotHostAdapter();
    new BotPlayerAdapter();
    new BotChatAdapter();
}

} // namespace TortoiseBots
