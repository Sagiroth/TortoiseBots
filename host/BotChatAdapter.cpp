#include "BotChatAdapter.h"

#include "../commands/BotCommands.h"

#include <cctype>
#include <string>

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

    if (commandName != "bot")
        return true;

    // Returning false tells the generic AllCommandScript registry that this
    // command has been consumed; normal core command lookup must not see it.
    BotCommands::HandleChatCommand(handler, args ? args : "");
    return false;
}

} // namespace TortoiseBots
