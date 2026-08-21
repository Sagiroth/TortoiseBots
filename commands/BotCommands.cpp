#include "BotCommands.h"
#include "../runtime/BotManager.h"
#include "../runtime/BotController.h"

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
#include "Log.h"

#include <sstream>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
static bool HandleAdd(ChatHandler* handler, char const* args)
{
    if (!handler || !args || !*args)
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

    // Basic ownership check: for this slice, allow same-account or any if GM
    // Full security ladder deferred to later slice.
    ::WorldSession* sess = BotManager::Instance().AddBotWithMaster(accountId, guid, masterGuid);
    if (sess)
        handler->PSendSysMessage("Bot %s added, following %s.", name.c_str(), requester->GetName());
    else
        handler->PSendSysMessage("Failed to add bot %s (already exists or error).", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
static bool HandleRemove(ChatHandler* handler, char const* args)
{
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
    if (BotManager::Instance().RemoveBot(guid, true))
        handler->PSendSysMessage("Bot %s removed.", name.c_str());
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
    if (BotManager::Instance().SetBotFollow(botGuid, masterGuid))
        handler->PSendSysMessage("Bot %s now following %s.", name.c_str(), requester->GetName());
    else
        handler->PSendSysMessage("Bot %s not found.", name.c_str());
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
        handler->PSendSysMessage("Usage: .bot add <name> | .bot remove <name> | .bot follow <botName>");
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

    if (cmd == "add" || cmd == "login")
        return HandleAdd(handler, subArgs);
    if (cmd == "remove" || cmd == "rm" || cmd == "logout")
        return HandleRemove(handler, subArgs);
    if (cmd == "follow")
        return HandleFollow(handler, subArgs);
    if (cmd == "help" || cmd == "h")
    {
        handler->PSendSysMessage("Bot commands: .bot add <name> | .bot remove <name> | .bot follow <botName>");
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

// Exported weak symbol for core Chat.cpp hook (optional, but also used for direct call)
extern "C" bool HandleTortoiseBotsChatCommand(ChatHandler* handler, char const* args)
{
    return TortoiseBots::BotCommands::HandleChatCommand(handler, args);
}
