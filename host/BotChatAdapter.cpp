#include "BotChatAdapter.h"

#include "BotAddonAdapter.h"
#include "BotPacketAdapter.h"
#include "../commands/BotCommandQueue.h"
#include "../commands/BotCommands.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#include <cctype>
#include <string>
#include <typeinfo>

namespace TortoiseBots {

BotChatAdapter::BotChatAdapter()
    : AllCommandScript("tortoisebots_commands")
{
}

bool BotChatAdapter::CanExecuteCommand(ChatHandler* handler, char const* command, char const* args)
{
    if (!command)
        return true;

    std::string commandName(command);
    for (char& character : commandName)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    bool const isAutogear = commandName == "autogear";
    if (commandName != "bot" && !isAutogear)
        return true;

    // Returning false tells the generic AllCommandScript registry that this
    // command has been consumed; normal core command lookup must not see it.
    // Issue #833: a player's command is queued for the world thread (see
    // BotCommandQueue.h); the world hook drains it whether or not the AI is
    // enabled. The server console, remote access and `pending_commands`
    // (null-session CliHandlers) already run on the world thread and keep
    // running inline. A handler subclass with its own reply sink also stays
    // inline: the drain can only rebuild a plain ChatHandler.
    if (!handler)
        return false;
    WorldSession* session = handler->GetSession();
    Player* requester = session ? session->GetPlayer() : nullptr;
    if (!requester || typeid(*handler) != typeid(ChatHandler))
    {
        if (isAutogear)
            BotCommands::TryHandleBotCommand(handler, ("autogear " + std::string(args ? args : "")).c_str());
        else
            BotCommands::HandleChatCommand(handler, args ? args : "");
        return false;
    }

    // `autogear` carries its full text: the drain dispatches it through
    // TryHandleBotCommand, which owns the top-level `.autogear` form.
    std::string const queued = isAutogear ? "autogear " + std::string(args ? args : "") : std::string(args ? args : "");
    BotCommandQueue::Instance().Enqueue(requester->GetObjectGuid().GetRawValue(), BotCommandReply::Chat,
        queued, isAutogear);
    return false;
}

uint64_t BotChatAdapter::DrainQueuedCommands()
{
    BotCommandQueue::Instance().Drain(
        [](uint64 guid) -> Player*
        {
            // Not-in-world lookup: a player between maps still owns the
            // command, exactly as when it ran inline; a logged-out player is
            // gone from the accessor and the command is dropped.
            Player* player = ObjectAccessor::FindPlayerNotInWorld(ObjectGuid(guid));
            return player && player->GetSession() ? player : nullptr;
        },
        [](Player* requester, PendingBotCommand const& command)
        {
            // Issue #846: chat and packets queued for bots before this
            // command run first, so `/p stay` then `/p .bot action follow`
            // in one tick end in follow, as typed.
            BotPacketAdapter::DrainDeferredPackets(command.seq);
            if (command.reply == BotCommandReply::Addon)
            {
                BotAddonAdapter::ExecuteCommand(requester, command.args);
                return;
            }
            ChatHandler handler(requester->GetSession());
            if (command.topLevel)
                BotCommands::TryHandleBotCommand(&handler, command.args.c_str());
            else
                BotCommands::HandleChatCommand(&handler, command.args.c_str());
        },
        [](Player* requester, DroppedBotCommands const& dropped)
        {
            std::string const text = "Too many bot commands at once: " + std::to_string(dropped.count) +
                " ignored.";
            if (dropped.reply == BotCommandReply::Addon)
            {
                BotAddonAdapter::SendReply(requester, text);
                return;
            }
            ChatHandler(requester->GetSession()).SendSysMessage(text.c_str());
        },
        [](Player* requester, PendingBotCommand const& command)
        {
            // A bot still holding chat or packets between maps has not acked
            // the earlier order yet; run this command after it, on the next
            // drain, so the older `stay` cannot land on top of the newer
            // `follow`. Only items queued before the command count: a newer
            // held item ages out, and waiting on it would lose the command.
            return BotPacketAdapter::HasHeldPacketsBefore(requester->GetObjectGuid().GetRawValue(), command.seq);
        });
    return BotCommandQueue::Instance().DeferredRunLimit();
}

} // namespace TortoiseBots
