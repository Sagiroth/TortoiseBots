#include "BotHostAdapter.h"
#include "BotSessionAdapter.h"
#include "../runtime/BotManager.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Config/Config.h"
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

    // AutoTest is gated by `TortoiseBots.AutoTest` (default OFF). BUILD_PLAYERBOTS=ON
    // alone must never log a test character. Enable via `TortoiseBots.AutoTest = 1`
    // and `TortoiseBots.AutoTest.AccountId` / `CharacterGuid` in mangosd.conf.
    if (m_ticks == 200 && !BotManager::Instance().IsAutoTestEnabled())
    {
        extern bool sTortoiseBotsAutoTestAllowed();
        if (sTortoiseBotsAutoTestAllowed())
        {
            uint32 acct = sConfig.GetIntDefault("TortoiseBots.AutoTest.AccountId", 4);
            uint32 guidLow = sConfig.GetIntDefault("TortoiseBots.AutoTest.CharacterGuid", 1);
            if (guidLow == 0)
                return;
            ObjectGuid testGuid(HIGHGUID_PLAYER, guidLow);
            if (!sObjectMgr.GetPlayerDataByGUID(guidLow))
            {
                sLog.outString("TortoiseBots: AutoTest requested but character guid %u not in cache — skipping", guidLow);
                return;
            }
            sLog.outString("TortoiseBots: auto-triggering 7-step headless spike test (acct %u guid %u)", acct, guidLow);
            BotManager::Instance().SetAutoTestEnabled(true, acct, testGuid);
        }
    }

    // Phase 3: drive BotManager (headless session lifecycle, auto-test, etc.).
    // BotManager itself does no DB query per tick, no sync HTTP, just state checks.
    BotManager::Instance().OnWorldUpdate(diff);
}

bool sTortoiseBotsAutoTestAllowed()
{
    // Gate through configuration — BUILD_PLAYERBOTS=ON alone must never auto-log a test character.
    // Default OFF. Enable by adding `TortoiseBots.AutoTest = 1` to mangosd.conf
    // (or tortoise_bots.conf if sConfig is extended to load it) and restarting.
    return sConfig.GetBoolDefault("TortoiseBots.AutoTest", false);
}

} // namespace TortoiseBots
