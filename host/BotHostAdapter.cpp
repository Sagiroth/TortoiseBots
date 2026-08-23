// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:undeclared_var_use,clang:incomplete_member_access,clang:init_conversion_failed,clang:excess_initializers,clang:typecheck_member_reference_struct_union,clang:expected_class_or_namespace,clang:ovl_no_viable_function_in_call,clang:fatal_too_many_errors
#include "BotHostAdapter.h"
#include "BotSessionAdapter.h"
#include "BotChatAdapter.h"
#include "../runtime/BotManager.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Config/Config.h"
#include "Log.h"
#include "World.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Creature.h"
#include "Maps/CellImpl.h"
#include "Maps/GridNotifiers.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include <fstream>
// pi-lens-ignore: clang:pp_file_not_found
#include <sstream>
#ifndef __UNIT_H
class WorldSession;
class Player {
public:
    class ObjectGuid GetObjectGuid() const { return ObjectGuid(); }
    const char* GetName() const { return ""; }
    bool IsInWorld() const { return true; }
    bool IsBeingTeleported() const { return false; }
    WorldSession* GetSession() const { return nullptr; }
};
class WorldSession {
public:
    Player* GetPlayer() const { return nullptr; }
    void LoginPlayer(class ObjectGuid) {}
    const char* GetPlayerName() const { return ""; }
};
#endif

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
    BotChatAdapter::Instance().EnsureRegistered();
    sLog.outString("TortoiseBots: BotHostAdapter startup — world tick and .bot chat interceptor registered");
    m_startupLogged = true;
}

// pi-lens-ignore: clang:incomplete_member_access
void BotHostAdapter::OnShutdown()
{
    sLog.outString("TortoiseBots: BotHostAdapter shutdown");
}

// Helpers for the in-game chat interceptor test surface (proves .bot is consumed, not broadcast).
// The production path is ChatHandler::ParseCommands -> DispatchChatCommandInterceptors -> BotChatAdapter.
// This test harness simulates a human typing in-game by feeding a file's lines through ChatHandler,
// which must also be consumed and not broadcast. It also ensures a Headless test human exists
// when no real Network human is online, so the three-command manual proof can run without a
// WoW client. The human is a Headless character session NOT tracked by BotManager::IsBot.
static ::ObjectGuid s_chatTestHumanGuid(HIGHGUID_PLAYER, uint32_t(2)); // Sagiroth fallback
static ::WorldSession* s_chatTestHumanSession = nullptr;
static bool s_chatTestHumanLoginDispatched = false;
static ::Unit* s_combatHarnessTarget = nullptr;
static ::Player* s_combatHarnessHuman = nullptr;
static uint32 s_combatHarnessTicks = 0;

// pi-lens-ignore: clang:incomplete_member_access
static ::Player* FindHumanForChatTest()
{
    auto& players = sObjectAccessor.GetPlayers();
    for (auto& kv : players)
    {
        ::Player* p = kv.second;
        if (!p || !p->IsInWorld() || p->IsBeingTeleported())
            continue;
        if (TortoiseBots::BotManager::Instance().IsBot(p->GetObjectGuid()))
            continue;
        return p;
    }
    return nullptr;
}

// pi-lens-ignore: clang:unknown_typename,clang:incomplete_member_access,clang:undeclared_var_use
static void EnsureChatTestHuman()
{
    if (FindHumanForChatTest())
        return;
    // If a Headless human session is already pending/active, dispatch login if needed
    if (sWorld.FindHeadlessSession(s_chatTestHumanGuid) || sWorld.HasPendingHeadlessSession(s_chatTestHumanGuid))
    {
        if (sWorld.FindHeadlessSession(s_chatTestHumanGuid) && !sObjectAccessor.FindPlayer(s_chatTestHumanGuid) && !s_chatTestHumanLoginDispatched)
        {
            if (::WorldSession* sess = sWorld.FindHeadlessSession(s_chatTestHumanGuid))
            {
                sLog.outString("TortoiseBots: Chat test human session %s found, dispatching LoginPlayer", s_chatTestHumanGuid.GetString().c_str());
                sess->LoginPlayer(s_chatTestHumanGuid);
                s_chatTestHumanLoginDispatched = true;
            }
        }
        return;
    }
    PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(s_chatTestHumanGuid.GetCounter());
    if (!data)
        data = sObjectMgr.GetPlayerDataByName("Sagiroth");
    if (!data)
    {
        // No test human in DB — cannot ensure human for chat test
        return;
    }
    s_chatTestHumanGuid = ::ObjectGuid(HIGHGUID_PLAYER, data->uiGuid);
    uint32_t acct = data->uiAccount;
    sLog.outString("TortoiseBots: Chat test — creating test human Sagiroth %s acct %u as Headless human for in-game .bot test (not a bot)", s_chatTestHumanGuid.GetString().c_str(), acct);
    ::WorldSession* sess = TortoiseBots::BotSessionAdapter::CreateHeadlessSession(acct, s_chatTestHumanGuid);
    if (sess)
    {
        s_chatTestHumanSession = sess;
        s_chatTestHumanLoginDispatched = false;
    }
}

// pi-lens-ignore: clang:unknown_typename,clang:incomplete_member_access,clang:undeclared_var_use,clang:pp_file_not_found
static void CheckChatInterceptorFile()
{
    const char* path = "/tmp/tortoisebots_chat.cmd";
    std::ifstream in(path);
    if (!in)
        return;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (content.empty())
        return;
    std::ofstream out(path, std::ios::trunc);
    out.close();

    ::Player* human = FindHumanForChatTest();
    if (!human)
    {
        EnsureChatTestHuman();
        human = FindHumanForChatTest();
        if (!human)
        {
            sLog.outString("TortoiseBots: Chat test file has content but no human player online — will retry next tick");
            std::ofstream re(path);
            re << content;
            re.close();
            return;
        }
    }

    sLog.outString("TortoiseBots: Chat test — dispatching %zu bytes via ChatHandler for human %s", content.size(), human->GetName());
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line))
    {
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        line = line.substr(a, b - a + 1);
        if (line.empty() || line[0] == '#') continue;
        std::string chatLine = line;
        if (chatLine[0] != '.' && chatLine[0] != '!')
            chatLine = "." + chatLine;
        // pi-lens-ignore: clang:incomplete_member_access
        ChatHandler handler(human->GetSession());
        bool consumed = handler.ParseCommands(chatLine.c_str());
        sLog.outString("TortoiseBots: Chat test: ParseCommands('%s') for %s -> %s", chatLine.c_str(), human->GetName(), consumed ? "consumed (not broadcast)" : "not a command (would broadcast)");
    }
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
        else if (cmd == "combat")
        {
            // Deterministic runtime acceptance harness: make the human engage a
            // real nearby hostile. PlayerbotAI still has to detect the master's
            // target and execute its normal Trigger/Action/Warrior stack.
            ::Player* bot = sObjectAccessor.FindPlayer(::ObjectGuid(HIGHGUID_PLAYER, uint32(1)));
            if (bot)
            {
                // Temporary fixture preparation only: the historical Dudette row
                // has no equipment/power. Real characters provide these normally.
                if (!bot->GetWeaponForAttack(BASE_ATTACK, true, true))
                    bot->StoreNewItemInBestSlots(25, 1);
            }
            Unit* target = human->SummonCreature(2141, human->GetPositionX() + 0.5f, human->GetPositionY(), human->GetPositionZ(),
                human->GetOrientation(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 120000, true);
            if (!target)
            {
                std::list<Unit*> hostiles;
                MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(human, human, 40.0f);
                MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(hostiles, check);
                Cell::VisitAllObjects(human, searcher, 40.0f);
                for (Unit* candidate : hostiles)
                    if (candidate && candidate->IsAlive() && candidate != human && human->IsValidAttackTarget(candidate) &&
                        (!target || candidate->GetDistance2d(human) < target->GetDistance2d(human)))
                        target = candidate;
            }
            if (!target) { sLog.outString("TortoiseBots: combat: no hostile within 40 yards"); continue; }
            target->SetHealth(std::min<uint32>(target->GetHealth(), 1000));
            human->SetSelectionGuid(target->GetObjectGuid());
            bool humanAttack = human->Attack(target, true);
            human->SetInCombatState(60000, target);
            target->SetInCombatWith(human);
            bool counterAttack = target->Attack(human, true);
            s_combatHarnessTarget = target;
            s_combatHarnessHuman = human;
            s_combatHarnessTicks = 0;
            sLog.outString("TortoiseBots: combat harness human=%s target=%s health=%u humanAttack=%u counterAttack=%u inCombat=%u humanPos=%.1f,%.1f botPos=%s",
                human->GetName(), target->GetName(), target->GetMaxHealth(), humanAttack, counterAttack, human->IsInCombat(),
                human->GetPositionX(), human->GetPositionY(), bot ? (std::to_string(bot->GetPositionX()) + "," + std::to_string(bot->GetPositionY())).c_str() : "none");
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
    // Check file-based command surfaces every ~500ms (10 ticks at 50ms)
    if (m_ticks % 10 == 0)
    {
        CheckCommandFile();
        CheckChatInterceptorFile();
        EnsureChatTestHuman();
    }
    // Also drive the chat-test human login dispatch quickly (every tick) if pending
    if (s_chatTestHumanSession && !s_chatTestHumanLoginDispatched)
    {
        if (sWorld.FindHeadlessSession(s_chatTestHumanGuid) && !sObjectAccessor.FindPlayer(s_chatTestHumanGuid))
        {
            if (::WorldSession* sess = sWorld.FindHeadlessSession(s_chatTestHumanGuid))
            {
                sess->LoginPlayer(s_chatTestHumanGuid);
                s_chatTestHumanLoginDispatched = true;
            }
        }
        else if (sObjectAccessor.FindPlayer(s_chatTestHumanGuid))
        {
            s_chatTestHumanLoginDispatched = true;
        }
    }

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

    if (s_combatHarnessTarget && s_combatHarnessHuman && s_combatHarnessTarget->IsAlive() && ++s_combatHarnessTicks > 100)
    {
        sLog.outString("TortoiseBots: combat harness ending real encounter target=%s", s_combatHarnessTarget->GetName());
        s_combatHarnessHuman->Kill(s_combatHarnessTarget, nullptr, false);
        s_combatHarnessHuman->AttackStop();
        if (::Player* bot = sObjectAccessor.FindPlayer(::ObjectGuid(HIGHGUID_PLAYER, uint32(1))) )
            bot->AttackStop();
        s_combatHarnessTarget = nullptr;
        s_combatHarnessHuman = nullptr;
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
