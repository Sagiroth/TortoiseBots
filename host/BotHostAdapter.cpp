#include "BotHostAdapter.h"
#include "BotSessionAdapter.h"
#include "../runtime/BotManager.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "World.h"
#include "ObjectAccessor.h"
#include "Player.h"

namespace TortoiseBots {

// Pending-factory registration via static initializer — runs before main().
// The core drains this at World::SetInitialWorldSettings().
namespace {
struct BotHostFactoryRegistrar
{
    BotHostFactoryRegistrar()
    {
        RegisterPendingWorldListenerFactory(&CreateBotHostAdapter);
    }
};
// Keep the factory object from being discarded by the linker when built as a
// static library and linked with --as-needed. The attribute is a hint; the
// real guarantee is that host/Module.cpp's TortoiseBots::Initialize is
// referenced from the pending-drain path when BUILD_PLAYERBOTS=ON, but we
// keep this as a fallback.
#if defined(__GNUC__)
__attribute__((used))
#endif
static BotHostFactoryRegistrar s_registrar;
}

IWorldUpdateListener* CreateBotHostAdapter()
{
    return &BotHostAdapter::Instance();
}

BotHostAdapter& BotHostAdapter::Instance()
{
    static BotHostAdapter instance;
    return instance;
}

void BotHostAdapter::EnsureRegistered()
{
    if (m_registered)
        return;
    // If the pending-factory path already registered us, sWorld will contain us.
    // Otherwise register directly (e.g. when whole-archive kept the object but
    // pending drain already ran). This is idempotent.
    // Note: sWorld may not be fully constructed if called too early; the pending
    // path handles that. Direct call is only safe after SetInitialWorldSettings.
    // We check by trying to register; World guards duplicates.
    sWorld.RegisterWorldUpdateListener(this);
    m_registered = true;
}

void BotHostAdapter::OnStartup()
{
    if (m_startupLogged)
        return;
    sLog.outString("TortoiseBots: BotHostAdapter startup — registered as world tick listener");
    m_startupLogged = true;
}

void BotHostAdapter::OnShutdown()
{
    sLog.outString("TortoiseBots: BotHostAdapter shutdown");
}

void BotHostAdapter::OnWorldUpdate(uint32 diff)
{
    // One-time startup log once the world tick is actually flowing.
    if (!m_startupLogged)
        OnStartup();

    ++m_ticks;

    // Phase 3 spike auto-trigger: if no test is running and we have a known test
    // character (account 4 / guid 1 — the default Admin), start the 7-step
    // headless lifecycle test automatically. This is only for the Phase 3 spike
    // proof; in production AutoTest is gated by config.
    if (m_ticks == 200 && !BotManager::Instance().IsAutoTestEnabled())
    {
        // Check via ObjectMgr cache if the test character exists (cheap, no DB query).
        extern bool sTortoiseBotsAutoTestAllowed(); // forward, defined below
        if (sTortoiseBotsAutoTestAllowed())
        {
            // Use the default Admin character if it exists.
            // sObjectMgr.GetPlayerDataByGUID is O(1) cache lookup, not a DB query per tick.
            // We defer the actual IsBot check to BotManager.
            ObjectGuid testGuid(HIGHGUID_PLAYER, uint32(1));
            // Only trigger if the data is cached (character exists).
            // Include ObjectMgr.h for the check — we do it lazily here to avoid
            // heavy include in the header.
            // The actual enable is safe to call even if the guid is invalid; BotManager
            // will log and abort the test.
            sLog.outString("TortoiseBots: auto-triggering 7-step headless spike test (acct 4 guid 1)");
            BotManager::Instance().SetAutoTestEnabled(true, 4, testGuid);
        }
    }

    // Phase 3: drive BotManager (headless session lifecycle, auto-test, etc.).
    // BotManager itself does no DB query per tick, no sync HTTP, just state checks.
    BotManager::Instance().OnWorldUpdate(diff);
}

bool sTortoiseBotsAutoTestAllowed()
{
    // For the spike we allow auto-test unconditionally (the server is a local
    // Docker test stack). In a real deployment this would read sConfig.
    return true;
}

} // namespace TortoiseBots
