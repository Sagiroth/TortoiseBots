#pragma once

#include "BotCommandContext.h"

#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/PullOpeningPolicy.h"
#include "../ai/playerbot/strategy/generic/PullStrategy.h"

#include <cstdint>
#include <string>

class ChatHandler;
class Player;
class PlayerbotAI;
class Unit;

namespace TortoiseBots {
namespace BotCommands {

// Helpers and entry points that more than one command file needs. Everything
// a single domain owns stays private to its own file.
std::string Trim(std::string value);
Player* Requester(ChatHandler* handler);
bool ResolveOwnedBot(ChatHandler* handler, char const* args, Player*& bot, BotRecord*& record, std::string& name);

// TBM protocol lines: pipe-safe escaping and the shared action ack/err
// answers (the commander's order log notes every reply).
std::string ProtocolSafe(std::string value);
void NoteCommanderOrderResult(ChatHandler* handler, std::string const& intent, bool ok, std::string const& result);
void SendActionError(ChatHandler* handler, std::string const& intent, char const* code, char const* message);
void SendActionAck(ChatHandler* handler, std::string const& intent, std::string const& scope, uint32_t count,
    std::string const& executor = std::string());

// Party.cpp: movement anchors and stay-breaking shared with the action and
// commander files.
void SetStayAnchor(PlayerbotAI* ai, float x, float y, float z, uint32_t mapId);
void RelaxTacticalMovement(PlayerbotAI* ai);
bool CheckPullTarget(Player* executor, Unit* target, std::string& code, std::string& message);
void SendBotStateLine(ChatHandler* handler, Player* bot);
// Info.cpp: the movement mode name of a bot's AI (its "follow"/"stay"/...
// strategy), also shown by the roster.
char const* MovementName(PlayerbotAI* ai);

// Per-bot behaviour toggles the TBM bot panel exposes. Each key maps to one
// mature strategy on one engine; nothing outside this list is reachable from
// `.bot behavior`, so the addon cannot be used as a generic strategy editor.
// An inverted key is ON while its strategy is ABSENT: `aoe` is on by default
// and `aoe off` adds the "no aoe" veto on every area spell.
struct BehaviorToggle
{
    char const* key;
    char const* strategy;
    BotState state;
    bool inverted = false;
};
BehaviorToggle const* FindBehaviorToggle(std::string const& key);
bool IsBehaviorOn(PlayerbotAI* ai, BehaviorToggle const& toggle);
void SetBehavior(PlayerbotAI* ai, BehaviorToggle const& toggle, bool enable);

// What ConfigurePullMode changes on the tank: the "pull back" strategy in each
// engine and the PullStrategy command fields. Taken before it and put back
// exactly when the pull is refused afterwards, so a refused command neither
// leaves the tank in pull mode nor ends or rewrites an earlier pull that is
// still running (OnPullEnded / "pull end" would).
struct PullModeSnapshot
{
    bool pullBack[static_cast<uint8>(BotState::BOT_STATE_ALL)] = {};
    bool hasCommand = false;
    PullStrategy::CommandState command = {};
};

PullModeSnapshot SavePullMode(PlayerbotAI* ai);
void RestorePullMode(PlayerbotAI* ai, PullModeSnapshot const& snapshot);
void WarnPartyStayAfterPull(ChatHandler* handler, BotCommandContext const& context, Player* executor);

// Action.cpp: the crowd-control and pull order bodies, shared with the
// commander's `cmd cc` / `cmd pull`.
bool ExecuteInterruptAction(ChatHandler* handler, BotCommandContext const& context, Player* requester);
std::string CurrentCcMark(PlayerbotAI* ai);
bool IsRaidTargetMark(std::string const& mark);
bool RunCcOrder(ChatHandler* handler, BotCommandContext const& context, Player* requester,
    std::string const& intent, std::string const& reportIntent);
bool RunPullOrder(ChatHandler* handler, BotCommandContext const& context, Player* requester,
    std::string const& intent, std::string const& option, std::string const& reportIntent);
// The parsed pull option ("[seconds] [ranged]") as ack detail text.
std::string PullOptionText(ai::PullOrderOption const& parsed);
// The `.bot action raid ...` intents (status/tankface/douse/custom), shared
// with the commander's raid orders.
bool HandleRaidAction(ChatHandler* handler, BotCommandContext const& context, Player* requester,
    std::string const& intent);

// Inventory.cpp: pool bots are exempt from the maintenance conveniences,
// including `.bot autogear` (BotCommands.cpp).
bool IsOwnedBotQolEligible(Player* bot, BotRecord const* record);

} // namespace BotCommands
} // namespace TortoiseBots
