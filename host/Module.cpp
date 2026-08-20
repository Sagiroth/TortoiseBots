#include "Module.h"
#include "BotHostAdapter.h"
#include "Log.h"

namespace TortoiseBots {

void Initialize()
{
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
    sLog.outString("TortoiseBots: Shutdown — PlayerBots module unloaded");
}

void RegisterWorldListener()
{
    // Ensure the singleton listener is created and its factory has been queued.
    // The factory was queued at static-init time; if for any reason it wasn't
    // (e.g. whole-archive not used), register directly now.
    BotHostAdapter::Instance().EnsureRegistered();
}

} // namespace TortoiseBots
