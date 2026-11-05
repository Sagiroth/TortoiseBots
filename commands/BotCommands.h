#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "../runtime/CommandedGroupQuery.h" // IsInCommandedGroup
#include "../runtime/CommanderTelemetry.h"
#include "../runtime/CombatMonitorPolicy.h" // CombatMonitor::MemberInfo
#include "../runtime/DungeonMobTelemetry.h"
#include "../runtime/RaidTelemetry.h"

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
// report listed, sent every 500 ms while the group fights. Field and guid reads only, no grid or world scan. `wantCombat`
// selects the groups that fight, `wantIdle` the ones that do not (the emitter asks each on its own period, and both on a
// tick where both are due).
void CollectCommanderPositions(std::vector<CommanderTelemetry::PosRef>& out, bool wantCombat, bool wantIdle);
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
// The Raids tab (spec dash-raids-tab): the raid map copies a bot stands in, without a commander binding, for the RAIDS
// datagram. Walks only the bots and players the emitter's cycle already holds, reads each member's group slots; fills
// `out` sorted by (map, instance, group id) and records the same set for the sampler (ObservedRaidSet). World thread, once
// per 2 s snapshot cycle.
void CollectObservedRaids(std::vector<Player*> const& bots, std::vector<Player*> const& players,
                          std::vector<RaidTelemetry::GroupInfo>& out);
// The observed raid groups' combat books, 1 Hz on the world thread: one Refresh per group, a boss pick while it fights, and
// a released or vanished group's open fight closed after the lead-gone grace. A bound group is skipped (its book is the
// bound path's). Reads RaidsEnabled() only while nothing is observed; nothing while AiPlayerbot.ObservabilityRaids is 0.
void UpdateObservedRaids(uint32_t diff);
bool IsObservedRaidGroup(uint32_t groupId);
void ClearObservedRaids();
// Commander.cpp exports the observed-raid sampler shares with the bound path: a group's members in the combat monitor's
// shape, the bound commander's display name on a group ("" when none), whether a binding stands on it, and the fight's boss
// pick from the engaged creatures.
std::vector<CombatMonitor::MemberInfo> CombatMonitorMembersOf(std::vector<Player*> const& bots);
std::string BoundCommanderNameOfGroup(uint32_t groupId);
bool GroupIsBound(uint32_t groupId);
void NoteFightBoss(uint32_t groupId, Player* anchor);
// The boss timeline's per-tick sample (AiPlayerbot.CombatTimeline): the fight boss's health and current victim. The
// boss guid is the one NoteFightBoss picked (or the run's plan boss, NoteBossGuid); nothing when it is unknown.
void SampleFightTimeline(uint32_t groupId, Player* anchor);
// The G2 raid detail readers (ready facts, boss debuffs): the same per-bot reads the bound path does, shared so
// CollectObservedRaids fills them at most every 5 s without duplicating the aura logic.
void ReadRaidReadyFacts(Player* bot, CommanderTelemetry::BotExtra& extra, uint32_t nowMs);
void FillRaidBotDebuffs(Player* bot, CommanderTelemetry::BotExtra& extra);
// The bound path's overload: the exclusion set also holds the binding's bots and the commander, so a debuff another
// binding member cast on the bot is not the boss's work. The unbound raid detail passes no set.
void FillRaidBotDebuffs(Player* bot, CommanderTelemetry::BotExtra& extra,
    std::unordered_set<uint32_t> const& friendlyLow);
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
