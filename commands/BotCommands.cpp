// pi-lens-ignore-file: all
#include "BotCommands.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../behavior/PlayerConvenience.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/PlayerbotAIStorage.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotAI.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldPacket.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
#include <cctype>
#include <cstring>
#include <string>
// Lens fallback stubs — core headers absent in static analysis. Real build uses
// the mangosd headers via -I src/game. Guards use the same macros as the
// real headers so the stubs are never active in a real build.
#ifndef MANGOSSERVER_CHAT_H
class WorldSession;
class ChatHandler {
public:
    void PSendSysMessage(const char* fmt, ...) {}
    void SendSysMessage(const char*) {}
    WorldSession* GetSession() const { return nullptr; }
};
#endif
#ifndef _OBJECTMGR_H
struct PlayerCacheData { uint32_t uiGuid; uint32_t uiAccount; };
struct LensOM { PlayerCacheData* GetPlayerDataByName(const std::string&) { return nullptr; } PlayerCacheData* GetPlayerDataByGUID(uint32_t) { return nullptr; } Player* GetPlayer(const char*) { return nullptr; } };
static LensOM sObjectMgr;
inline bool normalizePlayerName(std::string&, size_t = 32, bool = true) { return true; }
#endif
#ifndef MANGOS_OBJECTACCESSOR_H
class Player;
struct LensOA { Player* FindPlayerByName(const char*) { return nullptr; } Player* FindPlayer(ObjectGuid) { return nullptr; } };
static LensOA sObjectAccessor;
#endif
#ifndef __UNIT_H
using uint32 = uint32_t;
constexpr int SEC_GAMEMASTER = 3;
#ifndef OBJECT_GUID_H
struct ObjectGuid { uint32 GetCounter() const { return 0; } };
#endif
#ifndef HIGHGUID_PLAYER
constexpr int HIGHGUID_PLAYER = 0;
#endif
constexpr int CHAT_MSG_WHISPER = 1;
class WorldPacket { public: WorldPacket() {} template<typename T> WorldPacket& operator<<(T const&) { return *this; } };
class Player {
public:
    ObjectGuid GetObjectGuid() const { return ObjectGuid(); }
    const char* GetName() const { return ""; }
    WorldSession* GetSession() const { return nullptr; }
    void* GetGroupInvite() const { return nullptr; }
    bool IsInSameGroupWith(Player*) const { return false; }
};
class WorldSession {
public:
    Player* GetPlayer() const { return nullptr; }
    int GetSecurity() const { return 0; }
    uint32 GetAccountId() const { return 0; }
    bool IsHeadless() const { return false; }
    void HandleGroupInviteOpcode(WorldPacket&) {}
    void HandleGroupUninviteOpcode(WorldPacket&) {}
};
#endif
#ifndef MANGOSSERVER_LOG_H
struct Log { void outString(const char*, ...) {} void outError(const char*, ...) {} };
static Log sLog;
#endif

namespace TortoiseBots {
namespace BotCommands {

static std::string Trim(std::string value)
{
    size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

static Player* Requester(ChatHandler* handler)
{
    return handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
}

static bool CanControl(Player* requester, BotRecord const* record)
{
    if (!requester || !requester->GetSession() || !record)
        return false;
    return requester->GetSession()->GetSecurity() >= SEC_GAMEMASTER ||
        record->accountId == requester->GetSession()->GetAccountId();
}

static bool ResolveOwnedBot(ChatHandler* handler, char const* args, Player*& bot, BotRecord*& record, std::string& name)
{
    bot = nullptr;
    record = nullptr;
    name = Trim(args ? args : "");
    if (name.empty() || !normalizePlayerName(name))
        return false;

    bot = sObjectMgr.GetPlayer(name.c_str());
    if (!bot)
        bot = sObjectAccessor.FindPlayerByName(name.c_str());
    if (!bot)
        return false;

    record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!CanControl(Requester(handler), record))
        return false;

    return bot->GetSession() && bot->GetSession()->IsHeadless() &&
        BotManager::Instance().IsBot(bot->GetObjectGuid());
}

static bool HandleList(ChatHandler* handler)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    uint32 shown = 0;
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        BotRecord* record = bot ? BotManager::Instance().FindBot(bot->GetObjectGuid()) : nullptr;
        if (!bot || !record || !CanControl(requester, record))
            continue;

        ++shown;
        handler->PSendSysMessage("%s: %s, random %u, AI %u",
            bot->GetName(), record->enteredWorld ? "in world" : "starting",
            record->random ? 1 : 0,
            PlayerbotAIStorage::Instance().GetAI(bot) ? 1 : 0);
    }

    if (!shown)
        handler->PSendSysMessage("No owned PlayerBots are online.");
    return true;
}

static bool HandleStats(ChatHandler* handler)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    uint32 total = 0;
    uint32 random = 0;
    uint32 withAi = 0;
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        BotRecord* record = bot ? BotManager::Instance().FindBot(bot->GetObjectGuid()) : nullptr;
        if (!bot || !record || !CanControl(requester, record))
            continue;
        ++total;
        random += record->random ? 1 : 0;
        withAi += PlayerbotAIStorage::Instance().GetAI(bot) ? 1 : 0;
    }
    handler->PSendSysMessage("Owned PlayerBots: %u online, %u random, %u with AI.", total, random, withAi);
    return true;
}

static char const* LifecycleName(BotLifecycle lifecycle)
{
    switch (lifecycle)
    {
        case BotLifecycle::PendingAdd: return "starting";
        case BotLifecycle::InWorld: return "in world";
        case BotLifecycle::Removing: return "removing";
    }

    return "unknown";
}

static char const* MovementName(PlayerbotAI* ai)
{
    if (!ai)
        return "unavailable";
    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT))
        return "follow";
    if (ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
        return "stay";
    if (ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
        return "guard";
    if (ai->HasStrategy("free", BotState::BOT_STATE_NON_COMBAT))
        return "free";
    return "custom";
}

static bool HandleStatus(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot status <online bot name> (same account only)");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    handler->PSendSysMessage("%s: %s, AI %u, movement %s, random %u, owner %s.",
        name.c_str(), LifecycleName(record->lifecycle), ai ? 1u : 0u,
        MovementName(ai), record->random ? 1u : 0u,
        record->masterGuid == requester->GetObjectGuid() ? "you" : "another player");
    return true;
}

static bool HandleInvite(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot invite <online bot name> (same account only)");
        return true;
    }
    if (bot == requester || !requester->GetSession())
    {
        handler->PSendSysMessage("That character cannot be invited as your bot.");
        return true;
    }

    // Let the native group handler create the invite. This emits the real
    // SMSG_GROUP_INVITE, which the module packet bridge feeds to PlayerbotAI.
    auto* previousInvite = bot->GetGroupInvite();
    WorldPacket packet;
    packet << bot->GetName() << uint32(0);
    requester->GetSession()->HandleGroupInviteOpcode(packet);
    if (bot->GetGroupInvite() == previousInvite)
    {
        handler->PSendSysMessage("The group invitation for %s was rejected by the native group handler.", name.c_str());
        return true;
    }

    handler->PSendSysMessage("Invitation sent to bot %s; it may accept it asynchronously.", name.c_str());
    return true;
}

static bool HandleUninvite(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot uninvite <online bot name> (same account only)");
        return true;
    }

    WorldPacket packet;
    packet << bot->GetName();
    requester->GetSession()->HandleGroupUninviteOpcode(packet);
    handler->PSendSysMessage("Uninvite sent for bot %s.", name.c_str());
    return true;
}

static bool HandleStay(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot stay <online bot name> (same account only)");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", name.c_str());
        return true;
    }

    // Reuse the stay action so the command updates both strategies and the
    // current "return"/"stay" position anchors.
    ai::Event stayEvent("stay", "", requester);
    if (!ai->DoSpecificAction("stay chat shortcut", stayEvent, true))
    {
        handler->PSendSysMessage("Bot %s could not enter stay mode.", name.c_str());
        return true;
    }

    handler->PSendSysMessage("Bot %s will stay.", name.c_str());
    return true;
}

static bool HandleNamedAction(ChatHandler* handler, char const* args, char const* command,
    char const* action, char const* confirmation)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot %s <online bot name> (same account only)", command);
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", name.c_str());
        return true;
    }

    ai::Event event(command, "", requester);
    if (!ai->DoSpecificAction(action, event, true))
    {
        handler->PSendSysMessage("Bot %s could not execute %s.", name.c_str(), command);
        return true;
    }

    handler->PSendSysMessage("Bot %s %s.", name.c_str(), confirmation);
    return true;
}

static bool HandleGuard(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "guard", "guard chat shortcut", "will guard this position");
}

static bool HandleFree(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "free", "free chat shortcut", "is free to move");
}

static bool HandleReady(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "ready", "ready check", "will run a readiness check");
}

static bool HandleAttack(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "attack", "attack my target", "will attack your selected target");
}

static bool IsPublicFormation(std::string const& formation)
{
    return formation == "default" || formation == "melee" || formation == "queue" ||
        formation == "chaos" || formation == "circle" || formation == "line" ||
        formation == "shield" || formation == "arrow" || formation == "near" ||
        formation == "far";
}

static bool HandleFormation(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::string input = Trim(args ? args : "");
    size_t separator = input.find_first_of(" \t");
    if (!requester || separator == std::string::npos)
    {
        handler->PSendSysMessage("Usage: .bot formation <bot name> <default|melee|queue|chaos|circle|line|shield|arrow|near|far>");
        return true;
    }

    std::string botName = input.substr(0, separator);
    std::string formation = Trim(input.substr(separator + 1));
    for (char& character : formation)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    if (!IsPublicFormation(formation))
    {
        handler->PSendSysMessage("Unknown formation. Use default, melee, queue, chaos, circle, line, shield, arrow, near, or far.");
        return true;
    }

    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!ResolveOwnedBot(handler, botName.c_str(), bot, record, name))
    {
        handler->PSendSysMessage("You may only control an online bot on your account.");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", name.c_str());
        return true;
    }

    ai::Event event("formation", formation, requester);
    if (!ai->DoSpecificAction("formation", event, true))
    {
        handler->PSendSysMessage("Bot %s could not set formation %s.", name.c_str(), formation.c_str());
        return true;
    }

    handler->PSendSysMessage("Bot %s formation set to %s.", name.c_str(), formation.c_str());
    return true;
}

static bool HandleMatureCommand(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::string input = Trim(args ? args : "");
    size_t separator = input.find_first_of(" \t");
    if (!requester || separator == std::string::npos)
    {
        handler->PSendSysMessage("Usage: .bot command <botName> <Playerbot command>");
        return true;
    }

    std::string botName = input.substr(0, separator);
    std::string command = Trim(input.substr(separator + 1));
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string resolvedName;
    if (command.empty() || !ResolveOwnedBot(handler, botName.c_str(), bot, record, resolvedName))
    {
        handler->PSendSysMessage("You may only command an online bot on your account.");
        return true;
    }

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        ai->HandleCommand(CHAT_MSG_WHISPER, command, *requester);
        handler->PSendSysMessage("Forwarded command for %s: %s; action success is not guaranteed.", resolvedName.c_str(), command.c_str());
    }
    else
        handler->PSendSysMessage("Bot %s has no AI yet.", resolvedName.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
static bool HandleAdd(ChatHandler* handler, char const* args)
{
    if (!handler)
        return false;
    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot add <characterName>");
        return true;
    }

    std::string name = args;
    // Trim
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);

    if (name.empty())
    {
        handler->PSendSysMessage("Usage: .bot add <characterName>");
        return true;
    }

    // Normalize name
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid character name.");
        return true;
    }

    ::Player* requester = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to add a bot.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        // Try online lookup
        if (::Player* online = sObjectAccessor.FindPlayerByName(name.c_str()))
            data = sObjectMgr.GetPlayerDataByGUID(online->GetObjectGuid().GetCounter());
    }

    if (!data)
    {
        handler->PSendSysMessage("Character '%s' not found.", name.c_str());
        return true;
    }

    ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
    uint32_t accountId = data->uiAccount;
    ::ObjectGuid masterGuid = requester->GetObjectGuid();

    if (sObjectAccessor.FindPlayer(guid))
    {
        handler->PSendSysMessage("Character '%s' is already online and cannot be claimed as a Headless bot.", name.c_str());
        return true;
    }

    if (!requester->GetSession() ||
        (accountId != requester->GetSession()->GetAccountId() && requester->GetSession()->GetSecurity() < SEC_GAMEMASTER))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }

    if (BotManager::Instance().AddBotWithMaster(accountId, guid, masterGuid))
        handler->PSendSysMessage("Bot %s queued for login; it will follow %s after entering the world.",
            name.c_str(), requester->GetName());
    else
        handler->PSendSysMessage("Failed to add bot %s (already exists or error).", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
static bool HandleRemove(ChatHandler* handler, char const* args)
{
    if (!handler)
        return false;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to remove a bot.");
        return true;
    }
    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot remove <characterName>");
        return true;
    }
    std::string name = args;
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid character name.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        handler->PSendSysMessage("Character '%s' not found.", name.c_str());
        return true;
    }
    ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
    BotRecord* record = BotManager::Instance().FindBot(guid);
    if (!record)
    {
        handler->PSendSysMessage("Character '%s' is not a module-owned bot.", name.c_str());
        return true;
    }
    if (!CanControl(requester, record))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }
    if (BotManager::Instance().RemoveBot(guid, true))
        handler->PSendSysMessage("Removal requested for bot %s; Headless cleanup completes asynchronously.", name.c_str());
    else
        handler->PSendSysMessage("Bot %s not found or not removable.", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
static bool HandleFollow(ChatHandler* handler, char const* args)
{
    ::Player* requester = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot follow <botName>");
        return true;
    }
    std::string name = args;
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid bot name.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        handler->PSendSysMessage("Bot '%s' not found.", name.c_str());
        return true;
    }
    ::ObjectGuid botGuid(HIGHGUID_PLAYER, data->uiGuid);
    ::ObjectGuid masterGuid = requester->GetObjectGuid();
    BotRecord* record = BotManager::Instance().FindBot(botGuid);
    if (!record || (requester->GetSession()->GetSecurity() < SEC_GAMEMASTER &&
        record->accountId != requester->GetSession()->GetAccountId()))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }
    if (BotManager::Instance().SetBotFollow(botGuid, masterGuid))
        handler->PSendSysMessage("Bot %s now following %s.", name.c_str(), requester->GetName());
    else
        handler->PSendSysMessage("Bot %s could not enter follow mode; no success is reported.", name.c_str());
    return true;
}
// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename
#include "Group/Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/Map.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Objects/Unit.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Objects/Creature.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/strategy/generic/PullStrategy.h"

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
static bool HandlePullback(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    if (!requester->IsInWorld() || requester->IsBeingTeleported())
    {
        handler->PSendSysMessage("You must be in world to use pullback.");
        return true;
    }
    if (!requester->IsAlive() || requester->IsTaxiFlying())
    {
        handler->PSendSysMessage("You must be alive and not on a taxi to use pullback.");
        return true;
    }
    ObjectGuid sel = requester->GetSelectionGuid();
    if (sel.IsEmpty())
    {
        handler->PSendSysMessage("You have no target. Select an enemy first.");
        return true;
    }
    Unit* target = nullptr;
    if (sel.IsPlayer())
        target = sObjectAccessor.FindPlayer(sel);
    else
    {
        Map* map = requester->GetMap();
        if (map)
        {
            // Mangos map has GetCreature / GetUnit variants
            // Try common APIs
            if (Creature* c = map->GetCreature(sel))
                target = c;
            else if (Unit* u = map->GetUnit(sel))
                target = u;
        }
        if (!target)
        {
            // Fallback via AI lookup using any bot AI
            for (Player* bot : BotManager::Instance().GetAllBots())
            {
                if (PlayerbotAI* aiTmp = PlayerbotAIStorage::Instance().GetAI(bot))
                {
                    if (Unit* u = aiTmp->GetUnit(sel))
                    {
                        target = u;
                        break;
                    }
                }
            }
        }
        // Last fallback: try to find creature via sObjectAccessor global
        if (!target)
        {
            // sObjectAccessor.FindCreature not always available; try player accessor for creature counter
            // Keep as is - will report not found
        }
    }
    if (!target)
    {
        handler->PSendSysMessage("Target not found (must be in same map and visible).");
        return true;
    }
    if (!target->IsAlive())
    {
        handler->PSendSysMessage("Target is dead.");
        return true;
    }
    if (target->IsInCombat())
    {
        handler->PSendSysMessage("Target is already in combat.");
        return true;
    }
    if (!requester->IsHostileTo(target) && !target->IsHostileTo(requester))
    {
        // Allow if hostile to bot at least; but for POC require hostile to requester
        // Keep permissive: check either
        bool hostileToAnyBot = false;
        for (Player* bot : BotManager::Instance().GetBotsForMaster(requester->GetObjectGuid()))
            if (bot->IsHostileTo(target)) { hostileToAnyBot = true; break; }
        if (!hostileToAnyBot && !requester->IsHostileTo(target))
        {
            handler->PSendSysMessage("Target is not hostile.");
            return true;
        }
    }
    // Find tank bot in party (assume 1 tank)
    std::vector<Player*> candidates;
    Group* group = requester->GetGroup();
    if (group)
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == requester) continue;
            if (!member->IsInWorld() || !member->IsAlive() || member->GetMap() != requester->GetMap()) continue;
            ObjectGuid mg = member->GetObjectGuid();
            if (!BotManager::Instance().IsBot(mg)) continue;
            BotRecord* rec = BotManager::Instance().FindBot(mg);
            if (!rec || !CanControl(requester, rec)) continue;
            if (member->IsInCombat()) continue;
            if (PlayerConvenience::Instance().IsBusy(mg)) continue;
            if (!PlayerbotAI::IsTank(member, true)) continue;
            candidates.push_back(member);
        }
    }
    // Fallback: owned bots if not in group
    if (candidates.empty() && !group)
    {
        for (Player* bot : BotManager::Instance().GetBotsForMaster(requester->GetObjectGuid()))
        {
            if (!bot->IsInWorld() || !bot->IsAlive() || bot->GetMap() != requester->GetMap()) continue;
            if (bot->IsInCombat()) continue;
            if (PlayerConvenience::Instance().IsBusy(bot->GetObjectGuid())) continue;
            if (!PlayerbotAI::IsTank(bot, true)) continue;
            candidates.push_back(bot);
        }
    }
    if (candidates.empty())
    {
        handler->PSendSysMessage("No tank bot found in your party (needs a bot with tank role).");
        return true;
    }
    Player* tank = candidates[0];
    if (candidates.size() > 1)
        handler->PSendSysMessage("Multiple tank bots found, using %s.", tank->GetName());
    // Check tank not in combat and distance 100y from tank
    if (tank->IsInCombat())
    {
        handler->PSendSysMessage("Tank %s is already in combat.", tank->GetName());
        return true;
    }
    float distTankToTarget = tank->GetDistance(target);
    if (distTankToTarget > 100.0f)
    {
        handler->PSendSysMessage("Target too far from tank %s (%.1fy > 100y).", tank->GetName(), distTankToTarget);
        return true;
    }
    // Also check anchor map match
    if (tank->GetMapId() != requester->GetMapId())
    {
        handler->PSendSysMessage("Tank and you are on different maps.");
        return true;
    }
    // Decide ranged vs melee
    bool isRanged = false;
    float desiredDist = 14.0f;
    PlayerbotAI* tankAI = PlayerbotAIStorage::Instance().GetAI(tank);
    if (tankAI)
    {
        // Try PullStrategy path for proper weapon/range
        if (PullStrategy* strat = PullStrategy::Get(tankAI))
        {
            if (strat->CanDoPullAction(target))
            {
                isRanged = true;
                float r = strat->GetRange();
                if (r > 5.0f) desiredDist = r * 0.75f;
                else desiredDist = tankAI->GetRange("shoot") * 0.75f;
                if (desiredDist < 10.0f) desiredDist = 25.0f;
            }
            else
            {
                // Check generic shoot availability as fallback
                const char* tries[] = {"shoot", "shoot bow", "shoot gun", "shoot crossbow", "throw", nullptr};
                for (int i = 0; tries[i]; ++i)
                    if (tankAI->CanCastSpell(tries[i], target, true, nullptr, true))
                    { isRanged = true; desiredDist = tankAI->GetRange("shoot") > 5 ? tankAI->GetRange("shoot")*0.75f : 26.0f; break; }
            }
        }
        else
        {
            const char* tries[] = {"shoot", "shoot bow", "shoot gun", "shoot crossbow", "throw", nullptr};
            for (int i = 0; tries[i]; ++i)
                if (tankAI->CanCastSpell(tries[i], target, true, nullptr, true))
                { isRanged = true; desiredDist = tankAI->GetRange("shoot") > 5 ? tankAI->GetRange("shoot")*0.75f : 26.0f; break; }
        }
    }
    if (!isRanged) desiredDist = 12.0f; // body pull inside aggroDistance 22

    if (!PlayerConvenience::Instance().RequestPullback(requester, tank, target, isRanged, desiredDist))
    {
        handler->PSendSysMessage("Pullback already active for %s or failed to queue.", tank->GetName());
        return true;
    }
    handler->PSendSysMessage("Pullback: tank %s %s -> %s (%.1fy, %s) anchor %.0f,%.0f",
        tank->GetName(), isRanged ? "shoot" : "body", target->GetName(), distTankToTarget, isRanged ? "ranged" : "melee",
        requester->getPositionX(), requester->getPositionY());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
static bool HandleSummon(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    if (!requester->IsInWorld() || requester->IsBeingTeleported())
    {
        handler->PSendSysMessage("You must be in world and not teleporting.");
        return true;
    }
    if (!requester->IsAlive())
    {
        handler->PSendSysMessage("You must be alive to summon.");
        return true;
    }
    if (requester->IsTaxiFlying())
    {
        handler->PSendSysMessage("You cannot summon while on a taxi.");
        return true;
    }
    std::string name = Trim(args ? args : "");
    if (name.empty() || !normalizePlayerName(name))
    {
        handler->PSendSysMessage("Usage: .bot summon <online bot name> (same account only)");
        return true;
    }
    Player* bot = sObjectMgr.GetPlayer(name.c_str());
    if (!bot) bot = sObjectAccessor.FindPlayerByName(name.c_str());
    if (!bot)
    {
        handler->PSendSysMessage("Bot '%s' not found or not online.", name.c_str());
        return true;
    }
    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record || !CanControl(requester, record))
    {
        handler->PSendSysMessage("You may only summon a bot on your account.");
        return true;
    }
    if (!bot->GetSession() || !bot->GetSession()->IsHeadless() || !BotManager::Instance().IsBot(bot->GetObjectGuid()))
    {
        handler->PSendSysMessage("Character '%s' is not a module-owned Headless bot.", name.c_str());
        return true;
    }
    if (!bot->IsInWorld())
    {
        handler->PSendSysMessage("Bot '%s' is not in world.", name.c_str());
        return true;
    }
    if (!bot->IsAlive())
    {
        handler->PSendSysMessage("Bot '%s' is dead and cannot be summoned (resurrect first).", name.c_str());
        return true;
    }
    if (bot->IsBeingTeleported())
    {
        handler->PSendSysMessage("Bot '%s' is already teleporting.", name.c_str());
        return true;
    }
    if (bot->IsTaxiFlying())
    {
        handler->PSendSysMessage("Bot '%s' is on a taxi.", name.c_str());
        return true;
    }
    if (bot->IsInCombat())
    {
        handler->PSendSysMessage("Bot '%s' is in combat and cannot be summoned.", name.c_str());
        return true;
    }
    if (requester->GetMap() && bot->GetMap() && requester->GetMap() != bot->GetMap())
    {
        // Cross-map summon is allowed via TeleportTo, just warn
        handler->PSendSysMessage("Bot '%s' is on a different map; summon will teleport across maps.", name.c_str());
    }
    if (PlayerConvenience::Instance().IsBusy(bot->GetObjectGuid()))
    {
        handler->PSendSysMessage("Bot '%s' already has a pending convenience action.", name.c_str());
        return true;
    }
    // Optional: check instance restrictions - for POC allow everywhere, but block if in BG/Arena?
    // Allow summon in dungeons/raids; if bot is in a different instance, TeleportTo will move it.
    if (!PlayerConvenience::Instance().RequestSummon(requester, bot))
    {
        handler->PSendSysMessage("Failed to summon bot '%s' (already pending or error).", name.c_str());
        return true;
    }
    handler->PSendSysMessage("Summoning %s to your location (5y) via portal (3s) and will follow.", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
bool HandleChatCommand(ChatHandler* handler, char const* args)
{
    if (!handler || !args)
        return false;

    // Trim leading spaces
    while (*args == ' ' || *args == '\t') ++args;
    if (!*args)
    {
        handler->PSendSysMessage("Usage: .bot add/remove/follow/invite/uninvite/stay/guard/free/ready/attack/formation/list/stats/status/pullback/summon/command");
        return true;
    }

    // Extract subcommand
    std::string cmd;
    const char* p = args;
    while (*p && *p != ' ' && *p != '\t')
    {
        cmd += *p;
        ++p;
    }
    while (*p == ' ' || *p == '\t') ++p;
    const char* subArgs = p;

    // Normalize cmd to lowercase
    for (char& c : cmd) c = tolower(c);

    if (cmd == "add")
        return HandleAdd(handler, subArgs);
    if (cmd == "remove")
        return HandleRemove(handler, subArgs);
    if (cmd == "follow")
        return HandleFollow(handler, subArgs);
    if (cmd == "invite")
        return HandleInvite(handler, subArgs);
    if (cmd == "uninvite")
        return HandleUninvite(handler, subArgs);
    if (cmd == "stay")
        return HandleStay(handler, subArgs);
    if (cmd == "guard")
        return HandleGuard(handler, subArgs);
    if (cmd == "free")
        return HandleFree(handler, subArgs);
    if (cmd == "ready")
        return HandleReady(handler, subArgs);
    if (cmd == "attack")
        return HandleAttack(handler, subArgs);
    if (cmd == "formation")
        return HandleFormation(handler, subArgs);
    if (cmd == "list")
        return HandleList(handler);
    if (cmd == "stats")
        return HandleStats(handler);
    if (cmd == "status")
        return HandleStatus(handler, subArgs);
    if (cmd == "pullback" || cmd == "pull-back")
        return HandlePullback(handler, subArgs);
    if (cmd == "summon")
        return HandleSummon(handler, subArgs);
    if (cmd == "command")
        return HandleMatureCommand(handler, subArgs);
    if (cmd == "help" || cmd == "h")
    {
        handler->PSendSysMessage("Bot commands: add/remove/follow/invite/uninvite/stay/guard/free/ready/attack/formation/list/stats/status/pullback/summon/command");
        return true;
    }

    handler->PSendSysMessage("Unknown bot command '%s'. Try .bot help", cmd.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access
bool TryHandleBotCommand(ChatHandler* handler, char const* text)
{
    if (!handler || !text)
        return false;
    // text is the full command after '.' — e.g., "bot add Dudette"
    // We only handle "bot ..." here; return false to let normal handler continue.
    if (strncmp(text, "bot", 3) != 0)
        return false;
    // Ensure "bot" is followed by space or end
    if (text[3] != '\0' && text[3] != ' ' && text[3] != '\t')
        return false;
    const char* args = text + 3;
    while (*args == ' ' || *args == '\t') ++args;
    return HandleChatCommand(handler, args);
}

} // namespace BotCommands
} // namespace TortoiseBots
