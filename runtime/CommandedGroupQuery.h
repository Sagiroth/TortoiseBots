#pragma once

// The one question the bot AI asks the remote commander (issue #884): is this
// bot in a group some commander has bound? The "master left the party, so
// leave" rule (ai/playerbot/strategy/actions/LeaveGroupAction.h) skips such a
// bot, because the bound group's instance bind would be lost with it. The
// answer is read from the binding table in commands/BotCommands.cpp (world
// thread only); the AI includes this header instead of the command layer.

class Player;

namespace TortoiseBots {
namespace BotCommands {

bool IsInCommandedGroup(Player* bot);

// The "threat" back-off a binding gave a bot (RemoteCommander::
// CommanderThreatLedger): AiFactory and the strategy snapshot load note it,
// and the snapshot save leaves it out (it is the binding's, not the bot's).
void NoteCommanderThreat(Player* bot);
bool IsCommanderThreat(Player* bot);

} // namespace BotCommands
} // namespace TortoiseBots
