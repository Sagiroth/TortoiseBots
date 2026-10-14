#pragma once

// The one question the bot AI asks the remote commander (issue #884): is this
// bot in a group some commander has bound? The "master left the party, so
// leave" rule (ai/playerbot/strategy/actions/LeaveGroupAction.h) skips such a
// bot, because the bound group's instance bind would be lost with it. The
// answer is read from the binding table in commands/BotCommands.cpp (world
// thread only); the AI includes this header instead of the command layer.

#include <cstdint>
#include <string>

class Player;
class PlayerbotAI;

namespace TortoiseBots {
namespace BotCommands {

bool IsInCommandedGroup(Player* bot);

// A real player's chat command (whisper, `.bot command`, party chat) reached
// this bot and passed its authorization: a run leader (`.bot run`) of the
// bot's bound party pauses, so the player's order is not overridden by the
// run's next one (player control first). Does nothing while no run exists.
void NoteHumanChatOrder(Player* bot, std::string const& text);

// The "threat" back-off a binding gave a bot (RemoteCommander::
// CommanderThreatLedger): AiFactory and the strategy snapshot load note it,
// and the snapshot save leaves it out (it is the binding's, not the bot's).
void NoteCommanderThreat(Player* bot);
bool IsCommanderThreat(Player* bot);

// A bound bot moves only on the commander's orders (RemoteCommander::
// CommanderAutonomyLedger): the open-world kit (grind, travel, rpg...) comes
// off it while it is bound and goes back on release. The bind calls this, and
// so does every path that may give the kit back while the bot is bound
// (AiFactory's strategy rebuild, the snapshot load, the instance guard's
// restore). fresh: the strategies were just rebuilt from nothing. listHeld:
// entries a config list would have added but was filtered of for the bound
// bot (AiFactory::CommandedSafeNonCombatStrategies), noted for the release
// too. Does nothing for a bot outside a bound group.
void HoldCommanderAutonomy(Player* bot, PlayerbotAI* ai, bool fresh, uint32_t listHeld = 0);
// The entries a binding holds off the bot: the snapshot save still writes
// them as the bot's own (they are the bot's, not the binding's).
uint32_t CommanderAutonomyWithheld(Player* bot);
// A bot's map changed: a kit its release kept back inside a dungeon or raid
// comes back once it is on an open-world map and not bound again.
void CommanderAutonomyMapChange(Player* bot, PlayerbotAI* ai);

// The hire lifecycle's questions about a commander's binding (hires bound to
// their own hirer, issue #884). The hire sweep settles a commander's binding
// first, so a binding that lost its lead or its group is released - and its
// hires returned to the hirer - before any hire is judged.
bool HasCommanderBinding(uint64_t commanderRaw);
void SettleCommanderBinding(uint64_t commanderRaw);

} // namespace BotCommands
} // namespace TortoiseBots
