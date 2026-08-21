#include "Module.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotHostAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotChatAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"

namespace TortoiseBots {

void Initialize()
{
    // pi-lens-ignore: clang:undeclared_var_use,clang:incomplete_member_access
    sLog.outString("TortoiseBots: Initialize — PlayerBots module loaded");
    // The listener is registered via the pending-factory path (static initializer
    // in BotHostAdapter.cpp) which is drained at World::SetInitialWorldSettings().
    // This function exists so mangosd has at least one referenced symbol from the
    // module, keeping the static library's object from being discarded by --as-needed.
    // It is safe to call twice.
    RegisterWorldListener();
}

void Shutdown()
{
    // pi-lens-ignore: clang:undeclared_var_use,clang:incomplete_member_access
    sLog.outString("TortoiseBots: Shutdown — PlayerBots module unloaded");
}

void RegisterWorldListener()
{
    // Ensure the singleton listener is created and its factory has been queued.
    // The factory was queued at static-init time; if for any reason it wasn't
    // (e.g. whole-archive not used), register directly now.
    BotHostAdapter::Instance().EnsureRegistered();
    // Chat interceptor for in-game ".bot" commands (thin, generic seam).
    // Registered here as well so the module's chat surface is ready even if
    // the factory path was not used. Idempotent.
    BotChatAdapter::Instance().EnsureRegistered();
}

} // namespace TortoiseBots
