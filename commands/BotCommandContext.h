#pragma once

#include "../runtime/BotManager.h"

#include "../ai/playerbot/PullExecutorPolicy.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;
class Unit;
namespace ai { class Event; }
class Group;


namespace TortoiseBots {
namespace BotCommands {

// One immutable view of the requester's world state. The selected target is
// resolved from SelectionGuid directly: ChatHandler::GetSelectedUnit() falls
// back to the requester when no target is selected, which is not a command
// target for Actions.
struct BotCommandContext
{
    Player* requester = nullptr;
    Unit* requesterTarget = nullptr;
    Unit* enemyTarget = nullptr;
    Group* group = nullptr;
    Player* selectedBot = nullptr;
    std::vector<Player*> partyBots;
    // Set by the run leader (.bot run) for the orders it sends: a pull then
    // holds the healer on its stand too (PausePartyDpsForPull) and keeps it
    // to heals. `healerStand` gives the point behind the tank for one healer
    // (false: no better point than the tank's own spot); empty for any other
    // order.
    bool runLeader = false;
    std::function<bool(Player*, float&, float&, float&)> healerStand;
    // Called for each healer a run-leader pull sets heal-only, so the run
    // leader lists it (its sync lets it go again when it is not wanted).
    std::function<void(Player*)> noteHealOnly;
};

// Every command path uses these checks. A BotRecord alone is not sufficient:
// the player must be a live module-owned Headless character and the requester
// must be its durable owner (or a GM).
bool IsBotAdministrator(Player* requester);
bool CanControlBot(Player* requester, BotRecord const* record);
bool IsLiveHeadlessBot(Player* bot, BotRecord const* record = nullptr);

BotCommandContext BuildContext(Player* requester);

// Dynamic scope: selecting a controllable owned bot targets only that bot;
// otherwise the request fans out to the owned party bots in the context.
std::vector<Player*> ResolveDynamicScope(BotCommandContext const& context);

// Pull and pullback use a tactical executor rather than broadcasting to every
// bot. A selected bot is considered first only when the request also resolves
// a valid enemy target; otherwise a live tank-capable party bot is selected.
// A tank counts as busy only while its combat flag is backed by a live
// opponent (a victim or attackers); a bare flag with neither is stale and the
// tank stays a candidate (issue #908).
Player* ResolvePullExecutor(BotCommandContext const& context, bool allowSelected = true);
// True when no executor was free but a tank-capable bot (the selected one or a
// party bot) is alive and really fighting: the pull is refused as `tank-busy`,
// not `no-tank`. Read only after ResolvePullExecutor returned null.
bool PullTankIsBusy(BotCommandContext const& context);
// Why the scope's bots were not pull executors, for the refusal detail: the
// most common exclusion (for example "in combat", "dead") across the selected
// bot and the party bots, with a repeat count. Empty when every bot was free
// (or the scope itself was empty).
std::string PullBlockerDetail(BotCommandContext const& context);
// Tally behind PullBlockerDetail, exposed for tests.
void CountPullBlockers(BotCommandContext const& context, std::map<ai::PullExcludeReason, uint32>& counts);
// Hold party DPS for the join delay while the tank pulls and
// establishes threat. Tanks are untouched. Healers are untouched too, except
// in the run leader's pulls (context.runLeader): a healer then stays on its
// stand, 9 yd behind the tank, like the others, and can heal from there (no
// wait gate, no damage spell, no following). (The party-chat pull hold,
// ai::PausePartyPullHold, gives healers the join window without a stay: they
// heal but deal no damage while it runs.) For a pullback the
// window additionally covers the tank's return leg and is narrowed to the
// join delay on arrival.
void PausePartyDpsForPull(BotCommandContext const& context, Player* executor, uint32 joinDelaySeconds, bool pullback = false);
// Records the per-command return mode (plus join delay) on the PullStrategy;
// PullEndAction restores the tank's default when the pull ends. rangedOrder is
// the `ranged` word of the order: open from range on any pull.
bool ConfigurePullMode(PlayerbotAI* ai, bool pullback, uint32 joinDelaySeconds = 0, bool rangedOrder = false);
// Resolve one live executor with a currently usable interrupt action. The
// action name comes from the mature class/pet action graph, not a duplicated
// class-to-spell table. Explicitly targeted bots never fall back to another
// executor when they cannot interrupt.
Player* ResolveInterruptExecutor(BotCommandContext const& context, Unit* target,
    std::string* outAction = nullptr);
// Resolve exactly one CC executor. Explicitly targeted bots never fall back to
// another executor when they lack the mature CC capability. Capability comes
// from the mature action graph; the optional output identifies the selected
// action for an immediate attempt.
enum class CcDenyReason
{
    None,
    NoSpellKnown,
    OutdoorsOnly,
    WrongCreatureType,
    Transient,
};
std::string CcDenyText(CcDenyReason reason, std::string const& spell, std::string const& targetName);
Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target, std::string const& mark,
    std::string* outAction = nullptr, std::string const& family = std::string());
Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target, std::string const& mark,
    std::string* outAction, CcDenyReason* outDeny, std::string* outDenySpell,
    std::string const& family = std::string());

// Execute a mature action while suppressing only its transient chat output.
// The pre-existing non-combat `silent` strategy is restored exactly on scope
// exit, so this helper cannot change a bot's durable strategy configuration.
bool ExecuteQuietAction(PlayerbotAI* ai, std::string const& action, ai::Event const& event);
// Execute one action for an explicit order, quietly, past the engine's
// isUseful/isPossible gates: those say when the automation may pick the action
// (a movement action is useless while the bot holds "stay"), and the order put
// the bot on that stay itself. The action's own checks still apply.
bool ExecuteOrderedAction(PlayerbotAI* ai, std::string const& action, ai::Event const& event);
// Run one normal AI step without exposing incidental bot chat to the owner.
void ExecuteQuietNextAction(PlayerbotAI* ai, bool minimal = false);
// Put a named mature action on the active engine so its existing prerequisite
// and continuation chain can handle reach-then-cast requests.
bool QueueMatureAction(PlayerbotAI* ai, std::string const& action,
    ai::Event const& event, float relevance);

// Durable ownership snapshots are exposed through BotManager. This helper
// centralizes the state labels used by the structured roster protocol.
std::string RosterState(Player* bot, BotRecord const* record);

} // namespace BotCommands
} // namespace TortoiseBots
