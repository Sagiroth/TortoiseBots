#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../runtime/CommandedGroupQuery.h" // IsInCommandedGroup
#include "../runtime/CommanderTelemetry.h"
#include "../runtime/DungeonMobTelemetry.h"

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
// The live positions of every bound group for the dashboard's map (task S3): the bots and the enemies its lead's last full
// report listed, sent every 500 ms while the group fights. Field and guid reads only, no grid or world scan. `wantIdle`
// adds the groups that do not fight (the emitter asks on its slower idle period).
void CollectCommanderPositions(std::vector<CommanderTelemetry::PosRef>& out, bool wantIdle);
// The elite and boss cast starts of the bound groups, once per 250 ms on
// the world thread from the emitter (only while a fight runs). Reads the mobs
// of each binding's last full report; a guid lookup per unit, no world scan.
void PollEnemyCasts();
// The dashboard's boss spawn cache (every known boss of each dungeon and raid map, from one pass over the core's creature spawn data):
// built once, from BotHostAdapter::OnStartup right after sPlayerbotAIConfig.Initialize() sealed the dungeon plans, never inside a
// commander snapshot; logs how long it took. Does nothing before the seal or a second time.
void BuildBossSpawnCache();
// Issue #1101: the mobs of every dungeon run for the dashboard's Dungeons tab. The emitter hands over the runs (members, sight
// readers, cap); this fills each run's candidates from what the members already hold (victims, selections, attackers, the
// bots' cached "attackers" and the sight readers' cached "possible targets"), each guid resolved at most once per run, no grid
// or world scan. World thread, once per 2 s snapshot cycle, outside the map update.
void CollectDungeonMobs(std::vector<DungeonMobTelemetry::RunRef>& runs);
// Any order a player gives the party pauses its run (`.bot run`); the run's own orders never pause it. Does nothing without a run.
void PauseDungeonRunForPlayer(uint64_t commanderRaw, std::string const& order);
// Another real player of a run-bound party: the run's commander (players in the party, 2026-10-23), else 0.
uint64_t BoundForeignCommander(Player* player);
// True when bot is in the group the commander is bound to: master-relative
// conveniences (the summon out of a dungeon when the master leaves it) leave
// such a bot with its lead bot.
bool IsCommandedBy(Player* commander, Player* bot);

} // namespace BotCommands
} // namespace TortoiseBots
