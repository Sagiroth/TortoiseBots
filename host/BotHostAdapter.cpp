// pi-lens-ignore: clang:pp_file_not_found
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
// pi-lens-ignore: clang:pp_file_not_found
#include <fstream>
// pi-lens-ignore: clang:pp_file_not_found
#include <sstream>

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

// pi-lens-ignore: clang:unknown_typename,clang:incomplete_member_access
IWorldUpdateListener* CreateBotHostAdapter()
{
    return &BotHostAdapter::Instance();
}

BotHostAdapter& BotHostAdapter::Instance()
{
    static BotHostAdapter instance;
    return instance;
}

// pi-lens-ignore: clang:incomplete_member_access
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

// pi-lens-ignore: clang:incomplete_member_access
void BotHostAdapter::OnStartup()
{
    if (m_startupLogged)
        return;
    sLog.outString("TortoiseBots: BotHostAdapter startup — registered as world tick listener");
    m_startupLogged = true;
}

// pi-lens-ignore: clang:incomplete_member_access
void BotHostAdapter::OnShutdown()
{
    sLog.outString("TortoiseBots: BotHostAdapter shutdown");
}

// pi-lens-ignore: clang:unknown_typename,clang:incomplete_member_access,clang:undeclared_var_use,clang:pp_file_not_found
static void CheckCommandFile()
{
    // Module-only command surface for Slice 1 manual testing — no core Chat hook yet.
    // File: /tmp/tortoisebots.cmd  — one command per line, e.g.:
    //   add Dudette
    //   remove Dudette
    //   follow Dudette
    // Also accepts "bot add Dudette" prefix. After reading, the file is truncated.
    // This is intentionally simple; the later Chat seam will replace it.
    const char* path = "/tmp/tortoisebots.cmd";
    std::ifstream in(path);
    if (!in)
        return;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (content.empty())
        return;
    // Truncate immediately to avoid re-execution on next tick
    std::ofstream out(path, std::ios::trunc);
    out.close();

    // Find first online human player to use as master/requester
    ::Player* human = nullptr;
    {
        // sObjectAccessor.GetPlayers() returns HashMapHolder<::Player>::MapType&
        auto& players = sObjectAccessor.GetPlayers();
        for (auto& kv : players)
        {
            ::Player* p = kv.second;
            if (!p || !p->IsInWorld() || p->IsBeingTeleported())
                continue;
            if (TortoiseBots::BotManager::Instance().IsBot(p->GetObjectGuid()))
                continue;
            human = p;
            break;
        }
    }
    if (!human)
    {
        sLog.outString("TortoiseBots: command file has content but no human player online — ignoring");
        return;
    }

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line))
    {
        // Trim
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        line = line.substr(a, b - a + 1);
        if (line.empty() || line[0] == '#') continue;
        // Strip optional "bot" prefix and optional leading '.'
        if (!line.empty() && line[0] == '.') line = line.substr(1);
        if (line.rfind("bot", 0) == 0)
        {
            line = line.substr(3);
            a = line.find_first_not_of(" \t");
            if (a != std::string::npos) line = line.substr(a);
            else line.clear();
        }
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string cmd, arg;
        ls >> cmd;
        std::getline(ls, arg);
        if (!arg.empty())
        {
            size_t s = arg.find_first_not_of(" \t");
            if (s != std::string::npos) arg = arg.substr(s);
            else arg.clear();
        }
        for (char& c : cmd) c = tolower(c);
        if (cmd == "add" || cmd == "login")
        {
            if (arg.empty()) { sLog.outString("TortoiseBots: add requires <botName>"); continue; }
            std::string name = arg;
            // sObjectMgr.GetPlayerDataByName expects normalized name
            // Try to find via cache
            PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
            if (!data)
            {
                // Try case-insensitive via FindPlayerByName
                if (::Player* online = sObjectAccessor.FindPlayerByName(name.c_str()))
                    data = sObjectMgr.GetPlayerDataByGUID(online->GetObjectGuid().GetCounter());
            }
            if (!data)
            {
                sLog.outString("TortoiseBots: add: character '%s' not found", name.c_str());
                continue;
            }
            ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
            uint32_t accountId = data->uiAccount;
            ::ObjectGuid masterGuid = human->GetObjectGuid();
            ::WorldSession* sess = TortoiseBots::BotManager::Instance().AddBotWithMaster(accountId, guid, masterGuid);
            if (sess)
                sLog.outString("TortoiseBots: add %s -> master %s queued", name.c_str(), human->GetName());
            else
                sLog.outString("TortoiseBots: add %s failed (already exists?)", name.c_str());
        }
        else if (cmd == "remove" || cmd == "rm" || cmd == "logout")
        {
            if (arg.empty()) { sLog.outString("TortoiseBots: remove requires <botName>"); continue; }
            std::string name = arg;
            PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
            if (!data) { sLog.outString("TortoiseBots: remove: '%s' not found", name.c_str()); continue; }
            ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
            if (TortoiseBots::BotManager::Instance().RemoveBot(guid, true))
                sLog.outString("TortoiseBots: remove %s queued", name.c_str());
            else
                sLog.outString("TortoiseBots: remove %s not found", name.c_str());
        }
        else if (cmd == "follow")
        {
            if (arg.empty()) { sLog.outString("TortoiseBots: follow requires <botName>"); continue; }
            std::string name = arg;
            PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
            if (!data) { sLog.outString("TortoiseBots: follow: '%s' not found", name.c_str()); continue; }
            ::ObjectGuid botGuid(HIGHGUID_PLAYER, data->uiGuid);
            ::ObjectGuid masterGuid = human->GetObjectGuid();
            if (TortoiseBots::BotManager::Instance().SetBotFollow(botGuid, masterGuid))
                sLog.outString("TortoiseBots: follow %s -> %s", name.c_str(), human->GetName());
            else
                sLog.outString("TortoiseBots: follow %s not found", name.c_str());
        }
        else
        {
            sLog.outString("TortoiseBots: unknown command '%s' in %s", cmd.c_str(), path);
        }
    }
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
void BotHostAdapter::OnWorldUpdate(uint32 diff)
{
    // One-time startup log once the world tick is actually flowing.
    if (!m_startupLogged)
        OnStartup();

    ++m_ticks;
    // Check file-based command surface every ~500ms (10 ticks at 50ms)
    if (m_ticks % 10 == 0)
        CheckCommandFile();

    if (m_ticks == 200 && sConfig.GetBoolDefault("TortoiseBots.PendingAddRemoveTest", false))
    {
        uint32 acct = sConfig.GetIntDefault("TortoiseBots.PendingAddRemoveTest.AccountId", 0);
        uint32 guidLow = sConfig.GetIntDefault("TortoiseBots.PendingAddRemoveTest.CharacterGuid", 0);
        if (acct && guidLow)
            BotManager::Instance().RunPendingAddRemoveTest(acct, ::ObjectGuid(HIGHGUID_PLAYER, guidLow));
    }

    // AutoTest is gated by `TortoiseBots.AutoTest` (default OFF). BUILD_PLAYERBOTS=ON
    // alone must never log a test character. Enable via `TortoiseBots.AutoTest = 1`
    // and `TortoiseBots.AutoTest.AccountId` / `CharacterGuid` in mangosd.conf.
    if (m_ticks == 200 && !BotManager::Instance().IsAutoTestEnabled())
    {
        extern bool sTortoiseBotsAutoTestAllowed();
        if (sTortoiseBotsAutoTestAllowed())
        {
            uint32 acct = sConfig.GetIntDefault("TortoiseBots.AutoTest.AccountId", 0);
            uint32 guidLow = sConfig.GetIntDefault("TortoiseBots.AutoTest.CharacterGuid", 0);
            if (guidLow == 0)
                return;
            ::ObjectGuid testGuid(HIGHGUID_PLAYER, guidLow);
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
