// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "../ai/playerbot/strategy/actions/TwinsOpeningActions.h"
#include "BotCommandContext.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
#include "../runtime/ExperimentalBisService.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/AltBotPolicy.h"
#include "../runtime/AltBotService.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireRaidPolicy.h"
#include "../runtime/HireSpecPolicy.h"
#include "../runtime/HireLevelPolicy.h"
#include "../runtime/HireTierRequestPolicy.h"
#include "BotCommandContext.h"
#include "../behavior/PlayerConvenience.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/AhMarketService.h"
#include "../host/ModuleVersion.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/RandomBotAccountRegistry.h"
#include "../runtime/RandomBotPoolReset.h"
#include "../runtime/RandomBotService.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotAI.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotDbStore.h"
#include "../ai/playerbot/ChatHelper.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/PullOpeningPolicy.h"
#include "../ai/playerbot/PullStartPolicy.h"
#include "../ai/playerbot/PullRefusalPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/strategy/generic/PullStrategy.h"
#include "../ai/playerbot/strategy/values/RtiTargetValue.h"
#include "../ai/playerbot/strategy/values/AttackersValue.h"
#include "../ai/playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../ai/playerbot/strategy/values/PositionValue.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../ai/playerbot/strategy/actions/EquipAction.h"
#include "../ai/playerbot/strategy/actions/UnequipAction.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotFactory.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/SurvivePolicy.h"
#include "runtime/OwnedBotQolPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"
#include "../ai/playerbot/AiFactory.h"
#include "../runtime/RemoteCommanderView.h"
#include "../runtime/RemoteCommanderTasks.h"
#include "../runtime/RemoteCommanderEnter.h"
#include "../runtime/CommanderTelemetry.h"
#include "../ai/playerbot/LootObjectStack.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Value.h"
#include "../ai/playerbot/GuidPosition.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemGroupPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldPacket.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/PathFinder.h"
#include "Movement/spline/MoveSplineInit.h"
#include "Maps/Cell.h"
#include "Maps/Map.h"
#include "Database/DBCStores.h"
#include "Database/SQLStorages.h"
#include "Maps/CellImpl.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Creature.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Spell.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Threat/ThreatManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <memory>
#include <cstring>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {

bool HandleInvite(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot invite <online bot name> (same account only)");
        return true;
    }
    if (bot == requester || !requester->GetSession())
    {
        handler->PSendSysMessage("That character cannot be invited as your bot.");
        return true;
    }

    // If the bot is still in another stale group, leave it so the invite can proceed.
    if (Group* oldGroup = bot->GetGroup())
    {
        if (oldGroup != requester->GetGroup())
        {
            oldGroup->RemoveMember(bot->GetObjectGuid(), 0);
        }
    }

    // Match the mature PlayerBots invite path: a full normal group must become
    // a raid before inviting another controlled bot.  Native handlers retain
    // ownership of the actual invitation and acceptance transitions.
    Group* requesterGroup = requester->GetGroup();
    if (requesterGroup && requesterGroup->isBGGroup())
        requesterGroup = requester->GetOriginalGroup();
    if (requesterGroup && !requesterGroup->IsLeader(requester->GetObjectGuid()))
    {
        Player* currentLeader = sObjectMgr.GetPlayer(requesterGroup->GetLeaderGuid());
        if (currentLeader && currentLeader->GetSession() && currentLeader->GetSession()->IsHeadless())
            requesterGroup->ChangeLeader(requester->GetObjectGuid());
    }
    if (requesterGroup && !requesterGroup->IsRaidGroup() &&
        requesterGroup->GetMembersCount() > 4)
    {
        requesterGroup->ConvertToRaid();
    }

    // Let the native group handler create the invite. This emits the real
    // SMSG_GROUP_INVITE, which the module packet bridge feeds to PlayerbotAI.
    auto* previousInvite = bot->GetGroupInvite();
    WorldPacket packet;
    packet << bot->GetName() << uint32(0);
    requester->GetSession()->HandleGroupInviteOpcode(packet);
    if (bot->GetGroupInvite() == previousInvite)
    {
        handler->PSendSysMessage("The group invitation for %s was rejected by the native group handler.", name.c_str());
        return true;
    }
    // The packet path remains queued for ordinary AI processing, but do not
    // make the first group formation wait for an unrelated AI tick. Reuse the
    // mature action, which still validates security and accepts through the
    // core's native group handler. If it cannot run yet, the queued packet is
    // the safe asynchronous fallback.
    bool joinedImmediately = false;
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        ai::Event inviteEvent("group invite", "", requester);
        joinedImmediately = ai->DoSpecificAction("accept invitation", inviteEvent, true) &&
            bot->IsInSameGroupWith(requester);
    }

    if (joinedImmediately)
        handler->PSendSysMessage("Invitation sent to bot %s; it joined your group.", name.c_str());
    else
        handler->PSendSysMessage("Invitation sent to bot %s; it may accept it asynchronously.", name.c_str());
    return true;
}

bool HandleUninvite(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot uninvite <online bot name> (same account only)");
        return true;
    }

    Group* requesterGroup = requester->GetGroup();
    if (requesterGroup && requesterGroup->isBGGroup())
        requesterGroup = requester->GetOriginalGroup();
    if (requesterGroup && !requesterGroup->IsLeader(requester->GetObjectGuid()))
    {
        Player* currentLeader = sObjectMgr.GetPlayer(requesterGroup->GetLeaderGuid());
        if (currentLeader && currentLeader->GetSession() && currentLeader->GetSession()->IsHeadless())
            requesterGroup->ChangeLeader(requester->GetObjectGuid());
    }

    WorldPacket packet;
    packet << bot->GetName();
    requester->GetSession()->HandleGroupUninviteOpcode(packet);
    handler->PSendSysMessage("Uninvite sent for bot %s.", name.c_str());
    return true;
}

// Direct single-bot interrupt: same probe-and-execute path as
// `.bot action interrupt`, but scoped to one owned bot instead of a
// tactical executor search. Resolving the owner-selected or named bot first
// keeps this consistent with .bot attack/.bot stay single-target verbs.
bool HandleInterrupt(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot interrupt <online bot name> (same account only)");
        return true;
    }
    Unit* target = nullptr;
    ObjectGuid selectionGuid = requester->GetSelectionGuid();
    if (!selectionGuid.IsEmpty())
    {
        Unit* selected = requester->GetSelectedUnit();
        if (selected && selected != requester &&
            !BotManager::Instance().IsBot(selected->GetObjectGuid()))
            target = selected;
    }
    if (!target || !target->IsInWorld() || !target->IsAlive())
    {
        handler->PSendSysMessage("Select a live enemy target first.");
        return true;
    }
    if (!target->IsNonMeleeSpellCasted(true))
    {
        handler->PSendSysMessage("The target is not casting an interruptible spell.");
        return true;
    }
    BotCommandContext context;
    context.requester = requester;
    context.selectedBot = bot;
    context.enemyTarget = target;
    return ExecuteInterruptAction(handler, context, requester);
}

// Match RoleChatFilter: group role overrides count, and melee/ranged are
// range categories (so @melee includes melee tanks, not just melee DPS).
static bool StrategyRoleMatches(PlayerbotAI* ai, Player* bot, std::string const& role)
{
    bool const inGroup = bot->GetGroup() != nullptr;
    if (role == "@tank") return ai->IsTank(bot, inGroup);
    if (role == "@heal") return ai->IsHeal(bot, inGroup);
    if (role == "@dps") return !ai->IsTank(bot, inGroup) && !ai->IsHeal(bot, inGroup);
    if (role == "@ranged") return ai->IsRanged(bot, inGroup);
    if (role == "@melee") return !ai->IsRanged(bot, inGroup);
    return role.empty();
}

// Prefer a complete registered name over interpreting its last word as a bot.
// Query the registry, not the active set: +strategy must work before activation.
static bool KnownStrategyChange(std::vector<Player*> const& scope, std::string const& change)
{
    std::set<std::string> supported;
    for (Player* bot : scope)
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            ai->GetAiObjectContext()->GetSupportedStrategies(supported);
    std::istringstream changes(change);
    std::string part;
    bool any = false;
    while (std::getline(changes, part, ','))
    {
        if (part.size() < 2 || (part[0] != '+' && part[0] != '-' && part[0] != '~') ||
            !supported.count(part.substr(1)))
            return false;
        any = true;
    }
    return any && change.back() != ',';
}

// Direct strategy toggle: `.bot strategy <+|-name> [bot]` fans out over the
// dynamic scope (selected bot or owned party bots), mirroring `.bot loot`.
// Thin wrapper over the mature ChangeStrategy path; validation and
// persistence stay AI-owned via ChangeStrategyAction/DbStore.
// Whole multi-word names, with an optional trailing role or owned bot name.
// Changes and persistence still use the mature AI-owned paths.
bool HandleStrategy(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    std::string input = Trim(args ? args : "");
    if (input.empty() || (input[0] != '+' && input[0] != '-' && input[0] != '~'))
    {
        handler->PSendSysMessage("Usage: .bot strategy [+/-/~]<strategy name> [role or BotName] (roles: @tank, @dps, @heal, @melee, @ranged)");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::string change = input;
    std::string role;
    std::vector<Player*> scope = ResolveDynamicScope(context);
    size_t separator = input.find_last_of(" \t");
    if (separator != std::string::npos)
    {
        std::string const suffix = input.substr(separator + 1);
        if (suffix[0] == '@')
        {
            if (suffix != "@tank" && suffix != "@dps" && suffix != "@heal" &&
                suffix != "@melee" && suffix != "@ranged")
            {
                handler->PSendSysMessage("Unknown role filter. Use @tank, @dps, @heal, @melee or @ranged.");
                return true;
            }
            role = suffix;
            change = Trim(input.substr(0, separator));
            // An explicit role scope addresses the party, even with a bot selected.
            scope = context.partyBots;
            // Validate against the role-matching bots only: a strategy that
            // only bots outside the role support must still be rejected,
            // and an empty role match is reported instead of silently
            // applying to zero bots.
            std::vector<Player*> roleScope;
            for (Player* bot : scope)
            {
                PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(bot);
                if (botAi && StrategyRoleMatches(botAi, bot, role))
                    roleScope.push_back(bot);
            }
            scope = roleScope;
        }
        else if (!KnownStrategyChange(scope, input))
        {
            Player* bot = nullptr;
            BotRecord* record = nullptr;
            std::string name;
            if (ResolveOwnedBot(handler, suffix.c_str(), bot, record, name))
            {
                scope = { bot };
                change = Trim(input.substr(0, separator));
            }
        }
    }
    if (scope.empty())
    {
        if (!role.empty())
            handler->PSendSysMessage("No party bot matches role %s.", role.c_str());
        else
            handler->PSendSysMessage("No live owned party bots are controllable.");
        return true;
    }
    if (!KnownStrategyChange(scope, change))
    {
        handler->PSendSysMessage("Unknown strategy change or unavailable bot name: %s", input.c_str());
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai || !StrategyRoleMatches(ai, bot, role))
            continue;
        ai->ChangeStrategy(change, BotState::BOT_STATE_ALL);
        sPlayerbotDbStore.Save(ai);
        ++succeeded;
    }
    handler->PSendSysMessage("Strategy change '%s' applied to %u bot(s).", change.c_str(), succeeded);
    return true;
}

bool HandleStay(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot stay <online bot name> (same account only)");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", name.c_str());
        return true;
    }

    // Reuse the stay action so the command updates both strategies and the
    // current "return"/"stay" position anchors. An older chat order still
    // queued in the AI must not run after it (runtime/QueuedOrderPolicy.h).
    ai->DropQueuedMovementOrders();
    ai::Event stayEvent("stay", "", requester);
    if (!ai->DoSpecificAction("stay chat shortcut", stayEvent, true))
    {
        handler->PSendSysMessage("Bot %s could not enter stay mode.", name.c_str());
        return true;
    }

    handler->PSendSysMessage("Bot %s will stay.", name.c_str());
    return true;
}

// relaxMovement: the order sends the bot at a target, so it ends a stay the
// same way the party-wide `.bot action` form does (RelaxTacticalMovement).
// movementOrder: the order sets the bot's movement mode, so the movement
// orders still queued from chat are older and dropped first
// (runtime/QueuedOrderPolicy.h).
static bool HandleNamedAction(ChatHandler* handler, char const* args, char const* command,
    char const* action, char const* confirmation, bool relaxMovement = false, bool movementOrder = false)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot %s <online bot name> (same account only)", command);
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", name.c_str());
        return true;
    }

    if (relaxMovement)
        RelaxTacticalMovement(ai);
    if (movementOrder)
        ai->DropQueuedMovementOrders();

    ai::Event event(command, "", requester);
    if (!ai->DoSpecificAction(action, event, true))
    {
        handler->PSendSysMessage("Bot %s could not execute %s.", name.c_str(), command);
        return true;
    }

    handler->PSendSysMessage("Bot %s %s.", name.c_str(), confirmation);
    return true;
}

// Moves a staying bot's anchors to a new spot: the "stay" position that
// "return to stay position" walks back to, and the idle "return" anchor the
// return strategy walks to, exactly as the stay shortcut sets both to the
// bot's own spot. Setting only "stay" left "return" at the spot of an
// earlier stay order, and the two strategies then walked the bot back and
// forth between the old and the new spot every few seconds.
void SetStayAnchor(PlayerbotAI* ai, float x, float y, float z, uint32 mapId)
{
    ai::PositionMap& posMap = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
    ai::PositionEntry stayPos = posMap["stay"];
    stayPos.Set(x, y, z, mapId);
    posMap["stay"] = stayPos;
    ai::PositionEntry returnPos = posMap["return"];
    returnPos.Set(x, y, z, mapId);
    posMap["return"] = returnPos;
    posMap["random"].Reset();
}

// Tactical requests must be able to leave a prior stay/follow hold. The
// movement and combat behavior itself remains owned by PlayerbotAI.
void RelaxTacticalMovement(PlayerbotAI* ai)
{
    if (!ai)
        return;

    ai->DropQueuedMovementOrders();
    ai->EndStayForOrder();
}

// The same refusals PullRequestAction::Execute makes, checked up front: the
// command runs that action silently, so its own "too far" / "can't be pulled"
// whispers never reach the player, and checking first keeps a refused pull
// from changing any bot's state. Returns false with an ACTION_ERR code and a
// player-facing reason.
bool CheckPullTarget(Player* executor, Unit* target, std::string& code, std::string& message)
{
    if (!executor || !target || !target->IsInWorld() || target->GetMapId() != executor->GetMapId())
    {
        code = "invalid-target";
        message = "Your target is not on the tank's map any more.";
        return false;
    }
    if (!target->IsAlive())
    {
        code = "dead-target";
        message = "Your target is dead. Select a live enemy first.";
        return false;
    }
    if (sServerFacade.IsFriendlyTo(target, executor))
    {
        code = "friendly";
        message = "Your target is friendly to the tank (for example another player's pet) and cannot be pulled.";
        return false;
    }
    float const maxPullDistance = sPlayerbotAIConfig.reactDistance * 3;
    if (target->GetDistance(executor) > maxPullDistance)
    {
        code = "too-far";
        message = "Your target is too far from the tank to pull (more than " +
            std::to_string(static_cast<uint32>(maxPullDistance)) + " yd).";
        return false;
    }
    if (!ai::AttackersValue::IsValid(target, executor, nullptr, false))
    {
        code = "invalid-target";
        message = "The tank cannot pull your target: it is not attackable, evading, out of the tank's sight, or a player.";
        return false;
    }
    return true;
}

PullModeSnapshot SavePullMode(PlayerbotAI* ai)
{
    PullModeSnapshot snapshot;
    if (!ai)
        return snapshot;
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
        snapshot.pullBack[i] = ai->HasStrategy("pull back", static_cast<BotState>(i));
    if (PullStrategy* strategy = PullStrategy::Get(ai))
    {
        snapshot.hasCommand = true;
        snapshot.command = strategy->SaveCommand();
    }
    return snapshot;
}

void RestorePullMode(PlayerbotAI* ai, PullModeSnapshot const& snapshot)
{
    if (!ai)
        return;
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
    {
        BotState state = static_cast<BotState>(i);
        if (ai->HasStrategy("pull back", state) != snapshot.pullBack[i])
            ai->ChangeStrategy(snapshot.pullBack[i] ? "+pull back" : "-pull back", state);
    }
    if (snapshot.hasCommand)
        if (PullStrategy* strategy = PullStrategy::Get(ai))
            strategy->RestoreCommand(snapshot.command);
}

bool HandleGuard(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "guard", "guard chat shortcut", "will guard this position", false, true);
}

bool HandleFree(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "free", "free chat shortcut", "is free to move", false, true);
}

bool HandleReady(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "ready", "ready check", "will run a readiness check");
}

bool HandleAttack(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "attack", "attack my target", "will attack your selected target", true);
}

static bool IsPublicFormation(std::string const& formation)
{
    return formation == "default" || formation == "melee" || formation == "queue" ||
        formation == "chaos" || formation == "circle" || formation == "line" ||
        formation == "shield" || formation == "arrow" || formation == "near" ||
        formation == "far";
}

bool HandleFormation(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::string input = Trim(args ? args : "");
    if (!requester || input.empty())
    {
        handler->PSendSysMessage("Usage: .bot formation [botName] <default/melee/queue/chaos/circle/line/shield/arrow/near/far>");
        return true;
    }

    size_t separator = input.find_first_of(" \t");
    std::string firstToken = input.substr(0, separator);
    for (char& c : firstToken)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Single formation name without bot name applies to dynamic scope
    if (separator == std::string::npos && IsPublicFormation(firstToken))
    {
        BotCommandContext context = BuildContext(requester);
        std::vector<Player*> scope = ResolveDynamicScope(context);
        if (scope.empty())
        {
            handler->PSendSysMessage("No live owned party bots are controllable.");
            return true;
        }

        uint32 succeeded = 0;
        for (Player* bot : scope)
        {
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai) continue;
            ai::Event event("formation", firstToken, requester);
            if (ai->DoSpecificAction("formation", event, true))
                ++succeeded;
        }

        if (context.selectedBot && scope.size() == 1)
            handler->PSendSysMessage("Bot %s formation set to %s.", context.selectedBot->GetName(), firstToken.c_str());
        else
            handler->PSendSysMessage("Party formation set to %s (%u bots updated).", firstToken.c_str(), succeeded);
        return true;
    }

    if (separator == std::string::npos)
    {
        handler->PSendSysMessage("Usage: .bot formation [botName] <default/melee/queue/chaos/circle/line/shield/arrow/near/far>");
        return true;
    }

    std::string botName = input.substr(0, separator);
    std::string formation = Trim(input.substr(separator + 1));
    for (char& character : formation)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    if (!IsPublicFormation(formation))
    {
        handler->PSendSysMessage("Unknown formation. Use default, melee, queue, chaos, circle, line, shield, arrow, near, or far.");
        return true;
    }

    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!ResolveOwnedBot(handler, botName.c_str(), bot, record, name))
    {
        handler->PSendSysMessage("You may only control an online bot on your account.");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", name.c_str());
        return true;
    }

    ai::Event event("formation", formation, requester);
    if (!ai->DoSpecificAction("formation", event, true))
    {
        handler->PSendSysMessage("Bot %s could not set formation %s.", name.c_str(), formation.c_str());
        return true;
    }

    handler->PSendSysMessage("Bot %s formation set to %s.", name.c_str(), formation.c_str());
    return true;
}

// Explicit role designation (Player Agency First): a leveling Arms/Fury
// warrior, Ret paladin, or Feral druid can serve as the party tank without
// changing talents. The tank kit mirrors AiFactory's native tank strategies
// so the bot qualifies via ContainsStrategy(STRATEGY_TYPE_TANK) and can pull.
// Both spec placeholders are offered; each class context keeps the one it
// knows ("protection" for Warrior/Paladin, "tank feral" for Druid) and the
// engine silently ignores the unknown name.
bool HandleRole(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::string input = Trim(args ? args : "");
    size_t separator = input.find_first_of(" \t");
    std::string botToken = separator == std::string::npos ? input : input.substr(0, separator);
    std::string roleToken = separator == std::string::npos ? std::string() : Trim(input.substr(separator + 1));
    for (char& c : roleToken)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    uint8 role = 255;
    if (roleToken == "tank")
        role = static_cast<uint8>(ai::BOT_ROLE_TANK);
    else if (roleToken == "healer" || roleToken == "heal")
        role = static_cast<uint8>(ai::BOT_ROLE_HEALER);
    else if (roleToken == "dps")
        role = static_cast<uint8>(ai::BOT_ROLE_DPS);
    else if (roleToken == "clear" || roleToken == "none" || roleToken == "reset")
        role = static_cast<uint8>(ai::BOT_ROLE_NONE);

    if (requester && role != 255 &&
        (botToken == "self" || botToken == requester->GetName()))
    {
        PlayerbotAIStorage& storage = PlayerbotAIStorage::Instance();
        if (role == static_cast<uint8>(ai::BOT_ROLE_NONE))
            storage.ClearPlayerForcedRole(requester->GetObjectGuid());
        else
            storage.SetPlayerForcedRole(requester->GetObjectGuid(), role);

        char const* label = role == static_cast<uint8>(ai::BOT_ROLE_TANK) ? "tank"
            : role == static_cast<uint8>(ai::BOT_ROLE_HEALER) ? "healer"
            : role == static_cast<uint8>(ai::BOT_ROLE_DPS) ? "dps" : "cleared";
        handler->PSendSysMessage("Your role set to %s.", label);
        return true;
    }
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || botToken.empty() || role == 255 ||
        !ResolveOwnedBot(handler, botToken.c_str(), bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot role <online bot name/self/your name> <tank/healer/dps/clear>");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", name.c_str());
        return true;
    }

    // Rebuild from the talent spec plus the forced role so a previous
    // designation cannot leak its strategy kit into the new role. The stored
    // snapshot is spec-dependent, so drop its strategy rows first (independent
    // value rows such as formation and CC marks are retained).
    ai->SetForcedRole(role);
    sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
    ai->ResetStrategies();
    if (role == static_cast<uint8>(ai::BOT_ROLE_TANK))
    {
        if (bot->GetClass() == CLASS_SHAMAN)
        {
            // Shaman tanks have their own spec strategy (issue #1125):
            // +protection/+tank feral have no shaman creators and would be
            // silently ignored, leaving the enhancement DPS kit installed.
            ai->ChangeStrategy("+tank shaman,+tank assist", BotState::BOT_STATE_NON_COMBAT);
            ai->ChangeStrategy("+tank shaman,+tank assist,+pull,+pull back,+close,+tank face",
                BotState::BOT_STATE_COMBAT);
        }
        else
        {
            ai->ChangeStrategy("+protection,+tank feral,+tank assist", BotState::BOT_STATE_NON_COMBAT);
            ai->ChangeStrategy("+protection,+tank feral,+tank assist,+pull,+pull back,+close",
                BotState::BOT_STATE_COMBAT);
        }
    }
    else if (role == static_cast<uint8>(ai::BOT_ROLE_DPS) && bot->GetClass() == CLASS_DRUID)
    {
        // A Feral ordered to DPS fights as a Cat: drop any Bear leftovers
        // ResetStrategies may have installed, then hold the Cat kit.
        ai->ChangeStrategy("-tank feral,-tank assist,-protection,-pull,-pull back", BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("-tank feral,-tank assist,-protection,-pull,-pull back", BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy("+dps feral,+dps assist,+close,+behind", BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+dps feral,+dps assist,+close,+behind", BotState::BOT_STATE_COMBAT);
    }
    sPlayerbotDbStore.Save(ai);

    // Issue #189 Phase 3: role-only designation stays gear-neutral, but warn
    // when the worn gear cannot back the role (squishy tank, no heals).
    if (role == static_cast<uint8>(ai::BOT_ROLE_TANK))
    {
        uint8 cls = bot->GetClass();
        if (cls == CLASS_WARRIOR || cls == CLASS_PALADIN)
        {
            Item const* offhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            ItemPrototype const* offProto = offhand ? offhand->GetProto() : nullptr;
            bool hasShield = offProto && offProto->Class == ITEM_CLASS_ARMOR &&
                offProto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD;
            if (!hasShield)
                handler->PSendSysMessage("Warning: %s has no shield equipped — will be squishy as tank.", name.c_str());
        }
        else if (cls == CLASS_DRUID)
        {
            if (!bot->HasSpell(5487) && !bot->HasSpell(9634))
                handler->PSendSysMessage("Warning: %s has no bear form — cannot tank effectively.", name.c_str());
        }
        else if (cls == CLASS_SHAMAN)
        {
            // A shaman tank with shield + Rockbiter + a key tank talent
            // (Totemic Alignment / Spirit Armor) backs the role (issue #1125);
            // only ungeared/undetectable shamans get the squishy warning.
            Item const* shamanOffhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            ItemPrototype const* shamanOffProto = shamanOffhand ? shamanOffhand->GetProto() : nullptr;
            bool shamanShield = shamanOffProto && shamanOffProto->Class == ITEM_CLASS_ARMOR &&
                shamanOffProto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD;
            bool shamanTalented = bot->HasSpell(51382) || bot->HasSpell(51381) ||
                bot->HasSpell(45952) || bot->HasSpell(45951);
            if (!shamanShield || !shamanTalented)
                handler->PSendSysMessage("Warning: %s has no shaman tank gear/talents (shield + Rockbiter + Totemic Alignment/Spirit Armor) — will be squishy as tank.", name.c_str());
        }
        else
        {
            handler->PSendSysMessage("Warning: %s is not a traditional tank class — will be squishy as tank.", name.c_str());
        }
    }

    char const* label = role == static_cast<uint8>(ai::BOT_ROLE_TANK) ? "tank"
        : role == static_cast<uint8>(ai::BOT_ROLE_HEALER) ? "healer"
        : role == static_cast<uint8>(ai::BOT_ROLE_DPS) ? "dps" : "cleared";
    handler->PSendSysMessage("Bot %s role set to %s.", name.c_str(), label);
    return true;
}

bool HandleMatureCommand(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::string input = Trim(args ? args : "");
    size_t separator = input.find_first_of(" \t");
    if (!requester || separator == std::string::npos)
    {
        handler->PSendSysMessage("Usage: .bot command <botName> <Playerbot command>");
        return true;
    }

    std::string botName = input.substr(0, separator);
    std::string command = Trim(input.substr(separator + 1));
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string resolvedName;
    if (command.empty() || !ResolveOwnedBot(handler, botName.c_str(), bot, record, resolvedName))
    {
        handler->PSendSysMessage("You may only command an online bot on your account.");
        return true;
    }

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        // Staging controls must be applied now, not merely acknowledged as queued.
        if (command=="stay") return HandleStay(handler, botName.c_str());
        if (command.compare(0,4,"all ")==0 && command.substr(4)!="?")
        {
            std::string change=Trim(command.substr(4));
            if (!KnownStrategyChange({bot},change))
            {
                handler->PSendSysMessage("Unknown strategy change for %s: %s",resolvedName.c_str(),change.c_str());
                return true;
            }
            ai->ChangeStrategy(change,BotState::BOT_STATE_ALL);
            sPlayerbotDbStore.Save(ai);
            handler->PSendSysMessage("Applied to %s in all states: %s",resolvedName.c_str(),change.c_str());
            ai->PrintStrategies(requester,BotState::BOT_STATE_COMBAT);
            return true;
        }
        ai->HandleCommand(CHAT_MSG_WHISPER, command, *requester);
        handler->PSendSysMessage("Forwarded command for %s: %s; action success is not guaranteed.", resolvedName.c_str(), command.c_str());
    }
    else
        handler->PSendSysMessage("Bot %s has no AI yet.", resolvedName.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
bool HandleFollow(ChatHandler* handler, char const* args)
{
    ::Player* requester = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot follow <botName>");
        return true;
    }
    std::string name = args;
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid bot name.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        handler->PSendSysMessage("Bot '%s' not found.", name.c_str());
        return true;
    }
    ::ObjectGuid botGuid(HIGHGUID_PLAYER, data->uiGuid);
    ::ObjectGuid masterGuid = requester->GetObjectGuid();
    BotRecord* record = BotManager::Instance().FindBot(botGuid);
    if (!record || !CanControlBot(requester, record))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }
    // An older chat order still queued in the AI must not run after this one
    // (runtime/QueuedOrderPolicy.h).
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(botGuid))
        ai->DropQueuedMovementOrders();
    if (BotManager::Instance().SetBotFollow(botGuid, masterGuid))
        handler->PSendSysMessage("Bot %s now following %s.", name.c_str(), requester->GetName());
    else
        handler->PSendSysMessage("Bot %s could not enter follow mode; no success is reported.", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
bool HandlePullback(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    if (!requester->IsInWorld() || requester->IsBeingTeleported())
    {
        handler->PSendSysMessage("You must be in world to use pullback.");
        return true;
    }
    if (!requester->IsAlive() || requester->IsTaxiFlying())
    {
        handler->PSendSysMessage("You must be alive and not on a taxi to use pullback.");
        return true;
    }

    BotCommandContext context = BuildContext(requester);
    if (!context.enemyTarget)
    {
        handler->PSendSysMessage("Select a live non-bot target before using pullback.");
        return true;
    }

    // PullRequestAction owns target validation, the pull position, movement,
    // spell selection, return, and terminal cleanup. The command only finds a
    // player-controlled tank and asks that mature path to handle the request.
    Player* tank = ResolvePullExecutor(context);
    if (!tank)
    {
        handler->PSendSysMessage("No tank bot found in your party (needs a bot with tank role). Designate one with .bot role <name> tank, or target a bot to pull with it.");
        return true;
    }

    PlayerbotAI* tankAI = PlayerbotAIStorage::Instance().GetAI(tank);
    if (!tankAI)
    {
        handler->PSendSysMessage("Tank %s has no AI yet.", tank->GetName());
        return true;
    }
    std::string refuseCode;
    std::string refuseMessage;
    if (!CheckPullTarget(tank, context.enemyTarget, refuseCode, refuseMessage))
    {
        handler->PSendSysMessage("Tank %s cannot pull your target: %s", tank->GetName(), refuseMessage.c_str());
        return true;
    }


    BotRecord* record = BotManager::Instance().FindBot(tank->GetObjectGuid());
    if (!record || (record->masterGuid != requester->GetObjectGuid() &&
        !BotManager::Instance().BindBotMaster(tank->GetObjectGuid(), requester->GetObjectGuid())))
    {
        handler->PSendSysMessage("Tank %s could not be assigned to you for pullback.", tank->GetName());
        return true;
    }
    PullModeSnapshot pullModeBefore = SavePullMode(tankAI);
    if (!ConfigurePullMode(tankAI, true, sPlayerbotAIConfig.pullBackDpsJoinDelay))
    {
        handler->PSendSysMessage("Tank %s has no pullback strategy available.", tank->GetName());
        return true;
    }
    // Snapshot the target GUID like the action path does.
    ai::Event event("pullback", context.enemyTarget->GetObjectGuid(), requester);
    if (!ExecuteQuietAction(tankAI, "pull my target", event))
    {
        RestorePullMode(tankAI, pullModeBefore);
        handler->PSendSysMessage("Tank %s could not start a pull for your selected target.", tank->GetName());
        return true;
    }
    // Move the tank and hold the party only once the pull is accepted.
    RelaxTacticalMovement(tankAI);
    PausePartyDpsForPull(context, tank, sPlayerbotAIConfig.pullBackDpsJoinDelay, true);

    // Same as the .bot action path: do not let the tank's leftover action
    // clock delay the pull.
    tankAI->ClearActionClock();
    ExecuteQuietNextAction(tankAI, false);

    handler->PSendSysMessage("Pullback requested: tank %s is using its native pull strategy.", tank->GetName());
    WarnPartyStayAfterPull(handler, context, tank);
    return true;
}

bool HandleGearInspect(ChatHandler* handler, char const* /*args*/)
{
    Player* requester = Requester(handler);
    if (!requester) { handler->PSendSysMessage("Select your companion in game."); return true; }
    BotCommandContext context = BuildContext(requester);
    if (!context.selectedBot) { handler->PSendSysMessage("Select a controlled companion."); return true; }
    handler->PSendSysMessage("%s: %s", context.selectedBot->GetName(), InspectExperimentalBis(context.selectedBot).c_str());
    return true;
}

bool HandleGearBis(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester || !requester->GetSession() || requester->GetSession()->GetSecurity() < SEC_GAMEMASTER)
    { handler->PSendSysMessage("Experimental BiS provisioning requires an in-game GM."); return true; }
    if (requester->IsInCombat())
    { handler->PSendSysMessage("Leave combat before provisioning companions."); return true; }
    std::string name = Trim(args ? args : "");
    std::vector<Player*> bots;
    if (name == "all") bots = BotManager::Instance().GetBotsForMaster(requester->GetObjectGuid());
    else if (!name.empty() && normalizePlayerName(name))
    {
        Player* bot = sObjectAccessor.FindPlayerByName(name.c_str());
        if (bot) bots.push_back(bot);
    }
    else { handler->PSendSysMessage("Usage: .bot gearbis <Name|all>"); return true; }
    uint32 geared=0, skipped=0, failed=0;
    for (Player* bot : bots)
    {
        BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
        if (bot == requester || !record || record->masterGuid != requester->GetObjectGuid() ||
            !CanControlBot(requester, record) || !IsLiveHeadlessBot(bot, record) ||
            HireLifecycle::Instance().GetMaster(bot->GetObjectGuid()) != requester->GetObjectGuid())
        { ++skipped; handler->PSendSysMessage("%s: not your current hired companion; skipped.", bot->GetName()); continue; }
        std::string report;
        int status = ApplyExperimentalBis(bot, report);
        if (status > 0) ++geared; else if (status == 0) ++skipped; else ++failed;
        handler->PSendSysMessage("%s: %s", bot->GetName(), report.c_str());
    }
    if (bots.empty()) handler->PSendSysMessage("No matching online controlled companions found.");
    handler->PSendSysMessage("BiS: geared=%u skipped=%u failed=%u", geared, skipped, failed);
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
bool HandleSummon(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    if (!requester->IsInWorld() || requester->IsBeingTeleported())
    {
        handler->PSendSysMessage("You must be in world and not teleporting.");
        return true;
    }
    if (!requester->IsAlive())
    {
        handler->PSendSysMessage("You must be alive to summon.");
        return true;
    }
    if (requester->IsTaxiFlying())
    {
        handler->PSendSysMessage("You cannot summon while on a taxi.");
        return true;
    }
    // Explicit all scope ignores selection and includes only this master's ready bots.
    if (Trim(args ? args : "") == "all")
    {
        std::vector<std::string> names;
        for (Player* companion : BotManager::Instance().GetBotsForMaster(requester->GetObjectGuid()))
            if (companion)
                names.emplace_back(companion->GetName());
        if (names.empty())
            handler->PSendSysMessage("No currently controlled companions are ready to summon.");
        for (std::string const& companionName : names)
            HandleSummon(handler, companionName.c_str());
        return true;
    }
    std::string name = Trim(args ? args : "");
    if (name.empty() || !normalizePlayerName(name))
    {
        handler->PSendSysMessage("Usage: .bot summon <online bot name|all> (controlled companions only)");
        return true;
    }
    Player* bot = sObjectMgr.GetPlayer(name.c_str());
    if (!bot) bot = sObjectAccessor.FindPlayerByName(name.c_str());
    if (!bot)
    {
        handler->PSendSysMessage("Bot '%s' not found or not online.", name.c_str());
        return true;
    }
    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record || !CanControlBot(requester, record))
    {
        handler->PSendSysMessage("You may only summon a bot on your account.");
        return true;
    }
    if (!bot->GetSession() || !bot->GetSession()->IsHeadless() || !BotManager::Instance().IsBot(bot->GetObjectGuid()))
    {
        handler->PSendSysMessage("Character '%s' is not a module-owned Headless bot.", name.c_str());
        return true;
    }
    if (!bot->IsInWorld())
    {
        handler->PSendSysMessage("Bot '%s' is not in world.", name.c_str());
        return true;
    }
    if (!IsLiveHeadlessBot(bot, record))
    {
        handler->PSendSysMessage("Character '%s' has no ready PlayerbotAI and cannot be controlled.", name.c_str());
        return true;
    }
    if (!bot->IsAlive())
    {
        handler->PSendSysMessage("Bot '%s' is dead and cannot be summoned (resurrect first).", name.c_str());
        return true;
    }
    if (bot->IsBeingTeleported())
    {
        handler->PSendSysMessage("Bot '%s' is already teleporting.", name.c_str());
        return true;
    }
    if (bot->IsTaxiFlying())
    {
        handler->PSendSysMessage("Bot '%s' is on a taxi.", name.c_str());
        return true;
    }
    if (bot->IsInCombat())
    {
        handler->PSendSysMessage("Bot '%s' is in combat and cannot be summoned.", name.c_str());
        return true;
    }
    if (requester->GetMap() && bot->GetMap() && requester->GetMap() != bot->GetMap())
    {
        // Cross-map summon is allowed via TeleportTo, just warn
        handler->PSendSysMessage("Bot '%s' is on a different map; summon will teleport across maps.", name.c_str());
    }
    if (PlayerConvenience::Instance().IsBusy(bot->GetObjectGuid()))
    {
        handler->PSendSysMessage("Bot '%s' already has a pending convenience action.", name.c_str());
        return true;
    }
    if (record->masterGuid != requester->GetObjectGuid() &&
        !BotManager::Instance().BindBotMaster(bot->GetObjectGuid(), requester->GetObjectGuid()))
    {
        handler->PSendSysMessage("Bot '%s' could not be assigned to you for summon.", name.c_str());
        return true;
    }
    if (!PlayerConvenience::Instance().RequestSummon(requester, bot))
    {
        handler->PSendSysMessage("Failed to summon bot '%s' (already pending or error).", name.c_str());
        return true;
    }
    handler->PSendSysMessage("Summoning %s to a safe position near you (3s); it will follow on arrival.", name.c_str());
    return true;
}

static BehaviorToggle const BehaviorToggles[] = {
    { "loot",     "loot",          BotState::BOT_STATE_NON_COMBAT },
    { "aoe",      "no aoe",        BotState::BOT_STATE_COMBAT, true },
    { "autocc",   "auto cc",       BotState::BOT_STATE_COMBAT },
    { "savemana", "conserve mana", BotState::BOT_STATE_COMBAT },
    { "boost",    "boost",         BotState::BOT_STATE_COMBAT },
    { "threat",   "threat",        BotState::BOT_STATE_COMBAT },
    { "potions",  "potions",       BotState::BOT_STATE_REACTION },
    { "mimic",    "mimic consumables", BotState::BOT_STATE_NON_COMBAT },
    { "spread",   "spread",        BotState::BOT_STATE_COMBAT },
};

BehaviorToggle const* FindBehaviorToggle(std::string const& key)
{
    for (BehaviorToggle const& toggle : BehaviorToggles)
        if (key == toggle.key)
            return &toggle;
    return nullptr;
}

bool IsBehaviorOn(PlayerbotAI* ai, BehaviorToggle const& toggle)
{
    return ai->HasStrategy(toggle.strategy, toggle.state) != toggle.inverted;
}

void SetBehavior(PlayerbotAI* ai, BehaviorToggle const& toggle, bool enable)
{
    ai->ChangeStrategy((enable != toggle.inverted ? "+" : "-") + std::string(toggle.strategy), toggle.state);
}

// "TBM:BOTSTATE|<name>|move=follow,loot=on,..." - one line per live bot.
// Flee parks a bot in passive follow, reported as its own movement mode so
// the addon can show that the bot is holding off until the next order.
void SendBotStateLine(ChatHandler* handler, Player* bot)
{
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!handler || !ai)
        return;
    std::string movement = MovementName(ai);
    if (ai->HasStrategy("passive", BotState::BOT_STATE_NON_COMBAT))
        movement = "flee";
    std::string fields = "move=" + movement;
    for (BehaviorToggle const& toggle : BehaviorToggles)
        fields += std::string(",") + toggle.key + "=" + (IsBehaviorOn(ai, toggle) ? "on" : "off");
    handler->PSendSysMessage("TBM:BOTSTATE|%s|%s", ProtocolSafe(bot->GetName()).c_str(), fields.c_str());
}

// .bot behavior <bot|party|raid> <key> <on|off> - allowlisted per-bot
// strategy toggles (BehaviorToggles). Persisted through the mature AI store
// like loot/auto cc; answers with an ACK and the bot's fresh TBM:BOTSTATE
// line. "party"/"raid" fans out to every live owned bot in the scope.
bool HandleBehavior(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::istringstream input(Trim(args ? args : ""));
    std::string botName;
    std::string key;
    std::string mode;
    input >> botName >> key >> mode;
    for (char& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : mode)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string loweredBot = botName;
    for (char& c : loweredBot)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string const intent = "behavior " + (key.empty() ? std::string("?") : key);
    BehaviorToggle const* toggle = FindBehaviorToggle(key);
    if (!toggle || (mode != "on" && mode != "off"))
    {
        SendActionError(handler, intent, "invalid",
            "Usage: .bot behavior <bot|party|raid> <loot|aoe|autocc|savemana|boost|threat|potions|mimic|spread> <on|off>");
        return true;
    }

    bool const enable = mode == "on";
    if (loweredBot == "party" || loweredBot == "raid")
    {
        if (!requester)
            return true;
        BotCommandContext context = BuildContext(requester);
        std::vector<Player*> scope = ResolveDynamicScope(context);
        if (scope.empty())
        {
            SendActionError(handler, intent, "no-bot", "No live owned party bots are controllable.");
            return true;
        }
        uint32_t succeeded = 0;
        for (Player* bot : scope)
        {
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai)
                continue;
            SetBehavior(ai, *toggle, enable);
            sPlayerbotDbStore.Save(ai);
            if (IsBehaviorOn(ai, *toggle) == enable)
                ++succeeded;
            SendBotStateLine(handler, bot);
        }
        // The addon accepts only "party" or "bot:<name>" as ACK scope; the
        // mode (on/off) is the detail field.
        SendActionAck(handler, intent, "party", succeeded, enable ? "on" : "off");
        return true;
    }

    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    PlayerbotAI* ai = nullptr;
    if (!requester || !ResolveOwnedBot(handler, botName.c_str(), bot, record, name) ||
        !(ai = PlayerbotAIStorage::Instance().GetAI(bot)))
    {
        SendActionError(handler, intent, "no-bot", "Unknown or uncontrollable bot name.");
        return true;
    }

    SetBehavior(ai, *toggle, enable);
    sPlayerbotDbStore.Save(ai);
    bool const now = IsBehaviorOn(ai, *toggle);
    if (now != enable)
        SendActionError(handler, intent, "failed", "This bot does not support that behaviour.");
    else
        SendActionAck(handler, intent, "bot:" + std::string(bot->GetName()), 1, now ? "on" : "off");
    SendBotStateLine(handler, bot);
    return true;
}


} // namespace BotCommands
} // namespace TortoiseBots
