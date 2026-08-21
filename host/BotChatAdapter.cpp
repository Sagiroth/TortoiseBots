#include "BotChatAdapter.h"
#include "../commands/BotCommands.h"

namespace TortoiseBots {

// Keep the adapter from being discarded when linking tortoise_bots as a static
// library with --as-needed. The real guarantee is whole-archive plus the
// explicit EnsureRegistered call from Module/BotHostAdapter, but retain the
// used attribute as a fallback.
#if defined(__GNUC__)
__attribute__((used))
#endif
static BotChatAdapter* s_botChatAdapterInstancePtr = nullptr;

BotChatAdapter& BotChatAdapter::Instance()
{
    static BotChatAdapter instance;
    s_botChatAdapterInstancePtr = &instance;
    return instance;
}

bool BotChatAdapter::TryHandleCommand(ChatHandler* handler, char const* text)
{
    // Thin delegation — no parsing beyond what BotCommands already does, no
    // movement or behavior logic here. BotCommands owns the "bot ..." prefix
    // check and the add/remove/follow dispatch to BotManager/BotController.
    return BotCommands::TryHandleBotCommand(handler, text);
}

void BotChatAdapter::EnsureRegistered()
{
    if (m_registered)
        return;
    RegisterChatCommandInterceptor(this);
    m_registered = true;
}

void BotChatAdapter::EnsureUnregistered()
{
    if (!m_registered)
        return;
    UnregisterChatCommandInterceptor(this);
    m_registered = false;
}

} // namespace TortoiseBots
