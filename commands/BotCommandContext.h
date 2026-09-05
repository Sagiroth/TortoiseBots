#pragma once

#include "../runtime/BotManager.h"

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
Player* ResolvePullExecutor(BotCommandContext const& context, bool allowSelected = true);
// Select the existing mature pull policy for the next requested pull. Ordinary
// Pull removes `pull back`; Pullback enables it so PullEnd can return to the
// stored pull position. No custom movement state is introduced.
bool ConfigurePullMode(PlayerbotAI* ai, bool pullback);
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
Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target, std::string const& mark,
    std::string* outAction = nullptr);

// Execute a mature action while suppressing only its transient chat output.
// The pre-existing non-combat `silent` strategy is restored exactly on scope
// exit, so this helper cannot change a bot's durable strategy configuration.
bool ExecuteQuietAction(PlayerbotAI* ai, std::string const& action, ai::Event const& event);
// Run one normal AI step without exposing incidental bot chat to the owner.
void ExecuteQuietNextAction(PlayerbotAI* ai, bool minimal = false);

// Durable ownership snapshots are exposed through BotManager. This helper
// centralizes the state labels used by the structured roster protocol.
std::string RosterState(Player* bot, BotRecord const* record);

} // namespace BotCommands
} // namespace TortoiseBots
