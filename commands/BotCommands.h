#pragma once

class ChatHandler;

namespace TortoiseBots {
namespace BotCommands {

// Returns true if args was handled as a bot command (even on error), false if not a bot command.
// Thin handler: parses args and delegates to BotManager/mature PlayerbotAI
// (no movement logic here).
bool HandleChatCommand(ChatHandler* handler, char const* args);

// Helper for core Chat.cpp hook — checks if text starts with "bot" and dispatches.
bool TryHandleBotCommand(ChatHandler* handler, char const* text);

} // namespace BotCommands
} // namespace TortoiseBots
