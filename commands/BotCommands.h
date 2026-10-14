#pragma once

#include <cstdint>
#include <vector>

#include "../runtime/CommandedGroupQuery.h" // IsInCommandedGroup
#include "../runtime/CommanderTelemetry.h"

class ChatHandler;
class Player;

namespace TortoiseBots {
namespace BotCommands {

// Returns true if args was handled as a bot command (even on error), false if not a bot command.
// Thin handler: parses args and delegates to BotManager/mature PlayerbotAI
// (no movement logic here).
bool HandleChatCommand(ChatHandler* handler, char const* args);

// Helper for core Chat.cpp hook — checks if text starts with "bot" and dispatches.
bool TryHandleBotCommand(ChatHandler* handler, char const* text);

// Issue #884: the remote commander's binding sweep. World thread, every tick
// (BotHostAdapter::OnUpdate); does nothing while no commander is bound and
// checks the bindings once a second otherwise (commander logout, disband,
// lead change); phase 2 sends each bound commander whose view is on its
// TBM:VIEW frames from here (runtime/RemoteCommanderView.h). Clear forgets
// every binding at shutdown.
void UpdateRemoteCommanders(uint32_t diff);
void ClearRemoteCommanders();
// The standing bindings for the observability dashboard's Commanders tab:
// commander, lead bot, bound bots and the commander's last orders. World
// thread; read-only (nothing is settled or released); empty while none stands.
void CollectCommanderTelemetry(std::vector<CommanderTelemetry::BindingRef>& out);
// True when bot is in the group the commander is bound to: master-relative
// conveniences (the summon out of a dungeon when the master leaves it) leave
// such a bot with its lead bot.
bool IsCommandedBy(Player* commander, Player* bot);

} // namespace BotCommands
} // namespace TortoiseBots
