#pragma once

// The one question the bot AI asks the remote commander (issue #884): is this
// bot in a group some commander has bound? The "master left the party, so
// leave" rule (ai/playerbot/strategy/actions/LeaveGroupAction.h) skips such a
// bot, because the bound group's instance bind would be lost with it. The
// answer is read from the binding table in commands/BotCommands.cpp (world
// thread only); the AI includes this header instead of the command layer.

#include <cstdint>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;

namespace TortoiseBots {
namespace BotCommands {

bool IsInCommandedGroup(Player* bot);

// The bot's bound party has a running run leader (`.bot run`): its crowd
// control then takes only the run's ordered mark, never a free pick
// (CcTargetValue). Cheap: a look over the few runs.
bool IsInRunningDungeonRun(Player* bot);
// The running run of the bot's party lists this creature entry kill_first.
bool IsRunKillFirst(Player* bot, uint32_t entry);
// The area triggers the bot's running or paused run has armed (null = none): a
// move / move_direct step's `entry` the run has reached, until it fired once for
// the run. They fire for the bot like listed ones (BotAreaTriggers.cpp). One
// lookup (no group: none). The pointer is read before any trigger script runs.
std::vector<uint32_t> const* RunArmedAreaTriggers(Player* bot);
// The plan map of the bot's running or paused run (HoldsItsDead), 0 = none. One lookup, world thread.
uint32_t BoundRunPlanMap(Player* bot);
// A bot fired an area trigger: the run that armed it disarms it for good.
void NoteRunAreaTriggerFired(Player* bot, uint32_t id);

// A real player's chat command (whisper, `.bot command`, party chat) reached
// this bot and passed its authorization. While a run leader (`.bot run`) drives
// the bot's bound party: the commander's whisper pauses the run, so the
// player's order is not overridden by the run's next one (player control
// first); party chat while the run runs is chatting, not an order; another
// player's whisper is refused with a reply (RemoteCommander::
// ClassifyRunBoundOrder, players in the party 2026-10-23). False: the command
// is dropped. Always true while no run exists.
bool NoteHumanChatOrder(Player* bot, Player& from, std::string const& text, bool whisper);

// The "threat" back-off a binding gave a bot (RemoteCommander::
// CommanderThreatLedger): AiFactory and the strategy snapshot load note it,
// and the snapshot save leaves it out (it is the binding's, not the bot's).
void NoteCommanderThreat(Player* bot);
bool IsCommanderThreat(Player* bot);

// A tank healer of a bound raid's layout (the raid layout record in
// commands/Commander.cpp): its "focus heal targets" is the layout's, not
// the bot's, so the snapshot save leaves it out like the binding's "threat".
bool IsRaidFocusHealer(Player* bot);

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

// A commander's raid binding as the hire service needs it (C6, one more hire into a bound raid): the
// commander has a standing raid binding (the lead bot's group is the bound group), whether he is a member
// of that group, and how many members it holds. Cheap; world thread only.
struct BoundRaidFacts
{
    bool bound = false;
    bool commanderInGroup = false;
    uint32_t members = 0;
    // The binding's identity (a seat hire is rechecked against it before it joins).
    uint64_t leadGuid = 0;
    uint32_t groupId = 0;
};
BoundRaidFacts BoundRaidOf(Player* commander);
// The bot is a member of the group of the commander's standing binding (any kind). Read-only; world thread
// only.
bool IsInOwnBindingGroup(Player* commander, Player* bot);
// A new hire of the commander joins his bound raid: into the lead bot's group, into the binding (follow
// target, hire state) and into the raid layout, where only the new bot is placed: nobody else moves. True once the bot is in the
// bound group (also on later calls); false while it cannot join yet (not settled in the world, no
// seat), the caller retries. The binding is never released or rebuilt. World thread only.
bool JoinBoundRaid(Player* commander, Player* bot);

} // namespace BotCommands
} // namespace TortoiseBots
