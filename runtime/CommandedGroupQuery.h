#pragma once

// The one question the bot AI asks the remote commander (issue #884): is this
// bot in a group some commander has bound? The "master left the party, so
// leave" rule (ai/playerbot/strategy/actions/LeaveGroupAction.h) skips such a
// bot, because the bound group's instance bind would be lost with it. The
// answer is read from the binding table in commands/BotCommands.cpp (world
// thread only); the AI includes this header instead of the command layer.

#include <cstdint>

class Player;

namespace TortoiseBots {
namespace BotCommands {

bool IsInCommandedGroup(Player* bot);

// The "threat" back-off a binding gave a bot (RemoteCommander::
// CommanderThreatLedger): AiFactory and the strategy snapshot load note it,
// and the snapshot save leaves it out (it is the binding's, not the bot's).
void NoteCommanderThreat(Player* bot);
bool IsCommanderThreat(Player* bot);

// The hire lifecycle's questions about a commander's binding (hires bound to
// their own hirer, issue #884). The hire sweep settles a commander's binding
// first, so a binding that lost its lead or its group is released - and its
// hires returned to the hirer - before any hire is judged.
bool HasCommanderBinding(uint64_t commanderRaw);
void SettleCommanderBinding(uint64_t commanderRaw);

} // namespace BotCommands
} // namespace TortoiseBots
