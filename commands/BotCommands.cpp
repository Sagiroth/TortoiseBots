// pi-lens-ignore-file: all
#include "BotCommands.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireSpecPolicy.h"
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
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/strategy/generic/PullStrategy.h"
#include "../ai/playerbot/strategy/values/RtiTargetValue.h"
#include "../ai/playerbot/strategy/values/AttackersValue.h"
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
#include "../runtime/RemoteCommanderView.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Value.h"
#include "../ai/playerbot/GuidPosition.h"
#include "../host/ModuleLog.h"
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
#include <memory>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
// Lens fallback stubs — core headers absent in static analysis. Real build uses
// the mangosd headers via -I src/game. Guards use the same macros as the
// real headers so the stubs are never active in a real build.
#ifndef MANGOSSERVER_CHAT_H
class WorldSession;
class ChatHandler {
public:
    void PSendSysMessage(const char* fmt, ...) {}
    void SendSysMessage(const char*) {}
    WorldSession* GetSession() const { return nullptr; }
};
#endif
#ifndef _OBJECTMGR_H
struct PlayerCacheData { uint32_t uiGuid; uint32_t uiAccount; };
struct LensOM { PlayerCacheData* GetPlayerDataByName(const std::string&) { return nullptr; } PlayerCacheData* GetPlayerDataByGUID(uint32_t) { return nullptr; } Player* GetPlayer(const char*) { return nullptr; } };
static LensOM sObjectMgr;
inline bool normalizePlayerName(std::string&, size_t = 32, bool = true) { return true; }
#endif
#ifndef MANGOS_OBJECTACCESSOR_H
class Player;
struct LensOA { Player* FindPlayerByName(const char*) { return nullptr; } Player* FindPlayer(ObjectGuid) { return nullptr; } };
static LensOA sObjectAccessor;
#endif
#ifndef __UNIT_H
using uint32 = uint32_t;
constexpr int SEC_GAMEMASTER = 3;
#ifndef OBJECT_GUID_H
struct ObjectGuid { uint32 GetCounter() const { return 0; } };
#endif
#ifndef HIGHGUID_PLAYER
constexpr int HIGHGUID_PLAYER = 0;
#endif
constexpr int CHAT_MSG_WHISPER = 1;
class WorldPacket { public: WorldPacket() {} template<typename T> WorldPacket& operator<<(T const&) { return *this; } };
class Player {
public:
    ObjectGuid GetObjectGuid() const { return ObjectGuid(); }
    const char* GetName() const { return ""; }
    WorldSession* GetSession() const { return nullptr; }
    void* GetGroupInvite() const { return nullptr; }
    bool IsInSameGroupWith(Player*) const { return false; }
};
class WorldSession {
public:
    Player* GetPlayer() const { return nullptr; }
    int GetSecurity() const { return 0; }
    uint32 GetAccountId() const { return 0; }
    bool IsHeadless() const { return false; }
    void HandleGroupInviteOpcode(WorldPacket&) {}
    void HandleGroupUninviteOpcode(WorldPacket&) {}
};
#endif
#ifndef MANGOSSERVER_LOG_H
struct Log { void outString(const char*, ...) {} void outError(const char*, ...) {} };
static Log sLog;
#endif

namespace TortoiseBots {
namespace BotCommands {

static bool IsRaidTargetMark(std::string const& mark);
static std::string CurrentCcMark(PlayerbotAI* ai);
static bool HandleRaidAction(ChatHandler* handler, BotCommandContext const& context, Player* requester, std::string const& intent);
static bool ExecuteInterruptAction(ChatHandler* handler, BotCommandContext const& context, Player* requester);

static std::string Trim(std::string value)
{
    size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

static Player* Requester(ChatHandler* handler)
{
    return handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
}


static bool ResolveOwnedBot(ChatHandler* handler, char const* args, Player*& bot, BotRecord*& record, std::string& name)
{
    bot = nullptr;
    record = nullptr;
    name = Trim(args ? args : "");
    if (name.empty() || !normalizePlayerName(name))
        return false;

    bot = sObjectMgr.GetPlayer(name.c_str());
    if (!bot)
        bot = sObjectAccessor.FindPlayerByName(name.c_str());
    if (!bot)
        return false;

    record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!CanControlBot(Requester(handler), record))
        return false;

    return IsLiveHeadlessBot(bot, record);
}

static bool HandleList(ChatHandler* handler)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    uint32 shown = 0;
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        BotRecord* record = bot ? BotManager::Instance().FindBot(bot->GetObjectGuid()) : nullptr;
        if (!bot || !record || !CanControlBot(requester, record))
            continue;

        ++shown;
        handler->PSendSysMessage("%s: %s, random %u, AI %u",
            bot->GetName(), record->enteredWorld ? "in world" : "starting",
            record->random ? 1 : 0,
            PlayerbotAIStorage::Instance().GetAI(bot) ? 1 : 0);
    }

    if (!shown)
        handler->PSendSysMessage("No owned PlayerBots are online.");
    return true;
}

static bool HandleStats(ChatHandler* handler)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    uint32 total = 0;
    uint32 random = 0;
    uint32 withAi = 0;
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        BotRecord* record = bot ? BotManager::Instance().FindBot(bot->GetObjectGuid()) : nullptr;
        if (!bot || !record || !CanControlBot(requester, record))
            continue;
        ++total;
        random += record->random ? 1 : 0;
        withAi += PlayerbotAIStorage::Instance().GetAI(bot) ? 1 : 0;
    }
    handler->PSendSysMessage("Owned PlayerBots: %u online, %u random, %u with AI.", total, random, withAi);
    return true;
}

static char const* LifecycleName(BotLifecycle lifecycle)
{
    switch (lifecycle)
    {
        case BotLifecycle::PendingAdd: return "starting";
        case BotLifecycle::InWorld: return "in world";
        case BotLifecycle::Removing: return "removing";
    }

    return "unknown";
}

static char const* MovementName(PlayerbotAI* ai)
{
    if (!ai)
        return "unavailable";
    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT))
        return "follow";
    if (ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
        return "stay";
    if (ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
        return "guard";
    if (ai->HasStrategy("free", BotState::BOT_STATE_NON_COMBAT))
        return "free";
    return "custom";
}

static bool HandleStatus(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot status <online bot name> (same account only)");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    handler->PSendSysMessage("%s: %s, AI %u, movement %s, random %u, owner %s.",
        name.c_str(), LifecycleName(record->lifecycle), ai ? 1u : 0u,
        MovementName(ai), record->random ? 1u : 0u,
        record->masterGuid == requester->GetObjectGuid() ? "you" : "another player");
    return true;
}

static bool HandleVersion(ChatHandler* handler)
{
    // Any player may ask; the version is public build metadata, not control.
    handler->PSendSysMessage("TortoiseBots %s", BuildVersion().c_str());
    handler->PSendSysMessage("TBM:VERSION|%s", BuildVersion().c_str());
    handler->PSendSysMessage("%s", AttributionLine().c_str());
    return true;
}

static bool HandleLease(ChatHandler* handler, char const* args)
{
    std::string sub = Trim(args ? args : "");
    for (char& c : sub) c = tolower(c);
    if (!sub.empty() && sub != "status")
    {
        handler->PSendSysMessage("Usage: .bot lease [status]");
        return true;
    }
    uint32_t grinding, trading, lft, bg, master;
    BotActivityLeaseManager::Instance().GetActivityCounts(grinding, trading, lft, bg, master);
    uint32_t tracked = grinding + trading + lft + bg + master;
    uint32_t online = BotManager::Instance().GetBotCount();
    uint32_t idle = online > tracked ? online - tracked : 0;
    handler->PSendSysMessage("Leases: Idle %u, Grinding %u, Trading %u, LftQueued %u, BgQueued %u, PlayerMaster %u (online %u, tracked %u).",
        idle, grinding, trading, lft, bg, master, online, tracked);

    std::vector<ActivityLeaseInfo> leases = BotActivityLeaseManager::Instance().GetActiveLeases();
    for (ActivityLeaseInfo const& info : leases)
    {
        uint32_t remaining = BotActivityLeaseManager::Instance().GetRemainingMs(info.guidLow);
        if (info.lease.maxDurationMs)
            handler->PSendSysMessage("Lease %u: %s, %u ms remaining.", info.guidLow,
                BotActivityName(info.lease.activity), remaining);
        else
            handler->PSendSysMessage("Lease %u: %s, indefinite.", info.guidLow,
                BotActivityName(info.lease.activity));
    }
    return true;
}


static bool HandleInvite(ChatHandler* handler, char const* args)
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

static bool HandleUninvite(ChatHandler* handler, char const* args)
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
static bool HandleInterrupt(ChatHandler* handler, char const* args)
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

// Direct strategy toggle: `.bot strategy <+|-name> [bot]` fans out over the
// dynamic scope (selected bot or owned party bots), mirroring `.bot loot`.
// Thin wrapper over the mature ChangeStrategy path; validation and
// persistence stay AI-owned via ChangeStrategyAction/DbStore.
static bool HandleStrategy(ChatHandler* handler, char const* args)
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
        handler->PSendSysMessage("Usage: .bot strategy [+/-/~]<strategy> [bot] (e.g. .bot strategy +loot, .bot strategy -passive)");
        return true;
    }
    size_t separator = input.find_first_of(" \t");
    std::string change = separator == std::string::npos ? input : Trim(input.substr(0, separator));
    std::string botToken = separator == std::string::npos ? std::string() : Trim(input.substr(separator + 1));
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope;
    if (!botToken.empty())
    {
        Player* bot = nullptr;
        BotRecord* record = nullptr;
        std::string name;
        if (!ResolveOwnedBot(handler, botToken.c_str(), bot, record, name))
        {
            handler->PSendSysMessage("You may only control an online bot on your account.");
            return true;
        }
        scope.push_back(bot);
    }
    else
        scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No live owned party bots are controllable.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        ai->ChangeStrategy(change, BotState::BOT_STATE_ALL);
        sPlayerbotDbStore.Save(ai);
        ++succeeded;
    }
    handler->PSendSysMessage("Strategy change '%s' applied to %u bot(s).", change.c_str(), succeeded);
    return true;
}

static bool HandleStay(ChatHandler* handler, char const* args)
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

static void RelaxTacticalMovement(PlayerbotAI* ai);

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

// `.bot action flee` parks bots in passive follow; an explicit tactical order
// is the owner calling them back into the fight.
static void EndFlee(PlayerbotAI* ai)
{
    if (!ai)
        return;
    if (ai->HasStrategy("passive", BotState::BOT_STATE_NON_COMBAT))
        ai->ChangeStrategy("-passive", BotState::BOT_STATE_NON_COMBAT);
    if (ai->HasStrategy("passive", BotState::BOT_STATE_COMBAT))
        ai->ChangeStrategy("-passive", BotState::BOT_STATE_COMBAT);
}

// Tactical requests must be able to leave a prior stay/follow hold. The
// movement and combat behavior itself remains owned by PlayerbotAI.
static void RelaxTacticalMovement(PlayerbotAI* ai)
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
static bool CheckPullTarget(Player* executor, Unit* target, std::string& code, std::string& message)
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

static PullModeSnapshot SavePullMode(PlayerbotAI* ai)
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

static void RestorePullMode(PlayerbotAI* ai, PullModeSnapshot const& snapshot)
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

static bool BindMovementMaster(Player* requester, Player* bot)
{
    if (!requester || !bot)
        return false;

    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record)
        return false;

    // Rebind even when the durable GUID already matches: the live AI pointer
    // may have been cleared by a reconnect, and FollowTargetValue caches the
    // old pointer until its next calculation.
    return BotManager::Instance().BindBotMaster(bot->GetObjectGuid(), requester->GetObjectGuid());
}

static bool HandleGuard(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "guard", "guard chat shortcut", "will guard this position", false, true);
}

static bool HandleFree(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "free", "free chat shortcut", "is free to move", false, true);
}

static bool HandleReady(ChatHandler* handler, char const* args)
{
    return HandleNamedAction(handler, args, "ready", "ready check", "will run a readiness check");
}

static bool HandleAttack(ChatHandler* handler, char const* args)
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

static bool HandleFormation(ChatHandler* handler, char const* args)
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
static bool HandleRole(ChatHandler* handler, char const* args)
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
        ai->ChangeStrategy("+protection,+tank feral,+tank assist", BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+protection,+tank feral,+tank assist,+pull,+pull back,+close",
            BotState::BOT_STATE_COMBAT);
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


static bool HandleMatureCommand(ChatHandler* handler, char const* args)
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
        ai->HandleCommand(CHAT_MSG_WHISPER, command, *requester);
        handler->PSendSysMessage("Forwarded command for %s: %s; action success is not guaranteed.", resolvedName.c_str(), command.c_str());
    }
    else
        handler->PSendSysMessage("Bot %s has no AI yet.", resolvedName.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
// Issue #192: fast-path companion hire. Same service, costs, and caps as the
// <Mercenary Hire> gossip wizard, but only available when the server sets
// AiPlayerbot.HireAnywhere; otherwise it points players to a recruiter. Usage:
//   .bot hire <class> [dps|tank|healer] [race] [male|female] [tier]
// Tokens after the class may come in any order: "warrior tauren male tank",
// "mage female frost", and "paladin protection" all parse. Bare spec words
// (protection/arms/fury/holy/retribution/...) imply their role. The optional
// tier word (t<N>, tier<N>, or the tier name like mc) picks the equipment
// tier up to the player's highest unlock (default tier 0, Standard kit).
static bool HandleHire(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to hire a companion.");
        return true;
    }
    std::string input = Trim(args ? args : "");
    if (input.empty())
    {
        handler->PSendSysMessage("Usage: .bot hire <class> [dps/tank/healer] [race] [male/female] [tier]");
        return true;
    }
    // Split on whitespace; class/race/gender/role/spec/tier tokens in any order.
    std::vector<std::string> tokens;
    {
        std::istringstream stream(input);
        std::string token;
        while (stream >> token)
            tokens.push_back(token);
    }
    HireSelection sel;
    // Tokens that are not class/race/gender/role words: resolved against the
    // class after the loop, so "frost mage" works in either order.
    std::vector<std::string> specWords;
    for (std::string raw : tokens)
    {
        std::string token = raw;
        for (char& c : token)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!sel.classId)
        {
            uint32 parsed = ai::ChatHelper::parseClass(token);
            if (parsed >= CLASS_WARRIOR && parsed <= CLASS_DRUID && parsed != 6 && parsed != 10)
            {
                sel.classId = static_cast<uint8>(parsed);
                continue;
            }
        }
        if (!sel.race)
        {
            uint32 parsed = ai::ChatHelper::parseRace(token);
            if (parsed >= RACE_HUMAN && parsed <= RACE_HIGH_ELF)
            {
                sel.race = static_cast<uint8>(parsed);
                continue;
            }
        }
        if (!sel.gender)
        {
            uint32 parsed = ai::ChatHelper::parseGender(token);
            if (parsed == GENDER_MALE || parsed == GENDER_FEMALE)
            {
                sel.gender = static_cast<uint8>(parsed);
                continue;
            }
        }
        // Issue #490 PR 2: optional tier word (t<N>/tier<N>/tier name like
        // mc) in any position, same any-order form as the other words.
        // Before role/spec so a tier name never reaches the spec matcher.
        uint8_t tierWord = 0;
        if (ParseHireTierWord(token, tierWord))
        {
            sel.gearTier = tierWord;
            continue;
        }
        if (ai::ChatHelper::parseRole(token) == ai::BOT_ROLE_NONE)
            specWords.push_back(token);
        if (!sel.role)
        {
            ai::BotRoles parsed = ai::ChatHelper::parseRole(token);
            if (parsed != ai::BOT_ROLE_NONE)
            {
                sel.role = static_cast<uint8>(parsed);
                continue;
            }
            // Bare spec words imply their role (protection -> tank, holy ->
            // healer). Match the gossip labels loosely: any token containing
            // the spec stem counts ("prot", "ret", "feral bear"). Feral Cat
            // stays DPS on the shared feral path (specIndex 3); other feral
            // words keep the Bear tank default (specIndex 0).
            std::string lower = token;
            bool catWord = lower.find("cat") != std::string::npos;
            bool tankWord = !catWord && (lower.find("prot") != std::string::npos || lower.find("tank") != std::string::npos ||
                lower.find("bear") != std::string::npos || lower.find("feral") != std::string::npos);
            bool healWord = lower.find("holy") != std::string::npos || lower.find("heal") != std::string::npos ||
                lower.find("resto") != std::string::npos || lower.find("discipline") != std::string::npos ||
                lower.find("disc") != std::string::npos;
            if (tankWord)
            {
                sel.role = static_cast<uint8>(ai::BOT_ROLE_TANK);
                if (sel.classId == CLASS_DRUID)
                    sel.specIndex = 0;
                continue;
            }
            if (healWord)
            {
                sel.role = static_cast<uint8>(ai::BOT_ROLE_HEALER);
                if (sel.classId == CLASS_DRUID)
                    sel.specIndex = 1;
                continue;
            }
            bool balanceWord = lower.find("balance") != std::string::npos;
            if (lower.find("dps") != std::string::npos || lower.find("arms") != std::string::npos ||
                lower.find("fury") != std::string::npos || lower.find("shadow") != std::string::npos ||
                lower.find("ret") != std::string::npos || balanceWord ||
                lower.find("frost") != std::string::npos || lower.find("fire") != std::string::npos ||
                lower.find("arcane") != std::string::npos || lower.find("afflic") != std::string::npos ||
                lower.find("demon") != std::string::npos || lower.find("destro") != std::string::npos ||
                lower.find("assassin") != std::string::npos || lower.find("combat") != std::string::npos ||
                lower.find("subtle") != std::string::npos || lower.find("beast") != std::string::npos ||
                lower.find("marks") != std::string::npos || lower.find("surv") != std::string::npos ||
                lower.find("elem") != std::string::npos || lower.find("enhance") != std::string::npos ||
                catWord)
            {
                sel.role = static_cast<uint8>(ai::BOT_ROLE_DPS);
                if (sel.classId == CLASS_DRUID)
                    sel.specIndex = catWord ? 3 : balanceWord ? 2 : -1;
                continue;
            }
        }
    }
    if (!sel.classId)
    {
        handler->PSendSysMessage("Usage: .bot hire <class> [dps/tank/healer] [race] [male/female] [tier]");
        return true;
    }
    // Issue #490 PR 2: a tier above the player's unlock fails fast with the
    // highest available tier named (Hire() re-validates against stale state).
    // Below 60 or with tiers disabled the tier is ignored, so skip the lookup.
    if (sel.gearTier > 0 && sPlayerbotAIConfig.hireGearTiersEnabled &&
        requester->GetLevel() == kHireGearHireLevel)
    {
        uint8_t highest = HireProvisionService::Instance().HighestUnlockedHireTierFor(requester);
        if (sel.gearTier > highest)
        {
            handler->PSendSysMessage("That equipment is not unlocked yet. Your highest available tier is %s (tier %u).",
                HireTierKitName(highest).c_str(), highest);
            return true;
        }
    }
    // A spec word picks the recruiter's spec option for every class, not only
    // druids: without a specIndex provisioning draws any premade tree of the
    // role, so "mage frost" could arrive Arcane. The spec word's role wins, as
    // in the gossip menu. Druids keep the bear/cat handling above.
    if (sel.classId != CLASS_DRUID)
    {
        uint32 specCount = 0;
        HireSpecOption const* specs = HireSpecOptions(sel.classId, specCount);
        for (std::string const& word : specWords)
        {
            int specIndex = HireSpecIndexForWord(sel.classId, word);
            if (specs && specIndex >= 0 && static_cast<uint32>(specIndex) < specCount)
            {
                sel.specIndex = specIndex;
                sel.role = specs[specIndex].role;
                break;
            }
        }
    }
    // Defaults: requester's faction race pool, random gender, class role.
    if (!sel.race)
    {
        Team team = requester->GetTeam();
        std::vector<uint8> pool;
        for (uint32 race = RACE_HUMAN; race <= RACE_HIGH_ELF; ++race)
        {
            if (Player::TeamForRace(race) != team)
                continue;
            // GetPlayerInfo is the authoritative class/race contract.
            if (!sObjectMgr.GetPlayerInfo(race, sel.classId))
                continue;
            pool.push_back(static_cast<uint8>(race));
        }
        if (pool.empty())
        {
            handler->PSendSysMessage("No race of your faction can be that class.");
            return true;
        }
        sel.race = pool[urand(0, uint32(pool.size() - 1))];
    }
    if (!sel.gender)
        sel.gender = urand(0, 1) ? GENDER_FEMALE : GENDER_MALE;
    if (!sel.role)
        sel.role = HireProvisionService::DefaultRoleForClass(sel.classId);
    if (sel.classId == CLASS_DRUID && sel.specIndex < 0 && sel.role == static_cast<uint8>(ai::BOT_ROLE_DPS))
        sel.specIndex = 3;
    HireOutcome outcome = HireProvisionService::Instance().Hire(requester, sel, false);
    handler->PSendSysMessage("%s", outcome.message.c_str());
    return true;
}

static bool HandleAdd(ChatHandler* handler, char const* args)
{
    if (!handler)
        return false;
    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot add <characterName>");
        return true;
    }

    std::string name = args;
    // Trim
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);

    if (name.empty())
    {
        handler->PSendSysMessage("Usage: .bot add <characterName>");
        return true;
    }

    // Normalize name
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid character name.");
        return true;
    }

    ::Player* requester = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to add a bot.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        // Try online lookup
        if (::Player* online = sObjectAccessor.FindPlayerByName(name.c_str()))
            data = sObjectMgr.GetPlayerDataByGUID(online->GetObjectGuid().GetCounter());
    }

    if (!data)
    {
        handler->PSendSysMessage("Character '%s' not found.", name.c_str());
        return true;
    }

    ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
    uint32_t accountId = data->uiAccount;
    ::ObjectGuid masterGuid = requester->GetObjectGuid();

    if (sObjectAccessor.FindPlayer(guid))
    {
        handler->PSendSysMessage("Character '%s' is already online and cannot be claimed as a Headless bot.", name.c_str());
        return true;
    }

    if (!requester->GetSession() ||
        (accountId != requester->GetSession()->GetAccountId() && requester->GetSession()->GetSecurity() < SEC_GAMEMASTER))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }

    uint32 ownerAccountId = requester->GetSession()->GetAccountId();
    OwnedCharacter existingOwnership;
    if (BotManager::Instance().GetOwnedCharacter(guid, existingOwnership) &&
        existingOwnership.ownerAccountId != ownerAccountId &&
        !IsBotAdministrator(requester))
    {
        handler->PSendSysMessage("Character '%s' is already owned by another account.", name.c_str());
        return true;
    }

    if (BotManager::Instance().AddBotWithMaster(accountId, guid, masterGuid))
    {
        if (!BotManager::Instance().RegisterOwnedCharacter(
            ownerAccountId, accountId, guid, masterGuid))
        {
            // Do not leave an unowned runtime after a failed durable write.
            BotManager::Instance().RemoveBot(guid, false);
            handler->PSendSysMessage("Failed to persist ownership for bot %s; login was cancelled.", name.c_str());
            return true;
        }
        handler->PSendSysMessage("Bot %s queued for login; it will follow %s after entering the world.",
            name.c_str(), requester->GetName());
    }
    else
        handler->PSendSysMessage("Failed to add bot %s (already exists or error).", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
static bool HandleRemove(ChatHandler* handler, char const* args)
{
    if (!handler)
        return false;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to remove a bot.");
        return true;
    }
    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot remove <characterName>");
        return true;
    }
    std::string name = args;
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid character name.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        handler->PSendSysMessage("Character '%s' not found.", name.c_str());
        return true;
    }
    ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
    BotRecord* record = BotManager::Instance().FindBot(guid);
    if (!record)
    {
        handler->PSendSysMessage("Character '%s' is not a module-owned bot.", name.c_str());
        return true;
    }
    if (!CanControlBot(requester, record))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }
    if (HireLifecycle::Instance().IsHired(guid))
    {
        // Issue #192 follow-up: a hired companion is temporary. An explicit
        // removal ends the hire now and deletes its character; the fee is not
        // refunded and the next hire is a new character.
        HireLifecycle::Instance().DismissNow(guid, "removed by master");
        handler->PSendSysMessage("Hire ended for %s; its character is being deleted.", name.c_str());
        return true;
    }
    if (BotManager::Instance().RemoveBot(guid, true))
        handler->PSendSysMessage("Removal requested for bot %s; Headless cleanup completes asynchronously.", name.c_str());
    else
        handler->PSendSysMessage("Bot %s not found or not removable.", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
static bool HandleFollow(ChatHandler* handler, char const* args)
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

// Companion maintenance: loot toggling plus vendor sell/repair fan-out over
// the dynamic scope (selected bot or all owned party bots). The mature chat
// actions own vendor checks (proximity, gossip flags, gold); this layer only
// resolves scope, invokes them, persists strategy state, and reports.
static bool HandleRaidAction(ChatHandler* handler, BotCommandContext const& context, Player* requester, std::string const& intent);
static bool HandleLoot(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    std::string mode = Trim(args ? args : "");
    for (char& c : mode)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!mode.empty() && mode != "on" && mode != "off")
    {
        handler->PSendSysMessage("Usage: .bot loot [on/off]");
        return true;
    }
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
        if (!ai)
            continue;
        bool enabled = mode.empty() ? !ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT) : mode == "on";
        ai->ChangeStrategy(enabled ? "+loot" : "-loot", BotState::BOT_STATE_NON_COMBAT);
        sPlayerbotDbStore.Save(ai);
        ++succeeded;
        if (context.selectedBot && scope.size() == 1)
            handler->PSendSysMessage("Bot %s: loot strategy %s.", bot->GetName(), enabled ? "enabled" : "disabled");
    }
    if (!context.selectedBot || scope.size() != 1)
        handler->PSendSysMessage("Loot strategy toggled for %u bot(s).", succeeded);
    return true;
}

static bool HandleRepair(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
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
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "repair", ai::Event("bot command", "repair", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u bot(s) to repair equipment.", succeeded);
    return true;
}

static bool HandleSell(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
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
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "sell", ai::Event("bot command", "sell", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u bot(s) to sell vendor trash.", succeeded);
    return true;
}

// Pre-combat rest: order scoped bots to sit and consume food/drink until
// full, combat, or the master moves away. The mature food/drink actions own
// inventory, sit state, and regen checks; this layer only fans out and
// reports over the dynamic scope.
static bool HandleRest(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
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
        if (!ai)
            continue;
        bool rested = false;
        if (bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA))
            rested = ExecuteQuietAction(ai, "drink", ai::Event("bot command", "drink", requester)) || rested;
        if (bot->GetHealth() < bot->GetMaxHealth())
            rested = ExecuteQuietAction(ai, "food", ai::Event("bot command", "food", requester)) || rested;
        if (rested)
            ++succeeded;
        else if (bot->GetHealth() >= bot->GetMaxHealth() && (bot->GetPowerType() != POWER_MANA || bot->GetPower(POWER_MANA) >= bot->GetMaxPower(POWER_MANA)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u companion bot(s) to rest and restore health/mana.", succeeded);
    return true;
}

static std::vector<Player*> DeadBotsInScope(BotCommandContext const& context)
{
    std::vector<Player*> scope = ResolveDynamicScope(context);
    std::vector<Player*> dead;
    for (Player* bot : scope)
        if (bot && !bot->IsAlive())
            dead.push_back(bot);
    return dead;
}

static bool HandleRelease(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = DeadBotsInScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No dead companion bots found.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "release", ai::Event("bot command", "release", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Released spirit for %u companion bot(s).", succeeded);
    return true;
}

static bool HandleCorpseRun(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = DeadBotsInScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No dead companion bots found.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "corpse run", ai::Event("bot command", "corpse run", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u companion bot(s) to run back to corpse.", succeeded);
    return true;
}

static bool HandleLearn(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
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
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "trainer", ai::Event("bot command", "learn", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u companion bot(s) to learn spells from nearby trainers.", succeeded);
    return true;
}

static bool HandleTrade(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    Player* bot = context.selectedBot;
    if (!bot || !bot->IsAlive())
    {
        handler->PSendSysMessage("Target an alive companion bot to trade.");
        return true;
    }
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", bot->GetName());
        return true;
    }
    if (ExecuteQuietAction(ai, "trade", ai::Event("bot command", requester->GetName(), requester)))
        handler->PSendSysMessage("Trade requested with %s.", bot->GetName());
    else
        handler->PSendSysMessage("Bot %s could not open trade.", bot->GetName());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
static bool HandlePullback(ChatHandler* handler, char const* args)
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
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
static bool HandleSummon(ChatHandler* handler, char const* args)
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
    std::string name = Trim(args ? args : "");
    if (name.empty() || !normalizePlayerName(name))
    {
        handler->PSendSysMessage("Usage: .bot summon <online bot name> (same account only)");
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
static std::string ProtocolSafe(std::string value)
{
    for (char& character : value)
    {
        if (character == '|' || character == '\r' || character == '\n')
            character = ' ';
    }
    return value;
}

static void SendActionError(ChatHandler* handler, std::string const& intent,
    char const* code, char const* message)
{
    if (!handler)
        return;
    std::string safeMessage = ProtocolSafe(message ? message : "request rejected");
    handler->PSendSysMessage("TBM:ACTION_ERR|%s|%s|%s", ProtocolSafe(intent).c_str(),
        code ? code : "rejected", safeMessage.c_str());
}

static void SendActionAck(ChatHandler* handler, std::string const& intent,
    std::string const& scope, uint32 count, std::string const& executor = {})
{
    if (!handler)
        return;
    std::string safeScope = ProtocolSafe(scope);
    std::string safeExecutor = executor.empty() ? "-" : ProtocolSafe(executor);
    handler->PSendSysMessage("TBM:ACTION_ACK|%s|%s|%u|%s",
        ProtocolSafe(intent).c_str(), safeScope.c_str(), count, safeExecutor.c_str());
}

static bool ExecuteInterruptAction(ChatHandler* handler, BotCommandContext const& context,
    Player* requester)
{
    Unit* target = context.enemyTarget;
    if (!target)
    {
        SendActionError(handler, "interrupt", "no-target", "Select a live enemy target first.");
        return true;
    }

    if (!target->IsNonMeleeSpellCasted(true))
    {
        SendActionError(handler, "interrupt", "not-casting", "The target is not casting an interruptible spell.");
        return true;
    }

    std::string actionName;
    Player* executor = ResolveInterruptExecutor(context, target, &actionName);
    if (!executor || actionName.empty())
    {
        SendActionError(handler, "interrupt", "no-interrupt",
            "No owned bot has a ready interrupt for this target.");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(executor);
    if (!ai)
    {
        SendActionError(handler, "interrupt", "no-ai", "The selected interrupt bot has no AI.");
        return true;
    }

    // Interrupt is an executor command, so break a stale hold/follow mode while
    // leaving movement and combat decisions to the mature action graph.
    RelaxTacticalMovement(ai);
    executor->SetSelectionGuid(target->GetObjectGuid());
    ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(target);

    ai::Event event("action interrupt", "", requester);
    bool accepted = ExecuteQuietAction(ai, actionName, event);
    if (!accepted)
    {
        if (!target->IsInWorld() || !target->IsAlive())
        {
            SendActionError(handler, "interrupt", "no-target",
                "The selected interrupt target is no longer available.");
            return true;
        }
        if (!target->IsNonMeleeSpellCasted(true))
        {
            SendActionError(handler, "interrupt", "not-casting",
                "The target finished casting before the interrupt could start.");
            return true;
        }

        // Direct execution deliberately bypasses prerequisites. Queue the
        // same mature action on the active engine instead, allowing its
        // existing reach prerequisite to move and then cast without a
        // command-side melee/ranged table.
        // This request came from the networked owner. Keep its queued
        // continuation eligible even if a later population tick is minimal.
        if (!QueueMatureAction(ai, actionName, event, ACTION_PASSTROUGH))
        {
            SendActionError(handler, "interrupt", "failed",
                "The mature interrupt action could not be started.");
            return true;
        }

        // Do not wrap this tick in ScopedSilentStrategy: changing the
        // non-combat strategy rebuilds that engine and would discard the
        // queued action/continuation before the reach prerequisite can run.
        ai->DoNextAction(false, true);
        accepted = true;
    }
    else
    {
        ExecuteQuietNextAction(ai, true);
    }

    SendActionAck(handler, "interrupt",
        context.selectedBot == executor
            ? "bot:" + std::string(executor->GetName()) : "party",
        1, executor->GetName());
    return accepted;
}

static std::string RosterLocation(uint32 mapId, uint32 zoneId, uint32 areaId)
{
    uint32 displayAreaId = areaId ? areaId : zoneId;
    if (displayAreaId)
    {
        auto const* area = GetAreaEntryByAreaID(displayAreaId);
        if (area && area->Name && *area->Name)
            return ProtocolSafe(area->Name);
    }
    if (!mapId && !zoneId && !areaId)
        return "-";
    return "map:" + std::to_string(mapId) + ",zone:" + std::to_string(zoneId) +
        ",area:" + std::to_string(areaId);
}

// Channel the core currently dispatches addon commands from for this
// requester. Mirrors the PARTY path of WorldSession::HandleMessagechatOpcode:
// the message is consumed before any broadcast, so a pre-battleground group or
// any non-battleground group works (raids included) and subgroup scoping is
// irrelevant. A battleground group with no original group never reaches a
// module, so the addon has to keep using `.bot` chat there.
static char const* AddonCommandChannel(Player* requester)
{
    // A server can disable the addon channel outright; the chat handler drops
    // every LANG_ADDON message before any module sees it.
    if (!sWorld.getConfig(CONFIG_BOOL_ADDON_CHANNEL))
        return "none";

    Group* group = requester ? requester->GetOriginalGroup() : nullptr;
    if (!group)
    {
        group = requester ? requester->GetGroup() : nullptr;
        if (!group || group->isBGGroup())
            return "none";
    }
    return "party";
}

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

static BehaviorToggle const* FindBehaviorToggle(std::string const& key)
{
    for (BehaviorToggle const& toggle : BehaviorToggles)
        if (key == toggle.key)
            return &toggle;
    return nullptr;
}

static bool IsBehaviorOn(PlayerbotAI* ai, BehaviorToggle const& toggle)
{
    return ai->HasStrategy(toggle.strategy, toggle.state) != toggle.inverted;
}

static void SetBehavior(PlayerbotAI* ai, BehaviorToggle const& toggle, bool enable)
{
    ai->ChangeStrategy((enable != toggle.inverted ? "+" : "-") + std::string(toggle.strategy), toggle.state);
}

// "TBM:BOTSTATE|<name>|move=follow,loot=on,..." - one line per live bot.
// Flee parks a bot in passive follow, reported as its own movement mode so
// the addon can show that the bot is holding off until the next order.
static void SendBotStateLine(ChatHandler* handler, Player* bot)
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

static bool HandleRoster(ChatHandler* handler)
{
    Player* requester = Requester(handler);
    if (!requester || !requester->GetSession())
    {
        handler->PSendSysMessage("TBM:ROSTER_ERROR|not-in-game|You must be in-game.");
        handler->PSendSysMessage("You must be in-game to request a bot roster.");
        return true;
    }

    uint32 accountId = requester->GetSession()->GetAccountId();
    // Backfill runtime records created before the durable roster migration.
    // The roster query also exposes undeleted same-account characters, while
    // explicit cross-account rows remain limited to the owning GM account.
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        BotRecord* record = bot ? BotManager::Instance().FindBot(bot->GetObjectGuid()) : nullptr;
        if (!bot || !record || record->random || record->accountId != accountId ||
            !CanControlBot(requester, record))
            continue;

        OwnedCharacter existing;
        if (!BotManager::Instance().GetOwnedCharacter(bot->GetObjectGuid(), existing))
            BotManager::Instance().RegisterOwnedCharacter(
                accountId, record->accountId, bot->GetObjectGuid(), record->masterGuid);
    }

    std::vector<OwnedCharacter> rows = BotManager::Instance().GetOwnedCharacters(
        requester->GetSession()->GetAccountId());
    for (auto it = rows.begin(); it != rows.end(); )
    {
        // The roster lists the player's own characters. Hired companions live on
        // random-pool accounts and are temporary: their character is deleted
        // when the hire ends, so a dismissed companion never shows up here.
        if (it->characterGuid == requester->GetObjectGuid() ||
            RandomBotAccountRegistry::Instance().IsRegistered(it->characterAccountId))
            it = rows.erase(it);
        else
            ++it;
    }
    handler->PSendSysMessage("TBM:ROSTER_BEGIN|%u", static_cast<uint32>(rows.size()));
    for (OwnedCharacter const& row : rows)
    {
        Player* player = sObjectAccessor.FindPlayer(row.characterGuid);
        BotRecord* record = BotManager::Instance().FindBot(row.characterGuid);
        std::string name = row.name.empty() ? "-" : row.name;
        uint32 classId = row.classId;
        uint32 mapId = row.mapId;
        uint32 zoneId = row.zoneId;
        uint32 areaId = row.areaId;
        uint32 grouped = 0;

        // Runtime Headless state, not characters.online, is authoritative.
        // A Network player with the same GUID is never reported as an active bot.
        std::string state = RosterState(player, record);
        if (player && state != "offline")
        {
            name = player->GetName();
            classId = player->GetClass();
            mapId = player->GetMapId();
            zoneId = player->GetZoneId();
            areaId = player->GetAreaId();
            grouped = requester->GetGroup() && requester->GetGroup()->IsMember(row.characterGuid) ? 1u : 0u;
        }

        handler->PSendSysMessage("TBM:ROSTER|%u|%s|%u|%s|%u|%s",
            row.characterGuid.GetCounter(), ProtocolSafe(name).c_str(), classId,
            ProtocolSafe(state).c_str(), grouped,
            ProtocolSafe(RosterLocation(mapId, zoneId, areaId)).c_str());
    }
    handler->PSendSysMessage("TBM:ROSTER_END");
    // CC assignments and the BOTSTATE snapshot below describe the same set:
    // the controllable party bots (owned alts and hired companions). The
    // roster rows leave hires out on purpose, so they cannot feed CC_ASSIGN.
    // An owned alt that is online but not in the party has no CC role (a CC
    // assignment only acts inside a party), so it is not listed. "rti cc"
    // defaults to Moon on every bot, so the mark alone does not make a CC bot:
    // list a bot only when it also has the "cc" combat strategy, the same
    // condition the CC hold uses (TargetValue.cpp).
    std::vector<Player*> const partyBots = BuildContext(requester).partyBots;
    std::vector<std::pair<std::string, std::string>> ccAssignments;
    for (Player* bot : partyBots)
    {
        PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!botAi || !botAi->HasStrategy("cc", BotState::BOT_STATE_COMBAT))
            continue;
        std::string const ccMark = CurrentCcMark(botAi);
        if (ccMark != "-")
            ccAssignments.emplace_back(bot->GetName(), ccMark);
    }
    handler->PSendSysMessage("TBM:CC_ASSIGN_BEGIN|%u", static_cast<uint32>(ccAssignments.size()));
    for (auto const& assignment : ccAssignments)
        handler->PSendSysMessage("TBM:CC_ASSIGN|%s|%s", ProtocolSafe(assignment.first).c_str(),
            ProtocolSafe(assignment.second).c_str());
    handler->PSendSysMessage("TBM:CC_ASSIGN_END");
    // Issue #489: Claimed guild bots snapshot for the player's raid roster
    std::vector<uint32> claimedGuids = ClaimLifecycle::Instance().GetClaimedBotsForPlayer(requester->GetGUIDLow());
    handler->PSendSysMessage("TBM:CLAIMED_BEGIN|%u", static_cast<uint32>(claimedGuids.size()));
    for (uint32 botGuidLow : claimedGuids)
    {
        ObjectGuid botGuid(HIGHGUID_PLAYER, botGuidLow);
        Player* player = sObjectAccessor.FindPlayer(botGuid);
        BotRecord* record = BotManager::Instance().FindBot(botGuid);
        std::string name = "-";
        uint32 classId = 0;
        uint32 level = 0;
        uint32 mapId = 0, zoneId = 0, areaId = 0;
        uint32 grouped = 0;
        uint32 gearLocked = 0;

        if (player)
        {
            name = player->GetName();
            classId = player->GetClass();
            level = player->GetLevel();
            mapId = player->GetMapId();
            zoneId = player->GetZoneId();
            areaId = player->GetAreaId();
            grouped = requester->GetGroup() && requester->GetGroup()->IsMember(botGuid) ? 1u : 0u;
            gearLocked = (level >= 60) ? 1u : 0u;
        }
        else if (PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(botGuidLow))
        {
            name = data->sName;
        }

        std::string state = RosterState(player, record);
        handler->PSendSysMessage("TBM:CLAIMED|%u|%s|%u|%u|%s|%u|%s|%u",
            botGuidLow, ProtocolSafe(name).c_str(), classId, level,
            ProtocolSafe(state).c_str(), grouped,
            ProtocolSafe(RosterLocation(mapId, zoneId, areaId)).c_str(),
            gearLocked);
    }
    handler->PSendSysMessage("TBM:CLAIMED_END");
    // Live behaviour snapshot for the controllable party bots (owned alts
    // and hired companions alike): drives the addon's toggle states and the
    // Party tab movement label. BEGIN/END replace the addon's whole set.
    // The addon drops a snapshot whose row count differs from BEGIN, so
    // count only the bots that will actually produce a line.
    std::vector<Player*> stateBots;
    for (Player* bot : partyBots)
        if (PlayerbotAIStorage::Instance().GetAI(bot))
            stateBots.push_back(bot);
    handler->PSendSysMessage("TBM:BOTSTATE_BEGIN|%u", static_cast<uint32>(stateBots.size()));
    for (Player* bot : stateBots)
        SendBotStateLine(handler, bot);
    handler->PSendSysMessage("TBM:BOTSTATE_END");
    // Version trailer: the addon shows "server <version>" from this line and
    // falls back to "server ?" when an older module never sends it.
    handler->PSendSysMessage("TBM:VERSION|%s", BuildVersion().c_str());
    // Capability trailer: pull-seconds = adjustable pull/pullback delays
    // (".bot action pull [seconds]", 0-60); flee = ".bot action flee";
    // inventory = ".bot inv" / ".bot item"; behavior = ".bot behavior" and the
    // BOTSTATE snapshot. Old addons ignore unknown lines and items.
    handler->PSendSysMessage("TBM:CAPS|pull-seconds,flee,inventory,behavior,claimed-roster");
    // Transport trailer: the addon sends its next commands over the addon
    // channel only while this says "party". See AddonCommandChannel.
    handler->PSendSysMessage("TBM:TRANSPORT|%s", AddonCommandChannel(requester));
    return true;
}

static bool HandleLogout(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot logout <online bot name> (same account only)");
        return true;
    }

    if (HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
    {
        // A hire is temporary: logging it out ends the hire and deletes its
        // character instead of keeping a pool character alive with the
        // master's level and gear.
        HireLifecycle::Instance().DismissNow(bot->GetObjectGuid(), "logged out by master");
        handler->PSendSysMessage("Hire ended for %s; its character is being deleted.", name.c_str());
        return true;
    }

    if (!BotManager::Instance().RemoveBot(bot->GetObjectGuid(), true))
    {
        handler->PSendSysMessage("Bot %s could not be logged out.", name.c_str());
        return true;
    }

    // Remove/logout deliberately leaves tortoise_bots_owned_character intact.
    handler->PSendSysMessage("Bot %s logout requested; durable ownership was retained.", name.c_str());
    return true;
}

// ── TBM bot panel: inventory snapshot, item orders, behaviour toggles ──────
// The panel addresses items by the server's own bag/slot pair, so an order
// always lands on the exact item the player clicked - never on "the first
// item with this name" like the whisper grammar.

// Mirrors WorldSession::HandleSetTradeItemOpcode, which cancels the whole
// trade on a refused item: only offer items the core will accept.
static bool CanTradeItemTo(Player* bot, Item* item, Player* requester)
{
    if (!bot || !item || !requester || !item->CanBeTraded())
        return false;
    if (item->IsSoulBound())
        return bot->GetMapId() == item->GetOriginMapId() &&
            bot->GetGroup() == requester->GetGroup() &&
            item->CanTradeSoulBoundToPlayer(requester->GetObjectGuid());
    return true;
}

// Flags: e = the bot can equip it, u = the mature item-usage value calls it
// an upgrade, b = it cannot be traded to the requester right now.
static std::string InventoryItemFlags(PlayerbotAI* ai, Player* bot, Player* requester, Item* item)
{
    std::string flags;
    ItemPrototype const* proto = item->GetProto();
    if (proto && proto->InventoryType != INVTYPE_NON_EQUIP && bot->CanUseItem(proto) == EQUIP_ERR_OK)
    {
        flags += 'e';
        ai::ItemUsage usage = ai->GetAiObjectContext()->GetValue<ai::ItemUsage>("item usage",
            ai::ItemQualifier(item).GetQualifier())->Get();
        if (usage == ai::ItemUsage::ITEM_USAGE_EQUIP)
            flags += 'u';
    }
    if (!CanTradeItemTo(bot, item, requester))
        flags += 'b';
    return flags.empty() ? "-" : flags;
}

// TBM:INV_BEGIN|<bot>|<copper>
// TBM:INV_EQ|<slot>|<itemId>|<durability>|<maxDurability>
// TBM:INV_ITEM|<bag>|<slot>|<itemId>|<count>|<flags>
// TBM:INV_END|<bot>|<freeSlots>|<totalSlots>
static void SendInventorySnapshot(ChatHandler* handler, Player* bot, Player* requester)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!handler || !bot || !ai)
        return;
    std::string const name = ProtocolSafe(bot->GetName());
    handler->PSendSysMessage("TBM:INV_BEGIN|%s|%u", name.c_str(), bot->GetMoney());
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        handler->PSendSysMessage("TBM:INV_EQ|%u|%u|%u|%u", static_cast<uint32>(slot), item->GetEntry(),
            item->GetUInt32Value(ITEM_FIELD_DURABILITY), item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY));
    }

    uint32 freeSlots = 0;
    uint32 totalSlots = 0;
    auto emit = [&](uint8 bag, uint8 slot, Item* item)
    {
        ++totalSlots;
        if (!item)
        {
            ++freeSlots;
            return;
        }
        handler->PSendSysMessage("TBM:INV_ITEM|%u|%u|%u|%u|%s", static_cast<uint32>(bag),
            static_cast<uint32>(slot), item->GetEntry(), item->GetCount(),
            InventoryItemFlags(ai, bot, requester, item).c_str());
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        emit(INVENTORY_SLOT_BAG_0, slot, bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
        if (!bag)
            continue;
        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
            emit(bagSlot, static_cast<uint8>(slot), bag->GetItemByPos(static_cast<uint8>(slot)));
    }
    handler->PSendSysMessage("TBM:INV_END|%s|%u|%u", name.c_str(), freeSlots, totalSlots);
}

static bool HandleInventory(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name) ||
        !PlayerbotAIStorage::Instance().GetAI(bot))
    {
        SendActionError(handler, "inv", "no-bot", "Usage: .bot inv <online bot you control>");
        return true;
    }
    SendInventorySnapshot(handler, bot, requester);
    return true;
}
// Issue #473: opt-in owned/hired-bot quality of life (all parts off by
// default, pool bots never eligible). Maintenance refreshes one owned bot's
// kit through the same bounded helpers as the hire/refresh paths
// (consumables, reagents, ammo, thrown, bandages, then a free repair);
// autogear re-gears incrementally within the quality/ilvl caps. With the
// flags off the commands reply that the feature is disabled.
static bool IsOwnedBotQolEligible(Player* bot, BotRecord const* record)
{
    if (!bot || !record)
        return false;
    if (!BotManager::Instance().IsBot(bot->GetObjectGuid()))
        return false;
    if (BotManager::Instance().IsRandomBot(bot->GetObjectGuid()))
        return HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
    return true;
}

static bool HandleMaintenance(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot maintenance <online bot name> (same account only)");
        return true;
    }
    if (!IsOwnedBotQolEligible(bot, record))
    {
        handler->PSendSysMessage("'%s' is a pool bot and keeps progressing organically.", name.c_str());
        return true;
    }
    if (!sPlayerbotAIConfig.ownedBotMaintenanceEnabled)
    {
        handler->PSendSysMessage("Owned-bot maintenance is disabled (AiPlayerbot.OwnedBotMaintenanceEnabled = 0).");
        return true;
    }
    if (!bot->IsAlive())
    {
        handler->PSendSysMessage("Bot '%s' must be alive.", name.c_str());
        return true;
    }
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.AddReagents();
    factory.AddPotions();
    factory.AddFood();
    factory.AddConsumes();
    factory.AddBandages();
    factory.InitAmmo();
    factory.InitThrown();
    bot->DurabilityRepairAll(false, 0.0f);
    bot->SaveToDB();
    handler->PSendSysMessage("Maintained %s (consumables, reagents, ammo, repair).", name.c_str());
    return true;
}

static bool HandleAutogear(ChatHandler* handler, char const* args)
{
    std::string text = Trim(args ? args : "");
    size_t split = text.find_first_of(" \t");
    std::string botToken = split == std::string::npos ? text : text.substr(0, split);
    std::string param = split == std::string::npos ? "" : text.substr(split + 1);
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || botToken.empty() || !ResolveOwnedBot(handler, botToken.c_str(), bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot autogear <online bot name> [quality|ilvl] (same account only)");
        return true;
    }
    if (!IsOwnedBotQolEligible(bot, record))
    {
        handler->PSendSysMessage("'%s' is a pool bot and keeps progressing organically.", name.c_str());
        return true;
    }
    if (!sPlayerbotAIConfig.ownedBotAutogearEnabled)
    {
        handler->PSendSysMessage("Owned-bot autogear is disabled (AiPlayerbot.OwnedBotAutogearEnabled = 0).");
        return true;
    }
    if (!bot->IsAlive())
    {
        handler->PSendSysMessage("Bot '%s' must be alive.", name.c_str());
        return true;
    }
    OwnedBotAutogearRequest parsed = ParseOwnedBotAutogearArg(param,
        sPlayerbotAIConfig.ownedBotAutogearQualityCap, sPlayerbotAIConfig.ownedBotAutogearIlvlCap);
    if (!parsed.ok)
    {
        handler->PSendSysMessage("Unknown autogear option '%s'. Use a quality (white, green, blue, purple) or an item level number.", Trim(param).c_str());
        return true;
    }
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.AutogearOwned(parsed.quality, parsed.ilvlCap);
    handler->PSendSysMessage("Re-geared %s within the configured caps.", name.c_str());
    return true;
}

// Opens a trade window between the bot and the requester. The core creates
// both trade records immediately; the requester's client opens its window
// when TRADE_STATUS_BEGIN_TRADE arrives.
static bool OpenTradeWith(Player* bot, Player* requester)
{
    if (bot->GetTrader() == requester)
        return true;
    if (bot->GetTrader() || requester->GetTrader())
        return false;
    WorldPacket packet(CMSG_INITIATE_TRADE, 8);
    packet << requester->GetObjectGuid();
    bot->GetSession()->HandleInitiateTradeOpcode(packet);
    return bot->GetTrader() == requester;
}

// .bot item <bot> trade
// .bot item <bot> equip|unequip|give <bag> <slot>
// Every reply is one TBM:ACTION_ACK / TBM:ACTION_ERR with intent "item <op>";
// equip and unequip follow it with a fresh inventory snapshot.
static bool HandleItem(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::istringstream input(Trim(args ? args : ""));
    std::string botName;
    std::string op;
    input >> botName >> op;
    for (char& c : op)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string const intent = "item " + (op.empty() ? std::string("?") : op);
    if (op != "trade" && op != "equip" && op != "unequip" && op != "give")
    {
        SendActionError(handler, intent, "invalid", "Usage: .bot item <bot> trade | equip/unequip/give <bag> <slot>");
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
    std::string const scope = "bot:" + std::string(bot->GetName());
    if (!bot->IsAlive())
    {
        SendActionError(handler, intent, "dead", "The bot must be alive.");
        return true;
    }

    if (op == "trade")
    {
        if (!OpenTradeWith(bot, requester))
        {
            SendActionError(handler, intent, "busy", "Trade could not be opened (busy, too far, or in combat).");
            return true;
        }
        SendActionAck(handler, intent, scope, 1, "open");
        return true;
    }

    uint32 bag = 0;
    uint32 slot = 0;
    if (!(input >> bag >> slot) || bag > 255 || slot > 255)
    {
        SendActionError(handler, intent, "invalid", "Missing bag and slot.");
        return true;
    }
    // Carried items only: equipment, backpack and equipped bags. Bank slots
    // share bag 255 with the backpack and must never be reachable from here.
    bool const equipped = Player::IsEquipmentPos(static_cast<uint8>(bag), static_cast<uint8>(slot));
    if (!equipped && !Player::IsInventoryPos(static_cast<uint8>(bag), static_cast<uint8>(slot)))
    {
        SendActionError(handler, intent, "invalid", "Only carried items can be ordered.");
        return true;
    }
    Item* item = bot->GetItemByPos(static_cast<uint8>(bag), static_cast<uint8>(slot));
    if (!item || !item->GetProto())
    {
        SendActionError(handler, intent, "no-item", "That item is no longer there; refresh the bag view.");
        SendInventorySnapshot(handler, bot, requester);
        return true;
    }
    uint32 const itemId = item->GetEntry();

    if (op == "equip")
    {
        ItemPrototype const* proto = item->GetProto();
        if (equipped || proto->InventoryType == INVTYPE_NON_EQUIP || bot->CanUseItem(proto) != EQUIP_ERR_OK)
        {
            SendActionError(handler, intent, "cannot-equip", "The bot cannot equip this item.");
            return true;
        }
        ai::EquipAction::EquipItem(ai, requester, item, true);
        // Equip and bag slots all sit below the backpack range; ammo stays in
        // the bag and is equipped through the ammo field instead.
        bool const done = proto->InventoryType == INVTYPE_AMMO
            ? bot->GetUInt32Value(PLAYER_AMMO_ID) == itemId
            : item->GetBagSlot() == INVENTORY_SLOT_BAG_0 && item->GetSlot() < INVENTORY_SLOT_ITEM_START;
        if (!done)
            SendActionError(handler, intent, "failed", "The equip was refused (combat, level, or skill).");
        else
            SendActionAck(handler, intent, scope, 1, std::to_string(itemId));
        SendInventorySnapshot(handler, bot, requester);
        return true;
    }

    if (op == "unequip")
    {
        if (!equipped)
        {
            SendActionError(handler, intent, "not-equipped", "That item is not equipped.");
            return true;
        }
        ai::UnequipAction::UnequipItem(ai, requester, item, true);
        if (bot->GetItemByPos(static_cast<uint8>(bag), static_cast<uint8>(slot)) == item)
            SendActionError(handler, intent, "failed", "No free bag space, or the item cannot be removed now.");
        else
            SendActionAck(handler, intent, scope, 1, std::to_string(itemId));
        SendInventorySnapshot(handler, bot, requester);
        return true;
    }

    // give: put the item into a trade window with the requester. Without an
    // open trade the first request opens one and answers "pending"; the addon
    // repeats the request once its trade window is shown.
    if (equipped)
    {
        SendActionError(handler, intent, "equipped", "Unequip the item first.");
        return true;
    }
    if (!CanTradeItemTo(bot, item, requester))
    {
        SendActionError(handler, intent, "no-trade", "This item cannot be traded (soulbound or quest item).");
        return true;
    }
    if (bot->GetTrader() != requester)
    {
        if (!OpenTradeWith(bot, requester))
        {
            SendActionError(handler, intent, "busy", "Trade could not be opened (busy, too far, or in combat).");
            return true;
        }
        SendActionAck(handler, intent, scope, 1, std::to_string(itemId) + " pending");
        return true;
    }
    if (item->IsInTrade())
    {
        SendActionAck(handler, intent, scope, 1, std::to_string(itemId));
        return true;
    }
    TradeData* trade = bot->GetTradeData();
    int tradeSlot = -1;
    for (uint8 i = 0; trade && i < TRADE_SLOT_TRADED_COUNT && tradeSlot < 0; ++i)
        if (!trade->GetItem(TradeSlots(i)))
            tradeSlot = i;
    if (tradeSlot < 0)
    {
        SendActionError(handler, intent, "trade-full", "The trade window is full.");
        return true;
    }
    WorldPacket packet(CMSG_SET_TRADE_ITEM, 3);
    packet << static_cast<uint8>(tradeSlot) << static_cast<uint8>(bag) << static_cast<uint8>(slot);
    bot->GetSession()->HandleSetTradeItemOpcode(packet);
    if (!item->IsInTrade())
    {
        SendActionError(handler, intent, "failed", "The trade refused this item.");
        return true;
    }
    SendActionAck(handler, intent, scope, 1, std::to_string(itemId));
    return true;
}

// .bot behavior <bot|party|raid> <key> <on|off> - allowlisted per-bot
// strategy toggles (BehaviorToggles). Persisted through the mature AI store
// like loot/auto cc; answers with an ACK and the bot's fresh TBM:BOTSTATE
// line. "party"/"raid" fans out to every live owned bot in the scope.
static bool HandleBehavior(ChatHandler* handler, char const* args)
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

static bool ParseAction(std::string input, std::string& intent, std::string& option)
{
    intent.clear();
    option.clear();
    input = Trim(input);
    if (input.empty())
        return false;

    size_t firstSeparator = input.find_first_of(" \t");
    std::string first = input.substr(0, firstSeparator);
    for (char& character : first)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    std::string remainder = firstSeparator == std::string::npos
        ? std::string() : Trim(input.substr(firstSeparator + 1));
    if (first == "focus" || first == "cc")
    {
        size_t secondSeparator = remainder.find_first_of(" \t");
        std::string mark = remainder.substr(0, secondSeparator);
        for (char& character : mark)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        // "cc clear [<BotName>]" is the dismissal intent (no mark named
        // "clear" exists: GetRtiIndex returns -1 for it).
        if (first == "cc" && mark == "clear")
        {
            std::string botToken = secondSeparator == std::string::npos
                ? std::string() : Trim(remainder.substr(secondSeparator + 1));
            if (mark.empty() || botToken.find_first_of(" \t") != std::string::npos)
                return false;
            intent = botToken.empty() ? first + " " + mark : first + " " + mark + " " + botToken;
            return true;
        }
        // "cc <mark> [<BotName>]": an explicit bot name assigns the mark to
        // that owned bot without juggling the player's target.
        if (first == "cc" && IsRaidTargetMark(mark))
        {
            std::string botToken = secondSeparator == std::string::npos
                ? std::string() : Trim(remainder.substr(secondSeparator + 1));
            if (mark.empty() || botToken.find_first_of(" \t") != std::string::npos)
                return false;
            intent = botToken.empty() ? first + " " + mark : first + " " + mark + " " + botToken;
            return true;
        }
        if ((first == "focus" && mark != "skull") ||
            (first == "cc" && !IsRaidTargetMark(mark)) ||
            mark.empty() ||
            (secondSeparator != std::string::npos &&
                !Trim(remainder.substr(secondSeparator + 1)).empty()))
            return false;
        intent = first + " " + mark;
        return true;
    }
    if (first == "corpse")
    {
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (rest != "run")
            return false;
        intent = "corpse run";
        return true;
    }

    if (first != "attack" && first != "interrupt" && first != "stop" && first != "pull" &&
        first != "pullback" && first != "come" && first != "stay" &&
        first != "follow" && first != "auto" && first != "aoe" && first != "hold" &&
        first != "comestay" && first != "ready" && first != "loot" &&
        first != "repair" && first != "sell" && first != "rest" &&
        first != "drink" && first != "eat" && first != "release" &&
        first != "corpse run" && first != "corpserun" && first != "learn" &&
        first != "trade" && first != "raid" && first != "flee" && first != "sap")
        return false;

    if (first == "raid")
    {
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (rest != "status" && rest != "tankface" && rest != "douse" && rest != "custom status" && rest != "custom on" && rest != "custom off")
            return false;
        intent = "raid " + rest;
        return true;
    }
    else if (first == "corpse run" || first == "corpserun")
    {
        intent = "corpse run";
        return true;
    }
    else if (first == "auto")
    {
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        // Only "auto cc" exists today; keep the surface extensible.
        // Split "auto cc [on|off]" into intent + option like aoe/loot.
        if (rest == "cc")
        {
            intent = "auto cc";
            return true;
        }
        if (rest == "cc on" || rest == "cc off")
        {
            intent = "auto cc";
            option = rest.substr(3);
            return true;
        }
        return false;
    }
    else if (first == "aoe" || first == "loot")
    {
        // "aoe [on|off]" / "loot [on|off]": bare form toggles, as documented.
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (!rest.empty() && rest != "on" && rest != "off")
            return false;
        intent = first;
        option = rest;
        return true;
    }
    else if (first == "sap")
    {
        // "sap [on|off] [<BotName>]": pre-pull Sap switch. Bare form toggles,
        // like aoe/loot; an explicit name scopes it to that owned bot, like
        // "cc <mark> [bot]". The mode token is lowercased, the name keeps its
        // case for resolution. option = "<on|off|toggle>[ <BotName>]".
        size_t modeSeparator = remainder.find_first_of(" \t");
        std::string mode = remainder.substr(0, modeSeparator);
        for (char& character : mode)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        std::string botToken = modeSeparator == std::string::npos
            ? std::string() : Trim(remainder.substr(modeSeparator + 1));
        if (mode != "on" && mode != "off")
        {
            // "sap <BotName>": toggle for that bot.
            if (!botToken.empty())
                return false;
            botToken = remainder.substr(0, modeSeparator);
            mode = "toggle";
        }
        if (botToken.find_first_of(" \t") != std::string::npos)
            return false;
        intent = first;
        option = botToken.empty() ? mode : mode + " " + botToken;
        return true;
    }
    else if (first == "pull" || first == "pullback")
    {
        // Owner-specified adjustable delay: ".bot action pull [seconds]" and
        // ".bot action pullback [seconds]". Plain "pull"/"pullback" keeps the
        // configured default; the TBM addon sends the slider value here.
        // Anything else is rejected here (not in HandleAction) so the usage
        // line stays the single source of truth for the surface.
        if (remainder.empty())
        {
            intent = first;
            return true;
        }
        try
        {
            size_t pos = 0;
            int seconds = std::stoi(remainder, &pos);
            if (pos != remainder.size() || seconds < 0 || seconds > 60)
                return false;
            intent = first;
            option = std::to_string(seconds);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    else if (!remainder.empty())
        return false;
    intent = first;
    return true;
}

static bool IsRaidTargetMark(std::string const& mark)
{
    return RtiTargetValue::GetRtiIndex(mark) >= 0;
}

// A refused CC order puts the group's raid marks back. The slots are restored
// in order: SetTargetIcon clears any other slot holding the mob it is given,
// and a mob holds one mark, so a slot already put back never holds a mob that
// a later slot gets back.
static void SnapshotRaidMarks(Group* group, ObjectGuid (&marks)[TARGET_ICON_COUNT])
{
    for (uint8 i = 0; i < TARGET_ICON_COUNT; ++i)
        marks[i] = group->GetTargetIcon(i);
}

static void RestoreRaidMarks(Group* group, ObjectGuid const (&marks)[TARGET_ICON_COUNT])
{
    for (uint8 i = 0; i < TARGET_ICON_COUNT; ++i)
        if (group->GetTargetIcon(i) != marks[i])
            group->SetTargetIcon(i, marks[i]);
}

// One mark = one owner. After assigning <mark> to <owner>, every other owned
// live bot in the same command scope holding that mark is reset to "none" and
// persisted. "none" resolves to GetRtiIndex -1, so CurrentCcMark reports "-"
// and the CC_ASSIGN snapshot drops the stale owner without extra plumbing.
// Returns the AIs it reset (each held exactly <mark>), so a refused order can
// put them back.
static std::vector<PlayerbotAI*> RevokeDuplicateCcOwners(Player* owner, std::string const& mark,
    std::vector<Player*> const& scope)
{
    std::vector<PlayerbotAI*> revoked;
    for (Player* other : scope)
    {
        if (!other || other == owner || !IsLiveHeadlessBot(other))
            continue;
        PlayerbotAI* otherAi = PlayerbotAIStorage::Instance().GetAI(other);
        if (!otherAi || !otherAi->GetAiObjectContext())
            continue;
        ai::Value<std::string>* otherMark =
            otherAi->GetAiObjectContext()->GetValue<std::string>("rti cc");
        if (!otherMark || otherMark->Get() != mark)
            continue;
        otherMark->Set("none");
        sPlayerbotDbStore.Save(otherAi);
        revoked.push_back(otherAi);
    }
    return revoked;
}

static std::string CurrentCcMark(PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return "-";

    std::string mark = ai->GetAiObjectContext()->GetValue<std::string>("rti cc")->Get();
    return IsRaidTargetMark(mark) ? mark : "-";
}

// The `cc <mark> [bot]` order against context.enemyTarget (or the group's
// existing mark). Shared by `.bot action cc` and the remote commander's
// `.bot cmd cc` (issue #884): requester is the event owner and the unit whose
// map and attack rules judge the target; reportIntent names the order in the
// ACK/ERR line.
static bool RunCcOrder(ChatHandler* handler, BotCommandContext const& context, Player* requester,
    std::string const& intent, std::string const& reportIntent)
{
    // Optional explicit bot name ("cc <mark> <BotName>") assigns that
    // owned bot without juggling the player's target. The mark token
    // itself is lowercase; the name keeps its case for resolution.
    std::string ccBotToken;
    std::string ccMark = intent.substr(3);
    size_t ccSpace = ccMark.find_first_of(" \t");
    if (ccSpace != std::string::npos)
    {
        ccBotToken = Trim(ccMark.substr(ccSpace + 1));
        ccMark = ccMark.substr(0, ccSpace);
    }
    int markIndex = RtiTargetValue::GetRtiIndex(ccMark);
    Unit* ccTarget = context.enemyTarget;

    // An explicit name wins over the live target: same assignment path
    // as the selected-bot case, including the immediate-cast probe when
    // an enemy is selected or the mark already exists. It is resolved
    // before the target checks, so an unknown name still reports no-bot.
    Player* namedBot = nullptr;
    if (!ccBotToken.empty())
    {
        BotRecord* namedRecord = nullptr;
        std::string namedName;
        if (!ResolveOwnedBot(handler, ccBotToken.c_str(), namedBot, namedRecord, namedName))
        {
            SendActionError(handler, reportIntent, "no-bot",
                "Unknown or uncontrollable bot name for cc assignment.");
            return true;
        }
    }

    // The group's marks before this order moves one: a refusal after the
    // move puts them back (RestoreRaidMarks).
    bool markMoved = false;
    ObjectGuid marksBefore[TARGET_ICON_COUNT];

    // Selecting an enemy is the most explicit request: make that unit the
    // requested raid mark. If the player selected a bot instead, preserve
    // the existing group mark and use it as the assignment target.
    if (ccTarget)
    {
        // Refuse before the mark moves: a corpse or non-attackable unit
        // would take the mark, and the order would still ACK success with
        // no CC target behind it. A GM requester is faction 35
        // (Player::SetGameMaster), which the attackability test refuses for
        // every hostile mob, so a GM needs only a live, targetable unit.
        bool const requesterIsGm = requester->IsGameMaster();
        bool const badTarget = !ccTarget->IsInWorld() || !ccTarget->IsAlive()
            || (requesterIsGm ? !ccTarget->IsTargetable(true, true)
                              : !requester->IsValidAttackTarget(ccTarget));
        if (badTarget)
        {
            SendActionError(handler, reportIntent, "no-target",
                "Your target is dead, out of the world or not attackable. Select a live enemy first.");
            return true;
        }
        // A named or selected executor is known before the mark moves, so
        // an executor without an AI is refused before any mark changes.
        Player* assignee = namedBot ? namedBot
            : (context.selectedBot && IsLiveHeadlessBot(context.selectedBot) ? context.selectedBot : nullptr);
        if (assignee && !PlayerbotAIStorage::Instance().GetAI(assignee))
        {
            SendActionError(handler, reportIntent, "no-ai", "The selected CC bot has no AI.");
            return true;
        }

        if (context.group)
        {
            SnapshotRaidMarks(context.group, marksBefore);
            context.group->SetTargetIcon(static_cast<uint8>(markIndex), ccTarget->GetObjectGuid());
            markMoved = true;
        }
    }
    else if (context.group && requester->GetMap())
    {
        ObjectGuid markGuid = context.group->GetTargetIcon(static_cast<uint8>(markIndex));
        if (!markGuid.IsEmpty())
            ccTarget = requester->GetMap()->GetUnit(markGuid);
    }

    if (ccTarget && (!ccTarget->IsInWorld() || !ccTarget->IsAlive()))
        ccTarget = nullptr;

    Player* executor = nullptr;
    std::string ccAction;
    if (namedBot)
    {
        executor = namedBot;
        if (ccTarget)
        {
            // Probe the named bot itself, not the party's best executor,
            // so the immediate cast uses an action this bot owns.
            BotCommandContext namedContext = context;
            namedContext.selectedBot = namedBot;
            ResolveCcExecutor(namedContext, ccTarget, ccMark, &ccAction);
        }
    }
    else if (context.selectedBot && IsLiveHeadlessBot(context.selectedBot))
    {
        // A selected bot is an assignment request. It may be configured
        // before an enemy is marked; when a live target is available, keep
        // the mature capability probe for an immediate attempt, but never
        // reject the assignment just because this current creature is not
        // legal for that bot's CC spell.
        executor = context.selectedBot;
        if (ccTarget)
        {
            ResolveCcExecutor(context, ccTarget, ccMark, &ccAction);
        }
    }
    else
    {
        if (!ccTarget)
        {
            SendActionError(handler, reportIntent, "no-target",
                "Select an enemy target or an owned bot with an existing raid mark.");
            return true;
        }

        executor = ResolveCcExecutor(context, ccTarget, ccMark, &ccAction);
        if (!executor)
        {
            if (markMoved)
                RestoreRaidMarks(context.group, marksBefore);
            SendActionError(handler, reportIntent, "no-cc",
                "No owned bot can CC this target (check class, target type, and spell state).");
            return true;
        }
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(executor);
    if (!ai)
    {
        if (markMoved)
            RestoreRaidMarks(context.group, marksBefore);
        SendActionError(handler, reportIntent, "no-ai", "The selected CC bot has no AI.");
        return true;
    }

    // RtiCcValue is a per-bot preference. Persist it through the existing
    // mature AI store so the assignment survives AI recreation/relogin.
    std::string const previousCcMark = ai->GetAiObjectContext()->GetValue<std::string>("rti cc")->Get();
    if (!ExecuteQuietAction(ai, "rti", ai::Event(intent, "cc " + ccMark, requester)))
    {
        if (markMoved)
            RestoreRaidMarks(context.group, marksBefore);
        SendActionError(handler, reportIntent, "failed", "The mature CC assignment was rejected.");
        return true;
    }
    sPlayerbotDbStore.Save(ai);
    // Exclusive ownership: the new owner keeps the mark; every other owned
    // live party bot holding it is reset to "none" and persisted.
    std::vector<PlayerbotAI*> const revokedOwners =
        RevokeDuplicateCcOwners(executor, ccMark, context.partyBots);

    if (ccTarget && !ccAction.empty())
    {
        // Break stay/follow in combat so a ranged executor can reach the
        // target. The mature AI remains responsible for subsequent casts.
        RelaxTacticalMovement(ai);
        executor->SetSelectionGuid(ccTarget->GetObjectGuid());
        ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(ccTarget);
        sServerFacade.SetFacingTo(executor, ccTarget);
        ai::Event ccEvent(intent, "cc " + ccMark, requester);
        if (!ExecuteQuietAction(ai, ccAction, ccEvent))
        {
            // Direct execution deliberately bypasses prerequisites. Keep
            // the mature CC action queued so its existing reach
            // prerequisite can move and then cast without a command-side
            // class/range table.
            // Keep this explicit owner request eligible through later
            // minimal ticks while the mature reach chain is in progress.
            if (!QueueMatureAction(ai, ccAction, ccEvent, ACTION_PASSTROUGH))
            {
                // The assignment was saved above: put back the marks, this
                // bot's previous mark and every owner the order revoked.
                if (markMoved)
                    RestoreRaidMarks(context.group, marksBefore);
                ai->GetAiObjectContext()->GetValue<std::string>("rti cc")->Set(previousCcMark);
                sPlayerbotDbStore.Save(ai);
                for (PlayerbotAI* revokedAi : revokedOwners)
                {
                    revokedAi->GetAiObjectContext()->GetValue<std::string>("rti cc")->Set(ccMark);
                    sPlayerbotDbStore.Save(revokedAi);
                }
                SendActionError(handler, reportIntent, "failed",
                    "The mature CC action could not be started.");
                return true;
            }
            // Keep the queued prerequisite chain intact for the first
            // movement/cast decision; the quiet helper would rebuild the
            // non-combat engine and drop it.
            ai->DoNextAction(false, true);
        }
        // While the CC cast bar runs, a forced decision can only cancel
        // it (the order's one Polymorph start never landed); the cast's
        // action duration already schedules the next decision.
        else if (!executor->IsNonMeleeSpellCasted(false, true, true))
            ExecuteQuietNextAction(ai, true);
    }

    SendActionAck(handler, reportIntent, (namedBot || context.selectedBot == executor)
        ? "bot:" + std::string(executor->GetName()) : "party", 1, executor->GetName());
    return true;
}

// The pull/pullback order against context.enemyTarget. Shared by `.bot action
// pull|pullback` and the remote commander's `.bot cmd pull|pullback` (issue
// #884): requester is the event owner, so the pull returns to its position;
// option is the join delay in seconds (empty = configured default);
// reportIntent names the order in the ACK/ERR line.
static bool RunPullOrder(ChatHandler* handler, BotCommandContext const& context, Player* requester,
    std::string const& intent, std::string const& option, std::string const& reportIntent)
{
    Player* executor = ResolvePullExecutor(context);
    if (!executor)
    {
        SendActionError(handler, reportIntent, "no-tank", "No live tank-capable owned party bot can pull this target.");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(executor);
    // Validate before any state changes: a refused pull must not park the
    // party, end a flee or flip the tank's pull mode.
    std::string refuseCode;
    std::string refuseMessage;
    if (!CheckPullTarget(executor, context.enemyTarget, refuseCode, refuseMessage))
    {
        SendActionError(handler, reportIntent, refuseCode.c_str(), refuseMessage.c_str());
        return true;
    }
    // Join delay: per-command [seconds] (0-60, parsed) or the configured
    // default. For a pullback the window runs from the tank's arrival at
    // the anchor; the hold placed below keeps DPS waiting through the
    // return leg and is re-stamped on arrival.
    bool pullback = intent == "pullback";
    uint32 joinDelay = pullback
        ? sPlayerbotAIConfig.pullBackDpsJoinDelay
        : sPlayerbotAIConfig.pullDpsJoinDelay;
    if (!option.empty())
        joinDelay = static_cast<uint32>(std::stoi(option));
    // Pull and Pullback share the mature request action; the per-command
    // return mode (plus join delay) is recorded on the PullStrategy
    // (never sticky). The tank's default is restored when the pull ends.
    PullModeSnapshot pullModeBefore = SavePullMode(ai);
    if (!ConfigurePullMode(ai, pullback, joinDelay))
    {
        char const* message = pullback
            ? "The mature pullback strategy is unavailable."
            : "The mature ordinary-pull strategy is unavailable.";
        SendActionError(handler, reportIntent, "pull-policy", message);
        return true;
    }

    // Report a body-pull substitution honestly: "reach pull" means the
    // tank has no working ranged option, so it walks in, hits the mob in
    // melee once, then returns to the anchor like a ranged pullback. The
    // ACK detail carries it for both intents.
    std::string pullActionName;
    if (PullStrategy* probe = PullStrategy::Get(ai))
        pullActionName = probe->GetPullActionName();
    bool bodyPull = pullActionName == "reach pull";

    // Snapshot the target GUID: the live selection can change between
    // command validation and the pull tick.
    ai::Event pullEvent(intent, context.enemyTarget->GetObjectGuid(), requester);
    if (!ExecuteQuietAction(ai, "pull my target", pullEvent))
    {
        // Not caught by the pre-check: put the tank's pull mode back.
        // No bot's movement, flee or hold was touched yet.
        RestorePullMode(ai, pullModeBefore);
        SendActionError(handler, reportIntent, "failed", "The native pull strategy rejected the target.");
        return true;
    }
    // The pull is accepted (the request only records state): now the tank
    // may break its stay, a fled party comes back and the DPS hold at your
    // position.
    RelaxTacticalMovement(ai);
    for (Player* member : context.partyBots)
        EndFlee(PlayerbotAIStorage::Instance().GetAI(member));
    PausePartyDpsForPull(context, executor, joinDelay, pullback);
    // A human order acts now: the tank's own action clock (left over from
    // whatever it was doing) would otherwise stall the pull for the rest of
    // its delay before the second pull tick ever runs - the reported "tank
    // stands still for seconds, then starts pulling".
    ai->ClearActionClock();
    // Pull start/action are normal movement-priority actions.  A minimal
    // step filters them out and leaves the tank waiting for a later tick.
    ExecuteQuietNextAction(ai, false);
    // The first step only runs "pull start". The walk to the firing position
    // is the next one ("pull action" with its "reach pull" prerequisite), so
    // whether the move started can only be judged after that step - judging
    // it after the first one aborted every ranged pull the moment it began.
    if (!bodyPull)
        ExecuteQuietNextAction(ai, false);

    // A ranged pull closes the distance with a point move to a firing
    // position. If that move cannot be launched the tank stands where it
    // was and the pull can only run out its 15 s window - report it instead
    // of acknowledging a pull that never started (issue #389). A body pull
    // is exempt: its melee chase is launched by the core on the next
    // movement tick, not inside this AI tick.
    if (!bodyPull)
    {
        Unit* pullTarget = nullptr;
        float pullRange = 0.0f;
        if (PullStrategy* probe = PullStrategy::Get(ai))
        {
            pullTarget = probe->GetTarget();
            pullRange = probe->GetRange();
        }

        bool started = !pullTarget || !pullTarget->IsInWorld() ||
            sServerFacade.isMoving(executor) ||
            executor->GetDistance(pullTarget) <= pullRange;

        if (!started)
        {
            // Abort the way the pull timeout would, so the tank is not left
            // holding the pull target or the party.
            ExecuteQuietAction(ai, "pull end", ai::Event(intent, "", requester));
            SendActionError(handler, reportIntent, "unreachable",
                "The tank could not start moving to a firing position for the pull.");
            return true;
        }
    }

    std::string scope = context.selectedBot == executor
        ? "bot:" + std::string(executor->GetName()) : "party";
    if (bodyPull)
        SendActionAck(handler, reportIntent, scope, 1, std::string(executor->GetName()) + " body-pull");
    else
        SendActionAck(handler, reportIntent, scope, 1, executor->GetName());
    return true;
}

static bool HandleAction(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::string intent;
    std::string option;
    if (!requester || !ParseAction(Trim(args ? args : ""), intent, option))
    {
        if (intent.empty())
        {
            // The addon drops an ACTION_ERR with an empty intent as malformed,
            // so name what was typed: the first word, at most 32 bytes and never
            // cut inside a UTF-8 character. ProtocolSafe runs before the trim, so
            // a word of only '|' characters becomes "unknown" like an empty one.
            std::string const typed = Trim(args ? args : "");
            size_t const end = typed.find_first_of(" \t");
            size_t length = end == std::string::npos ? typed.size() : end;
            if (length > 32)
            {
                length = 32;
                // typed[length] is the first byte left out. A continuation byte
                // (10xxxxxx) there means the cut would split a multibyte character,
                // so step back to that character's lead byte.
                while (length > 0 && (static_cast<unsigned char>(typed[length]) & 0xC0) == 0x80)
                    --length;
            }
            intent = Trim(ProtocolSafe(typed.substr(0, length)));
            if (intent.empty())
                intent = "unknown";
        }
        SendActionError(handler, intent, "invalid", "Usage: .bot action attack/interrupt/stop/pull [seconds]/pullback [seconds]/come/stay/hold/follow/flee/focus skull/cc <mark> [bot]/cc clear [bot]/auto cc [on/off]/sap [on/off] [bot]/aoe [on/off]/loot [on/off]/repair/sell/rest/drink/eat/release/corpse run/learn/trade/ready/raid [status/tankface/douse/custom status/custom on/custom off]");
        return true;
    }
    if (!requester->IsInWorld() || !requester->IsAlive() || requester->IsBeingTeleported())
    {
        SendActionError(handler, intent, "requester", "Requester must be alive, in world, and not teleporting.");
        return true;
    }

    BotCommandContext context = BuildContext(requester);
    bool tactical = intent == "pull" || intent == "pullback";
    if ((intent == "attack" || intent == "interrupt" || tactical) && !context.enemyTarget)
    {
        SendActionError(handler, intent, "no-target", "Select a live non-bot target first.");
        return true;
    }
    // A corpse stays selected after the kill; an order against it would ACK
    // and do nothing (or, for a pull, be refused without a reason).
    if ((intent == "attack" || intent == "interrupt" || tactical) && !context.enemyTarget->IsAlive())
    {
        SendActionError(handler, intent, "dead-target", "Your target is dead. Select a live enemy first.");
        return true;
    }
    if (intent == "interrupt")
        return ExecuteInterruptAction(handler, context, requester);

    std::vector<Player*> scope;
    Player* sapNamedBot = nullptr;
    if (intent == "focus skull")
    {
        if (context.group && context.enemyTarget)
        {
            context.group->SetTargetIcon(static_cast<uint8>(RtiTargetValue::GetRtiIndex("skull")), context.enemyTarget->GetObjectGuid());
        }
        else if (context.group)
        {
            int skullIndex = RtiTargetValue::GetRtiIndex("skull");
            ObjectGuid skullGuid = context.group->GetTargetIcon(static_cast<uint8>(skullIndex));
            if (skullGuid.IsEmpty())
            {
                SendActionError(handler, intent, "no-target", "Select an enemy target or mark a target with Skull first.");
                return true;
            }
        }
        else if (!context.enemyTarget)
        {
            SendActionError(handler, intent, "no-target", "Select an enemy target or mark a target with Skull first.");
            return true;
        }
        scope = context.partyBots;
    }
    else if (intent == "cc clear" || intent.compare(0, 9, "cc clear ") == 0)
    {
        // Dismissal: an explicit name clears only that bot; a targeted bot
        // clears itself; otherwise the whole owned party is cleared. "none"
        // is not a raid mark, so CurrentCcMark reports "-" and the next
        // CC_ASSIGN snapshot drops the owner.
        std::vector<Player*> clearScope;
        if (intent.size() > 9)
        {
            Player* named = nullptr;
            BotRecord* namedRecord = nullptr;
            std::string namedName;
            if (!ResolveOwnedBot(handler, intent.substr(9).c_str(), named, namedRecord, namedName))
            {
                SendActionError(handler, intent, "no-bot",
                    "Unknown or uncontrollable bot name for cc clear.");
                return true;
            }
            clearScope.push_back(named);
        }
        else if (context.selectedBot && IsLiveHeadlessBot(context.selectedBot))
            clearScope.push_back(context.selectedBot);
        else
            clearScope = context.partyBots;
        uint32 cleared = 0;
        std::string clearedName = "-";
        for (Player* bot : clearScope)
        {
            if (!bot || !IsLiveHeadlessBot(bot))
                continue;
            PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!botAi || !botAi->GetAiObjectContext())
                continue;
            botAi->GetAiObjectContext()->GetValue<std::string>("rti cc")->Set("none");
            sPlayerbotDbStore.Save(botAi);
            clearedName = bot->GetName();
            ++cleared;
        }
        if (!cleared)
        {
            SendActionError(handler, intent, "no-target",
                "Select an owned bot or have owned party bots to clear CC marks.");
            return true;
        }
        SendActionAck(handler, intent,
            clearScope.size() == 1 && cleared == 1
                ? "bot:" + clearedName : "party",
            cleared, clearedName);
        return true;
    }
    else if (intent.compare(0, 3, "cc ") == 0)
        return RunCcOrder(handler, context, requester, intent, intent);
    else if (intent == "raid status" || intent == "raid tankface" || intent == "raid douse" || intent == "raid custom status" || intent == "raid custom on" || intent == "raid custom off")
        return HandleRaidAction(handler, context, requester, intent);
    else if (intent == "sap" && option.find(' ') != std::string::npos)
    {
        // "sap <mode> <BotName>": only that owned bot; option keeps the mode.
        std::string sapBotToken = option.substr(option.find(' ') + 1);
        option = option.substr(0, option.find(' '));
        BotRecord* namedRecord = nullptr;
        std::string namedName;
        if (!ResolveOwnedBot(handler, sapBotToken.c_str(), sapNamedBot, namedRecord, namedName))
        {
            SendActionError(handler, intent, "no-bot", "Unknown or uncontrollable bot name for sap.");
            return true;
        }
        scope.push_back(sapNamedBot);
    }
    else
        scope = ResolveDynamicScope(context);
    if (tactical)
        return RunPullOrder(handler, context, requester, intent, option, intent);

    if (scope.empty())
    {
        SendActionError(handler, intent, "no-bots", "No live owned party bots are controllable.");
        return true;
    }

    uint32 succeeded = 0;
    std::string aoeState;
    // Per-bot reasons a rest order did not start, for the refusal (#916).
    std::vector<std::pair<std::string, std::string>> restRefusals;
    for (Player* bot : scope)
    {
        // Scope entries are resolved live from ObjectAccessor, but earlier
        // iterations run full AI actions; re-check before dereferencing so a
        // bot that logged out mid-loop is skipped, not crashed into (#225).
        if (!bot || !bot->IsInWorld())
            continue;

        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;

        // A movement order supersedes the bot's movement orders still queued
        // from chat: `/p stay` then `.bot action follow` in one tick reach the
        // AI in that order, but the chat line only runs on the bot's next AI
        // turns, after this order (runtime/QueuedOrderPolicy.h). The tactical
        // orders do the same through RelaxTacticalMovement.
        if (intent == "follow" || intent == "stay" || intent == "flee" ||
            intent == "come" || intent == "hold" || intent == "comestay")
            ai->DropQueuedMovementOrders();

        bool accepted = false;
        if (intent == "attack")
        {
            // AttackMyTargetAction intentionally reads requester's selection,
            // preserving the mature target validation and combat path.
            // Break stay and follow in combat so bots can move to and fight the target!
            RelaxTacticalMovement(ai);
            // Explicit attack unleashes DPS early: drop the wait window and
            // our anchor stay (never a player-placed stay). No-op when absent.
            // Release this bot itself, before it acts (releasing only the
            // other party bots left the first bot attacking while still held
            // and a lone scoped bot never released).
            ai->ChangeStrategy("-wait for attack", BotState::BOT_STATE_ALL);
            // Dedicated healers should support the party, not be forced to target and attack the enemy
            if (!PlayerbotAI::IsHeal(bot))
            {
                accepted = ExecuteQuietAction(ai, "attack my target",
                    ai::Event("action attack", "", requester));
            }
            else
            {
                accepted = true;
            }
            // Caster rotations and party heals are below the minimal-action
            // cutoff.  Run a normal first combat decision after committing the
            // explicit target so ranged bots engage alongside melee bots.
            ExecuteQuietNextAction(ai, false);
        }
        else if (intent == "stop")
        {
            // Keep healing and movement strategies enabled: stop only combat,
            // then use the mature narrow reset/target cleanup.
            bot->CombatStopWithPets(true);
            ai->Reset(false);
            bot->GetMotionMaster()->Clear();
            accepted = true;
        }
        else if (intent == "follow")
        {
            // FollowChatShortcutAction owns the cross-engine movement
            // transition; this narrow clear only discards a stale movement
            // generator left by the preceding tactical action.
            if (!BindMovementMaster(requester, bot))
                continue;
            bot->GetMotionMaster()->Clear();
            accepted = ExecuteQuietAction(ai, "follow chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "flee")
        {
            // Break off: drop combat and any pull hold, then the mature flee
            // shortcut parks the bot in passive follow on the requester. The
            // next tactical order (attack, pull, follow, stay...) ends it.
            ai->ChangeStrategy("-wait for attack", BotState::BOT_STATE_ALL);
            bot->CombatStopWithPets(true);
            ai->Reset(false);
            bot->GetMotionMaster()->Clear();
            if (!BindMovementMaster(requester, bot))
                continue;
            accepted = ExecuteQuietAction(ai, "flee chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "stay")
        {
            accepted = ExecuteQuietAction(ai, "stay chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "come" || intent == "hold" || intent == "comestay")
        {
            // The latest explicit order wins over a pending summon's follow
            // restore: hold/comestay keep the bot holding, come ("come, then
            // follow") lets the restore run again.
            if (intent == "come")
                TortoiseBots::PlayerConvenience::Instance().NoteComeOrder(bot->GetObjectGuid());
            else
                TortoiseBots::PlayerConvenience::Instance().NoteHoldOrder(bot->GetObjectGuid());
            ai->Reset();
            // Use the same owner-controlled transition as the mature Stay
            // shortcut; the captured stay position below remains Come's
            // regroup anchor.
            ai->SetMovementStrategy("stay");
            ai::PositionMap& posMap = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
            ai::PositionEntry pos = posMap["stay"];
            pos.Set(WorldPosition(requester));
            posMap["stay"] = pos;
            accepted = ExecuteQuietAction(ai, "return to stay position",
                ai::Event("return to stay position", "stay", requester));
        }
        else if (intent == "ready")
        {
            accepted = ExecuteQuietAction(ai, "ready check",
                ai::Event(intent, "", requester));
        }
        else if (intent == "rest" || intent == "drink" || intent == "eat")
        {
            bool rested = false;
            if (bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA))
                rested = ExecuteQuietAction(ai, "drink", ai::Event(intent, "drink", requester)) || rested;
            if (bot->GetHealth() < bot->GetMaxHealth())
                rested = ExecuteQuietAction(ai, "food", ai::Event(intent, "food", requester)) || rested;
            accepted = rested || (bot->GetHealth() >= bot->GetMaxHealth() &&
                (bot->GetPowerType() != POWER_MANA || bot->GetPower(POWER_MANA) >= bot->GetMaxPower(POWER_MANA)));
            if (!accepted)
            {
                // Name the gate that held so the caller can wait instead of
                // guessing (#916). The stop lines and the should-values are
                // the ones the drink/food actions read.
                bool const cheat = ai->HasCheat(BotCheatMask::item);
                ai::AiObjectContext* aiContext = ai->GetAiObjectContext();
                ai::RestRefusalInput in;
                in.alive = bot->IsAlive();
                in.inCombat = bot->IsInCombat();
                in.groupFighting = aiContext->GetValue<bool>("has attackers")->Get() ||
                    ai->IsGroupMemberNearInCombat();
                in.usesMana = bot->GetPowerType() == POWER_MANA;
                in.manaPct = in.usesMana && bot->GetMaxPower(POWER_MANA)
                    ? bot->GetPower(POWER_MANA) * 100 / bot->GetMaxPower(POWER_MANA) : 100;
                in.drinkStopPct = ai::DrinkStopManaPct(cheat, sPlayerbotAIConfig.almostFullHealth);
                in.healthPct = bot->GetMaxHealth() ? bot->GetHealth() * 100 / bot->GetMaxHealth() : 100;
                in.eatStopPct = ai::RestStopHealthPct(cheat, sPlayerbotAIConfig.mediumHealth,
                    sPlayerbotAIConfig.lowHealth, sPlayerbotAIConfig.almostFullHealth);
                in.hasDrink = cheat ||
                    !aiContext->GetValue<std::list<Item*>>("inventory items", "drink")->Get().empty();
                in.hasFood = cheat ||
                    !aiContext->GetValue<std::list<Item*>>("inventory items", "food")->Get().empty();
                in.shouldDrink = aiContext->GetValue<bool>("should drink")->Get();
                in.shouldEat = aiContext->GetValue<bool>("should eat")->Get();
                restRefusals.emplace_back(bot->GetName(), ai::RestRefusalReason(in));
            }
        }
        else if (intent == "aoe")
        {
            // Same switch as `.bot behavior <bot> aoe`: off adds the
            // "no aoe" veto on every area spell, on removes it. Persisted
            // like the behaviour toggles.
            BehaviorToggle const& aoeToggle = *FindBehaviorToggle("aoe");
            bool enable = option == "on" ||
                (option.empty() && !IsBehaviorOn(ai, aoeToggle));
            if (option == "off")
                enable = false;
            SetBehavior(ai, aoeToggle, enable);
            sPlayerbotDbStore.Save(ai);
            ExecuteQuietNextAction(ai, true);
            accepted = IsBehaviorOn(ai, aoeToggle) == enable;
        }
        else if (intent == "auto cc")
        {
            // Opt-in smart auto CC, OFF by default. Persisted like the loot
            // toggle so the choice survives relog; explicit `.bot action cc`
            // marks always win (CcTargetValue returns them first).
            bool enable = option == "on" ||
                (option.empty() && !ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT));
            if (option == "off")
                enable = false;
            ai->ChangeStrategy((enable ? "+" : "-") + std::string("auto cc"),
                BotState::BOT_STATE_COMBAT);
            sPlayerbotDbStore.Save(ai);
            ExecuteQuietNextAction(ai, true);
            accepted = ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT) == enable;
        }
        else if (intent == "sap")
        {
            // Pre-pull Sap on the CC mark, ON by default: "off" adds the
            // opt-out strategy "no sap", "on" removes it. Persisted like the
            // loot toggle. Other classes have no such strategy: skip them.
            if (bot->GetClass() != CLASS_ROGUE)
                continue;
            bool enable = option == "on" ||
                (option == "toggle" && ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT));
            ai->ChangeStrategy((enable ? "-" : "+") + std::string("no sap"),
                BotState::BOT_STATE_NON_COMBAT);
            sPlayerbotDbStore.Save(ai);
            accepted = ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT) != enable;
        }
        else if (intent == "focus skull")
        {
            RelaxTacticalMovement(ai);
            bool set = ExecuteQuietAction(ai, "rti",
                ai::Event("focus skull", "skull", requester));
            accepted = set && ExecuteQuietAction(ai, "attack rti target",
                ai::Event("focus skull", "", requester));
            ExecuteQuietNextAction(ai, true);
        }
        else if (intent == "loot")
        {
            bool enable = option == "on" ||
                (option.empty() && !ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT));
            if (option == "off")
                enable = false;
            ai->ChangeStrategy((enable ? "+" : "-") + std::string("loot"),
                BotState::BOT_STATE_NON_COMBAT);
            sPlayerbotDbStore.Save(ai);
            accepted = ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT) == enable;
        }
        else if (intent == "repair")
        {
            accepted = ExecuteQuietAction(ai, "repair", ai::Event(intent, "repair", requester));
        }
        else if (intent == "sell")
        {
            accepted = ExecuteQuietAction(ai, "sell", ai::Event(intent, "sell", requester));
        }
        else if (intent == "release")
        {
            if (!bot || bot->IsAlive())
                continue;
            accepted = ExecuteQuietAction(ai, "release", ai::Event(intent, "release", requester));
        }
        else if (intent == "corpse run")
        {
            if (!bot || bot->IsAlive())
                continue;
            accepted = ExecuteQuietAction(ai, "corpse run", ai::Event(intent, "corpse run", requester));
        }
        else if (intent == "learn")
        {
            accepted = ExecuteQuietAction(ai, "trainer", ai::Event(intent, "learn", requester));
        }
        else if (intent == "trade")
        {
            if (context.selectedBot != bot)
                continue;
            accepted = ExecuteQuietAction(ai, "trade", ai::Event(intent, requester->GetName(), requester));
        }

        if (accepted)
        {
            ++succeeded;
            if (intent == "aoe")
            {
                std::string current = IsBehaviorOn(ai, *FindBehaviorToggle("aoe"))
                    ? "on" : "off";
                if (aoeState.empty())
                    aoeState = current;
                else if (aoeState != current)
                    aoeState = "mixed";
            }
            if (intent == "loot")
            {
                std::string current = ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT)
                    ? "on" : "off";
                if (aoeState.empty())
                    aoeState = current;
                else if (aoeState != current)
                    aoeState = "mixed";
            }
            if (intent == "auto cc")
            {
                std::string current = ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT)
                    ? "on" : "off";
                if (aoeState.empty())
                    aoeState = current;
                else if (aoeState != current)
                    aoeState = "mixed";
            }
            if (intent == "sap")
            {
                std::string current = ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT)
                    ? "off" : "on";
                if (aoeState.empty())
                    aoeState = current;
                else if (aoeState != current)
                    aoeState = "mixed";
            }
        }
    }

    if (!succeeded && intent == "sap")
    {
        SendActionError(handler, intent, "no-rogue", "No rogue bot in scope: name, select or group a rogue bot.");
        return true;
    }

    if (!succeeded)
    {
        std::string const restText = ai::FormatRestRefusal(restRefusals);
        SendActionError(handler, intent, "failed", restText.empty()
            ? "No scoped bot accepted the mature action." : restText.c_str());
        return true;
    }

    std::string scopeName = sapNamedBot ? "bot:" + std::string(sapNamedBot->GetName())
        : context.selectedBot && scope.size() == 1
        ? "bot:" + std::string(context.selectedBot->GetName()) : "party";
    SendActionAck(handler, intent, scopeName, succeeded,
        intent == "aoe" || intent == "loot" || intent == "auto cc" || intent == "sap" ? aoeState : "");
    return true;
}

static bool HandleRaidAction(ChatHandler* handler, BotCommandContext const& context, Player* requester, std::string const& intent)
{
    std::vector<Player*> scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        SendActionError(handler, intent, "no-bots", "No live owned party bots are controllable.");
        return true;
    }
    if (intent == "raid status")
    {
        uint32 count = 0;
        for (Player* bot : scope)
        {
            if (!bot || !bot->IsInWorld())
                continue;
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai)
                continue;
            std::string active;
            for (char const* strategy : { "molten core", "onyxia's lair", "blackwing lair", "naxxramas",
                                          "zul'gurub", "ruins of ahn'qiraj", "ahn'qiraj temple",
                                          "onyxia", "magmadar", "suppression room", "four horseman", "sapphiron",
                                          "heigan", "anub'rekhan", "gluth", "thaddius", "kel'thuzad",
                                          "grobbulus",
                                          "loatheb",
                                          "ossirian",
                                          "geddon",
                                          "emerald sanctum", "lower karazhan", "karazhan crypt",
                                          "solnius", "araxxna", "moroes" })
            {
                if (ai->HasStrategy(strategy, BotState::BOT_STATE_COMBAT))
                    active += (active.empty() ? "" : ",") + std::string(strategy);
            }
            handler->PSendSysMessage("TBM:ACTION_ACK|raid status|bot:%s|1|%s",
                bot->GetName(), active.empty() ? "outdoor" : active.c_str());
            ++count;
        }
        if (!count)
            SendActionError(handler, intent, "failed", "No scoped bot accepted the mature action.");
        return true;
    }
    if (intent == "raid custom status" || intent == "raid custom on" || intent == "raid custom off")
    {
        // Custom Turtle raid tactics gate (Emerald Sanctum / Lower Karazhan /
        // Karazhan Crypt): enabling adds the three transition strategies so
        // zone-ins swap them in; disabling drops them for manual control.
        // Playtesting-dependent geometry stays out regardless (#237).
        uint32 succeeded = 0;
        for (Player* bot : scope)
        {
            if (!bot || !bot->IsInWorld())
                continue;
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai)
                continue;
            if (intent == "raid custom status")
            {
                std::string active;
                for (char const* strategy : { "emerald sanctum", "lower karazhan", "karazhan crypt" })
                {
                    if (ai->HasStrategy(strategy, BotState::BOT_STATE_COMBAT))
                        active += (active.empty() ? "" : ",") + std::string(strategy);
                }
                handler->PSendSysMessage("TBM:ACTION_ACK|raid custom status|bot:%s|1|%s",
                    bot->GetName(), active.empty() ? "off" : active.c_str());
                ++succeeded;
                continue;
            }
            bool enable = intent == "raid custom on";
            for (BotState state : { BotState::BOT_STATE_COMBAT, BotState::BOT_STATE_NON_COMBAT })
            {
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("emerald sanctum"), state);
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("lower karazhan"), state);
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("karazhan crypt"), state);
            }
            ++succeeded;
        }
        if (!succeeded)
        {
            SendActionError(handler, intent, "failed", "No scoped bot accepted the mature action.");
            return true;
        }
        std::string scopeName = context.selectedBot && scope.size() == 1
            ? "bot:" + std::string(context.selectedBot->GetName()) : "party";
        SendActionAck(handler, intent, scopeName, succeeded, "");
        return true;
    }
    uint32 succeeded = 0;
    char const* matureAction = intent == "raid tankface" ? "dragon tank face away" : "douse mc rune eternal";
    for (Player* bot : scope)
    {
        if (!bot || !bot->IsInWorld())
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        bool accepted = ExecuteQuietAction(ai, matureAction, ai::Event(intent, "", requester));
        if (!accepted && intent == "raid douse")
            accepted = ExecuteQuietAction(ai, "douse mc rune aqual", ai::Event(intent, "", requester));
        if (accepted)
            ++succeeded;
    }
    if (!succeeded)
    {
        SendActionError(handler, intent, "failed", "No scoped bot accepted the mature action.");
        return true;
    }
    std::string scopeName = context.selectedBot && scope.size() == 1
        ? "bot:" + std::string(context.selectedBot->GetName()) : "party";
    SendActionAck(handler, intent, scopeName, succeeded, "");
    return true;
}

// ---------------------------------------------------------------------------
// Remote commander (issue #884, phase 1)
//
// `.bot commander bind|unbind|status` and `.bot cmd <order>`, behind
// AiPlayerbot.RemoteCommander (default 0). A commander (a GM or the owner of
// every bot) binds a bot-only party of up to five, led by one of its bots, and
// leads it from anywhere: orders name their target by GUID or a 3D point and
// reach the group through the binding, not through the commander's selection
// or group membership. The decisions are in runtime/RemoteCommanderPolicy.h;
// this is the world glue. World thread only (the command queue drains here),
// like every other `.bot` command.
//
// Nothing here touches a bot's durable master: the followers follow the lead
// bot through their per-bot "manual follow target" value (the AI's own
// FollowTargetValue override, the value behind the whispered `follow target`),
// which the binding sets and puts back on release.
// ---------------------------------------------------------------------------

namespace {

RemoteCommander::BindingTable& CommanderBindings()
{
    static RemoteCommander::BindingTable table;
    return table;
}

// Each follower's own "manual follow target" from before the binding pointed
// it at the lead bot, put back on release.
std::unordered_map<uint64, ai::GuidPosition>& SavedFollowTargets()
{
    static std::unordered_map<uint64, ai::GuidPosition> saved;
    return saved;
}

uint32 s_commanderSweepMs = 0;

// Issue #884, phase 2: one perception feed per binding (`.bot commander view
// on|off`). Made on bind, off until the commander turns it on; dropped on
// release; nothing here runs while none exists. The frame's lines are reused
// frame to frame.
struct CommanderView
{
    bool enabled = false;
    uint32 seq = 0;
    RemoteCommander::View::Pacer pacer;
    RemoteCommander::View::Frame frame;
};

std::unordered_map<uint64, CommanderView>& CommanderViews()
{
    static std::unordered_map<uint64, CommanderView> views;
    return views;
}

// The character with this raw guid, also while it is between maps (a zoning
// commander or lead bot must not lose the binding).
Player* FindCharacter(uint64 raw)
{
    return raw ? ObjectAccessor::FindPlayerNotInWorld(ObjectGuid(raw)) : nullptr;
}

Player* FindOnlineByName(std::string name)
{
    name = Trim(name);
    if (name.empty() || !normalizePlayerName(name))
        return nullptr;
    Player* player = sObjectMgr.GetPlayer(name.c_str());
    if (!player)
        player = sObjectAccessor.FindPlayerByName(name.c_str());
    return player;
}

// Every live bot of the group, the lead bot first, resolved from the group's
// member slots through ObjectAccessor (never the GroupReference list, #225).
// Fills a caller-kept vector (the view feed reuses one).
void FillCommandedBots(Group* group, Player* lead, std::vector<Player*>& bots)
{
    bots.clear();
    if (!group || !lead)
        return;
    if (IsLiveHeadlessBot(lead))
        bots.push_back(lead);
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.guid == lead->GetObjectGuid())
            continue;
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (member && IsLiveHeadlessBot(member))
            bots.push_back(member);
    }
}

std::vector<Player*> CommandedBots(Group* group, Player* lead)
{
    std::vector<Player*> bots;
    FillCommandedBots(group, lead, bots);
    return bots;
}

RemoteCommander::BindingState ReadBindingState(RemoteCommander::Binding const& binding)
{
    RemoteCommander::BindingState state;
    state.enabled = sPlayerbotAIConfig.remoteCommander;
    Player* commander = FindCharacter(binding.commanderGuid);
    state.commanderOnline = commander && commander->GetSession() &&
        commander->GetSession()->HasNetworkTransport();
    Player* lead = FindCharacter(binding.leadGuid);
    state.leadOnline = lead != nullptr;
    if (Group* group = lead ? lead->GetGroup() : nullptr)
    {
        state.leadGroupId = group->GetId();
        state.leadIsLeader = group->IsLeader(lead->GetObjectGuid());
        state.leadGroupIsRaidOrBg = group->isRaidGroup() || group->isBGGroup();
        state.memberCount = group->GetMembersCount();
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (!BotManager::Instance().IsBot(slot.guid))
                state.realPlayerInGroup = true;
    }
    return state;
}

ai::Value<ai::GuidPosition>* ManualFollowTarget(PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return nullptr;
    return ai->GetAiObjectContext()->GetValue<ai::GuidPosition>("manual follow target");
}

void ResetFollowTargetCache(PlayerbotAI* ai)
{
    if (ai::Value<Unit*>* follow = ai->GetAiObjectContext()->GetValue<Unit*>("follow target"))
        follow->Reset();
}

// Point one follower's follow target at the lead bot, keeping its own value
// for the release.
void PointFollowerAtLead(uint64 commanderRaw, Player* bot, Player* lead)
{
    if (!bot || !lead || bot == lead)
        return;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    ai::Value<ai::GuidPosition>* value = ManualFollowTarget(ai);
    if (!value)
        return;
    uint64 const botRaw = bot->GetObjectGuid().GetRawValue();
    auto& saved = SavedFollowTargets();
    if (saved.find(botRaw) == saved.end())
    {
        ai::GuidPosition previous = value->Get();
        saved[botRaw] = previous.GetRawValue() == lead->GetObjectGuid().GetRawValue()
            ? ai::GuidPosition() : previous;
    }
    value->Set(ai::GuidPosition(lead));
    ResetFollowTargetCache(ai);
    CommanderBindings().AddFollower(commanderRaw, botRaw);
}

// Put a follower's own follow target back, unless something else changed it
// while it was bound.
void RestoreFollower(uint64 botRaw, uint64 leadRaw)
{
    auto& saved = SavedFollowTargets();
    auto it = saved.find(botRaw);
    ai::GuidPosition previous = it != saved.end() ? it->second : ai::GuidPosition();
    if (it != saved.end())
        saved.erase(it);
    Player* bot = FindCharacter(botRaw);
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    ai::Value<ai::GuidPosition>* value = ManualFollowTarget(ai);
    if (!value || value->Get().GetRawValue() != leadRaw)
        return;
    value->Set(previous);
    ResetFollowTargetCache(ai);
}

// Ends a binding: followers get their own follow target back. The lead bot
// keeps holding where it stands. notify: tell the commander (automatic
// release); an explicit unbind answers through its own ACK.
uint32 ReleaseBinding(uint64 commanderRaw, RemoteCommander::ReleaseReason reason, bool notify)
{
    RemoteCommander::Binding removed = CommanderBindings().Unbind(commanderRaw);
    CommanderViews().erase(commanderRaw);
    if (!removed.commanderGuid)
        return 0;
    for (uint64 follower : removed.followers)
        RestoreFollower(follower, removed.leadGuid);
    Player* commander = FindCharacter(commanderRaw);
    Player* lead = FindCharacter(removed.leadGuid);
    TB_LOG_DETAIL("TortoiseBots: remote commander %s released group of %s: %s",
        commander ? commander->GetName() : "(offline)", lead ? lead->GetName() : "(offline)",
        RemoteCommander::ReleaseReasonText(reason));
    if (notify && commander && commander->IsInWorld() && commander->GetSession())
    {
        ChatHandler(commander).PSendSysMessage("TBM:ACTION_ACK|commander unbind|party|%u|%s",
            static_cast<uint32>(removed.followers.size()),
            ProtocolSafe(RemoteCommander::ReleaseReasonText(reason)).c_str());
    }
    return static_cast<uint32>(removed.followers.size());
}

// Followers that left the bound group (kicked, invited by their owner into
// another party, left on their own) get their own follow target back at once;
// otherwise they would keep walking after the lead bot and ignore their
// owner's orders. The rest of the binding stands.
void ReleaseDepartedFollowers(uint64 commanderRaw, RemoteCommander::Binding const& binding)
{
    if (binding.followers.empty())
        return;
    std::vector<uint64> members;
    Player* lead = FindCharacter(binding.leadGuid);
    if (Group* group = lead ? lead->GetGroup() : nullptr)
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            members.push_back(slot.guid.GetRawValue());
    std::vector<uint64> const departed = RemoteCommander::DepartedFollowers(binding, members);
    uint64 const leadRaw = binding.leadGuid;
    for (uint64 follower : departed)
    {
        RestoreFollower(follower, leadRaw);
        CommanderBindings().RemoveFollower(commanderRaw, follower);
    }
}

// The requester's binding if it still stands (a stale one is released here,
// a follower that left the group is let go).
RemoteCommander::Binding const* StandingBinding(uint64 commanderRaw)
{
    RemoteCommander::Binding const* binding = CommanderBindings().FindByCommander(commanderRaw);
    if (!binding)
        return nullptr;
    RemoteCommander::ReleaseReason reason = RemoteCommander::CheckBinding(*binding, ReadBindingState(*binding));
    if (reason == RemoteCommander::ReleaseReason::None)
    {
        ReleaseDepartedFollowers(commanderRaw, *binding);
        return CommanderBindings().FindByCommander(commanderRaw);
    }
    ReleaseBinding(commanderRaw, reason, true);
    return nullptr;
}

RemoteCommander::BindMember MemberFacts(Player* commander, Player* bot)
{
    RemoteCommander::BindMember facts;
    if (!bot)
        return facts;
    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    facts.controllable = record && CanControlBot(commander, record);
    facts.liveBot = record && IsLiveHeadlessBot(bot, record);
    facts.hired = HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
    // One binding per bot: its saved follow target belongs to that binding.
    facts.inOtherBinding = CommanderBindings().FindByBot(bot->GetObjectGuid().GetRawValue()) != nullptr;
    return facts;
}

// Take a bot out of the commander's own party (the commander asked for it
// by name); the group pointer is read again because removing a member can
// disband it.
void LeaveCommanderParty(Player* commander, Player* bot)
{
    Group* group = commander ? commander->GetGroup() : nullptr;
    if (group && group->IsMember(bot->GetObjectGuid()))
        group->RemoveMember(bot->GetObjectGuid(), 0);
}

// The lead bot invites, the member accepts through the native handlers - the
// same opcodes the AI's own invite/accept actions use, without the accept
// action's master rebind: the member keeps its durable master.
bool JoinLeadGroup(Player* lead, Player* member)
{
    if (lead->GetGroup() && member->GetGroup() == lead->GetGroup())
        return true;
    if (Group* group = lead->GetGroup())
        if (group->IsFull())
            return false;
    if (member->GetGroupInvite())
        member->UninviteFromGroup();
    WorldPacket invite;
    invite << member->GetName() << uint32(0);
    lead->GetSession()->HandleGroupInviteOpcode(invite);
    if (!member->GetGroupInvite())
        return false;
    WorldPacket accept;
    accept << uint32(0);
    member->GetSession()->HandleGroupAcceptOpcode(accept);
    return lead->GetGroup() && member->GetGroup() == lead->GetGroup();
}

// Followers follow the lead bot; the lead bot holds until a move order.
// reanchorLead: the lead bot holds where it stands now (bind); otherwise a
// lead bot already holding keeps its spot, so a `follow` during a move does
// not stop the lead bot short. Returns the bots that took the order.
uint32 FormUpOnLead(Player* commander, Player* lead, std::vector<Player*> const& bots,
    bool reanchorLead)
{
    uint32 accepted = 0;
    uint64 const commanderRaw = commander->GetObjectGuid().GetRawValue();
    for (Player* bot : bots)
    {
        if (!bot || !bot->IsInWorld())
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        ai->DropQueuedMovementOrders();
        if (bot == lead)
        {
            if (!reanchorLead && ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
                ++accepted;
            else if (ExecuteQuietAction(ai, "stay chat shortcut", ai::Event("stay", "", commander)))
                ++accepted;
            continue;
        }
        PointFollowerAtLead(commanderRaw, bot, lead);
        bot->GetMotionMaster()->Clear();
        if (ExecuteQuietAction(ai, "follow chat shortcut", ai::Event("follow", "", commander)))
            ++accepted;
    }
    return accepted;
}

// ---------------------------------------------------------------------------
// Perception feed (issue #884, phase 2)
//
// Every AiPlayerbot.RemoteCommanderViewMs a bound commander whose feed is on
// gets one TBM:VIEW frame (grammar: runtime/RemoteCommanderView.h). The
// snapshot is read from what the bound bots already hold - their unit fields,
// their own cached "attackers" and "possible targets" lists (read only when
// already calculated: HasCachedValue, never a fresh calculation), the units
// attacking them, their victim and selection - and, for each hostile found
// that way, its own threat list. Hostiles are resolved by guid on the seeing
// bot's map (a hash lookup): no grid or world scan happens here.
// ---------------------------------------------------------------------------

RemoteCommander::View::Snapshot& ViewSnapshot()
{
    static RemoteCommander::View::Snapshot snapshot;
    return snapshot;
}

uint32 CurrentSpellId(Unit* unit)
{
    if (Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        if (spell->m_spellInfo)
            return spell->m_spellInfo->Id;
    if (Spell* spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        if (spell->m_spellInfo)
            return spell->m_spellInfo->Id;
    return 0;
}

char ViewStateLetter(PlayerbotAI* ai, Player* bot)
{
    if (!bot->IsAlive())
        return 'd';
    if (!ai)
        return 'n';
    switch (ai->GetState())
    {
        case BotState::BOT_STATE_COMBAT: return 'c';
        case BotState::BOT_STATE_DEAD: return 'd';
        case BotState::BOT_STATE_REACTION: return 'r';
        default: return 'n';
    }
}

// One of the bot's guid-list values, only when it already holds a calculated
// list; otherwise nothing (the bot calculates it on its own schedule).
void ReadCachedGuidList(PlayerbotAI* ai, char const* name, std::list<ObjectGuid>& out)
{
    out.clear();
    ai::AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
    if (!context || !context->HasValue(name))
        return;
    auto* value = dynamic_cast<ai::CalculatedValue<std::list<ObjectGuid>>*>(
        context->GetValue<std::list<ObjectGuid>>(name));
    if (value && value->HasCachedValue())
        out = value->LazyGet();
}

// A hostile the bot sees: in world on its map, alive, not friendly, and seen
// (stealth and invisibility detection, as the order targets check it).
Unit* SeenHostile(Player* bot, ObjectGuid guid)
{
    if (guid.IsEmpty() || guid == bot->GetObjectGuid())
        return nullptr;
    Map* map = bot->GetMap();
    Unit* unit = map ? map->GetUnit(guid) : nullptr;
    if (!unit || !unit->IsInWorld() || !unit->IsAlive())
        return nullptr;
    if (BotManager::Instance().IsBot(unit->GetObjectGuid()) || sServerFacade.IsFriendlyTo(unit, bot))
        return nullptr;
    if (!unit->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true))
        return nullptr;
    return unit;
}

void AddViewHostile(RemoteCommander::View::Snapshot& snapshot, RemoteCommander::View::HostileSet& taken,
    Player* bot, ObjectGuid guid)
{
    if (taken.Full() || guid.IsEmpty() || taken.Contains(guid.GetRawValue()))
        return;
    Unit* unit = SeenHostile(bot, guid);
    if (!unit || !taken.Add(guid.GetRawValue()))
        return;
    RemoteCommander::View::Hostile* hostile = snapshot.AddHostile();
    if (!hostile)
        return;
    hostile->guid = guid.GetRawValue();
    hostile->entry = unit->GetEntry();
    hostile->x = unit->GetPositionX();
    hostile->y = unit->GetPositionY();
    hostile->z = unit->GetPositionZ();
    hostile->hpPct = unit->GetMaxHealth()
        ? static_cast<uint32>(unit->GetHealth() * 100ull / unit->GetMaxHealth()) : 0;
    hostile->target = unit->GetTargetGuid().GetRawValue();
    hostile->casting = CurrentSpellId(unit);
    hostile->stealthed = unit->HasStealthAura();
    Creature* creature = unit->ToCreature();
    hostile->evading = creature && creature->IsInEvadeMode();
    hostile->crowdControlled = unit->HasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_CONFUSED |
        UNIT_STAT_FLEEING | UNIT_STAT_ROOT);

    // Engaged: the top of its own threat list, in % of the top entry.
    if (!unit->IsInCombat() || !unit->CanHaveThreatList())
        return;
    ThreatList const& list = unit->GetThreatManager().getThreatList();
    if (list.empty())
        return;
    RemoteCommander::View::ThreatTop* top = snapshot.AddThreat(hostile->guid);
    if (!top)
        return;
    for (HostileReference const* ref : list)
        if (ref)
            top->Push(ref->getUnitGuid().GetRawValue(), ref->getThreat());
    if (!top->count)
        snapshot.DropLastThreat();
}

void CollectViewSnapshot(RemoteCommander::Binding const& binding, RemoteCommander::View::Snapshot& snapshot)
{
    static std::vector<Player*> bots;
    static RemoteCommander::View::HostileSet taken;
    static std::list<ObjectGuid> cached;

    snapshot.Clear();
    snapshot.lead = binding.leadGuid;
    Player* lead = FindCharacter(binding.leadGuid);
    Group* group = lead ? lead->GetGroup() : nullptr;
    if (!group || group->GetId() != binding.groupId)
        return;
    FillCommandedBots(group, lead, bots);

    for (Player* bot : bots)
    {
        RemoteCommander::View::PartyMember& member = snapshot.AddPartyMember();
        member.guid = bot->GetObjectGuid().GetRawValue();
        member.name = bot->GetName();
        member.hp = bot->GetHealth();
        member.maxHp = bot->GetMaxHealth();
        Powers const powerType = bot->GetPowerType();
        member.powerType = static_cast<uint8>(powerType);
        member.power = bot->GetPower(powerType);
        member.maxPower = bot->GetMaxPower(powerType);
        member.x = bot->GetPositionX();
        member.y = bot->GetPositionY();
        member.z = bot->GetPositionZ();
        member.o = bot->GetOrientation();
        member.map = bot->GetMapId();
        member.instance = bot->GetInstanceId();
        ObjectGuid target = bot->GetTargetGuid();
        if (target.IsEmpty())
            if (Unit* victim = bot->GetVictim())
                target = victim->GetObjectGuid();
        member.target = target.GetRawValue();
        member.casting = CurrentSpellId(bot);
        member.dead = !bot->IsAlive();
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        member.state = ViewStateLetter(ai, bot);
        if (ai)
            if (ai::Engine* engine = ai->GetCurrentEngine())
                member.action = engine->GetLastAction();
    }

    taken.Clear();
    for (Player* bot : bots)
    {
        if (taken.Full())
            break;
        if (!bot->IsInWorld() || !bot->IsAlive())
            continue;
        // What it fights first, then what it merely sees.
        if (Unit* victim = bot->GetVictim())
            AddViewHostile(snapshot, taken, bot, victim->GetObjectGuid());
        AddViewHostile(snapshot, taken, bot, bot->GetTargetGuid());
        for (Unit* attacker : bot->GetAttackers())
            if (attacker)
                AddViewHostile(snapshot, taken, bot, attacker->GetObjectGuid());
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        ReadCachedGuidList(ai, "attackers", cached);
        for (ObjectGuid const& guid : cached)
            AddViewHostile(snapshot, taken, bot, guid);
    }
    // Then everything the bots' own sight lists hold, up to the cap.
    for (Player* bot : bots)
    {
        if (taken.Full())
            break;
        if (!bot->IsInWorld() || !bot->IsAlive())
            continue;
        ReadCachedGuidList(PlayerbotAIStorage::Instance().GetAI(bot), "possible targets", cached);
        for (ObjectGuid const& guid : cached)
            AddViewHostile(snapshot, taken, bot, guid);
    }
}

// Each tick while a binding exists: build the frames that fall due, send at
// most kLinesPerTick lines per commander.
void UpdateCommanderViews(uint32 diff)
{
    auto& views = CommanderViews();
    if (views.empty())
        return;
    static std::string scratch;
    uint32 const interval = sPlayerbotAIConfig.remoteCommanderViewMs;
    for (auto& entry : views)
    {
        CommanderView& view = entry.second;
        if (!view.enabled)
            continue;
        RemoteCommander::Binding const* binding = CommanderBindings().FindByCommander(entry.first);
        if (!binding)
            continue;
        Player* commander = FindCharacter(entry.first);
        bool const reachable = commander && commander->IsInWorld() && commander->GetSession();
        if (view.pacer.Due(diff, interval) && reachable)
        {
            CollectViewSnapshot(*binding, ViewSnapshot());
            view.pacer.Start(RemoteCommander::View::BuildFrame(ViewSnapshot(), ++view.seq, view.frame, scratch));
        }
        if (!view.pacer.Pending())
            continue;
        if (!reachable)
        {
            view.pacer.Abandon();
            continue;
        }
        size_t first = 0;
        size_t const count = view.pacer.NextBatch(first);
        ChatHandler handler(commander);
        for (size_t i = 0; i < count; ++i)
            handler.SendSysMessage(view.frame.lines[first + i].c_str());
    }
}

} // namespace

static bool HandleCommanderBind(ChatHandler* handler, Player* commander, std::string const& args)
{
    std::string const intent = "commander bind";
    std::vector<std::string> names = RemoteCommander::SplitOrderArgs(args);
    if (names.empty() || names.size() > RemoteCommander::kMaxGroupSize)
    {
        SendActionError(handler, intent, "invalid", "Usage: .bot commander bind <LeadBot> [<Bot> ...] (a party of up to five bots)");
        return true;
    }

    uint64 const commanderRaw = commander->GetObjectGuid().GetRawValue();
    RemoteCommander::BindFacts facts;
    facts.enabled = sPlayerbotAIConfig.remoteCommander;
    facts.commanderIsBot = BotManager::Instance().IsBot(commander->GetObjectGuid());
    // Settle every binding first (a stale one, or a follower that already
    // left its group, must not block this bind).
    for (uint64 other : CommanderBindings().Commanders())
        StandingBinding(other);
    facts.commanderAlreadyBound = CommanderBindings().FindByCommander(commanderRaw) != nullptr;

    Player* lead = FindOnlineByName(names[0]);
    facts.lead = MemberFacts(commander, lead);
    Group* commanderParty = commander->GetGroup();
    Group* leadGroup = lead ? lead->GetGroup() : nullptr;
    // A lead bot in the commander's own party is taken out of it.
    bool const leadLeavesParty = leadGroup && leadGroup == commanderParty;
    if (leadGroup && !leadLeavesParty)
    {
        facts.leadGrouped = true;
        facts.leadGroupIsRaidOrBg = leadGroup->isRaidGroup() || leadGroup->isBGGroup();
        for (Group::MemberSlot const& slot : leadGroup->GetMemberSlots())
            if (!BotManager::Instance().IsBot(slot.guid))
                facts.leadGroupHasRealPlayer = true;
        facts.leadGroupBound = CommanderBindings().FindByGroup(leadGroup->GetId()) != nullptr;
    }

    // The group to be: the lead's current bot-only group (adopted as it is)
    // plus the named bots.
    std::vector<Player*> joiners;
    std::vector<ObjectGuid> counted;
    if (lead)
        counted.push_back(lead->GetObjectGuid());
    if (facts.leadGrouped)
    {
        for (Group::MemberSlot const& slot : leadGroup->GetMemberSlots())
        {
            if (slot.guid == lead->GetObjectGuid())
                continue;
            counted.push_back(slot.guid);
            facts.members.push_back(MemberFacts(commander, ObjectAccessor::FindPlayer(slot.guid)));
        }
    }
    for (size_t i = 1; i < names.size(); ++i)
    {
        Player* bot = FindOnlineByName(names[i]);
        if (bot && std::find(counted.begin(), counted.end(), bot->GetObjectGuid()) != counted.end())
            continue;
        RemoteCommander::BindMember member = MemberFacts(commander, bot);
        if (bot)
        {
            Group* botGroup = bot->GetGroup();
            member.inOtherGroup = botGroup && botGroup != commanderParty;
            counted.push_back(bot->GetObjectGuid());
            joiners.push_back(bot);
        }
        facts.members.push_back(member);
    }

    RemoteCommander::BindRefusal refusal = RemoteCommander::CheckBind(facts);
    if (refusal != RemoteCommander::BindRefusal::None)
    {
        SendActionError(handler, intent, RemoteCommander::BindRefusalCode(refusal),
            RemoteCommander::BindRefusalText(refusal));
        return true;
    }

    // Form or adopt the party: out of the commander's party, the lead bot
    // leads, every named bot joins it.
    if (leadLeavesParty)
        LeaveCommanderParty(commander, lead);
    for (Player* bot : joiners)
        LeaveCommanderParty(commander, bot);
    if (Group* group = lead->GetGroup())
        if (!group->IsLeader(lead->GetObjectGuid()))
            group->ChangeLeader(lead->GetObjectGuid());
    for (Player* bot : joiners)
    {
        if (!JoinLeadGroup(lead, bot))
        {
            std::string message = std::string("The group invitation for ") + bot->GetName() +
                " was refused (another pending invite, a faction rule, or a full group). Bind again to adopt the group as it is now.";
            SendActionError(handler, intent, "invite-failed", message.c_str());
            return true;
        }
    }

    Group* group = lead->GetGroup();
    if (!group || !group->IsLeader(lead->GetObjectGuid()) || group->GetMembersCount() < 2)
    {
        SendActionError(handler, intent, "invite-failed", "The lead bot could not form a group.");
        return true;
    }

    RemoteCommander::Binding binding;
    binding.commanderGuid = commanderRaw;
    binding.leadGuid = lead->GetObjectGuid().GetRawValue();
    binding.groupId = group->GetId();
    RemoteCommander::BindResult result = CommanderBindings().Bind(binding);
    if (result != RemoteCommander::BindResult::Bound)
    {
        SendActionError(handler, intent, result == RemoteCommander::BindResult::GroupAlreadyBound
            ? "group-bound" : "already-bound", "The group or you are already bound.");
        return true;
    }

    // The perception feed starts off with every new binding; `.bot commander
    // view on` turns it on.
    CommanderViews()[commanderRaw] = CommanderView();

    std::vector<Player*> bots = CommandedBots(group, lead);
    FormUpOnLead(commander, lead, bots, true);
    std::string roster;
    for (Player* bot : bots)
        roster += (roster.empty() ? "" : ", ") + std::string(bot->GetName());
    handler->PSendSysMessage("You command %s's group: %s. Orders: .bot cmd <order>.", lead->GetName(), roster.c_str());
    SendActionAck(handler, intent, "party", static_cast<uint32>(bots.size()), lead->GetName());
    return true;
}

static bool HandleCommander(ChatHandler* handler, char const* args)
{
    Player* commander = Requester(handler);
    std::string text = Trim(args ? args : "");
    size_t split = text.find_first_of(" \t");
    std::string verb = text.substr(0, split);
    for (char& c : verb)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string rest = split == std::string::npos ? std::string() : Trim(text.substr(split + 1));
    std::string const intent = "commander " + (verb.empty() ? std::string("unknown") : ProtocolSafe(verb));

    if (verb != "bind" && verb != "unbind" && verb != "status" && verb != "view")
    {
        SendActionError(handler, intent, "invalid", "Usage: .bot commander bind <LeadBot> [<Bot> ...] / unbind / status / view on|off");
        return true;
    }
    if (!commander || !commander->GetSession())
    {
        SendActionError(handler, intent, "requester", "You must be in-game.");
        return true;
    }
    if (!sPlayerbotAIConfig.remoteCommander)
    {
        SendActionError(handler, intent, "disabled",
            RemoteCommander::BindRefusalText(RemoteCommander::BindRefusal::Disabled));
        return true;
    }

    if (verb == "bind")
        return HandleCommanderBind(handler, commander, rest);

    uint64 const commanderRaw = commander->GetObjectGuid().GetRawValue();
    RemoteCommander::Binding const* binding = StandingBinding(commanderRaw);
    if (!binding)
    {
        SendActionError(handler, intent, "not-bound", "You command no group. Use .bot commander bind <LeadBot> first.");
        return true;
    }
    Player* lead = FindCharacter(binding->leadGuid);
    std::string const leadName = lead ? lead->GetName() : "-";

    if (verb == "unbind")
    {
        uint32 restored = ReleaseBinding(commanderRaw, RemoteCommander::ReleaseReason::Unbound, false);
        handler->PSendSysMessage("You no longer command %s's group. The bots stay grouped; %s holds where it stands.",
            leadName.c_str(), leadName.c_str());
        SendActionAck(handler, intent, "party", restored, leadName);
        return true;
    }

    if (verb == "view")
    {
        std::string mode = rest;
        for (char& c : mode)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (mode != "on" && mode != "off")
        {
            SendActionError(handler, intent, "invalid", "Usage: .bot commander view on|off");
            return true;
        }
        CommanderView& view = CommanderViews()[commanderRaw];
        view.enabled = mode == "on";
        if (!view.enabled)
            view.pacer.Abandon();
        handler->PSendSysMessage("The view of %s's group is %s.", leadName.c_str(), view.enabled ? "on" : "off");
        std::vector<Player*> viewBots = CommandedBots(lead ? lead->GetGroup() : nullptr, lead);
        SendActionAck(handler, intent, "party", static_cast<uint32>(viewBots.size()), mode);
        return true;
    }

    // status
    std::vector<Player*> bots = CommandedBots(lead ? lead->GetGroup() : nullptr, lead);
    std::string roster;
    for (Player* bot : bots)
    {
        roster += (roster.empty() ? "" : ", ") + std::string(bot->GetName());
        if (!bot->IsAlive())
            roster += " (dead)";
    }
    handler->PSendSysMessage("You command %s's group: %s.", leadName.c_str(), roster.c_str());
    SendActionAck(handler, intent, "party", static_cast<uint32>(bots.size()), leadName);
    return true;
}

namespace {

// One standing order context: the commander, the lead bot and its group.
struct CommanderOrder
{
    Player* commander = nullptr;
    Player* lead = nullptr;
    Group* group = nullptr;
    std::vector<Player*> bots;
    // Whose attack rules judge a target: the lead bot, or while it is dead
    // the first live bot of the group on its map (a dead lead bot must not
    // stop the rest of the group from fighting).
    Player* judge = nullptr;
};

// The bound group as a BotCommandContext for the shared pull/CC executors.
// The lead bot stands in for the requester: its position is the pull anchor
// and the DPS hold spot, and its map judges the target. partyBots is the
// whole group, lead included (it may be the tank).
BotCommandContext CommanderContext(CommanderOrder const& order, Unit* target)
{
    BotCommandContext context;
    context.requester = order.lead;
    context.requesterTarget = target;
    context.enemyTarget = target;
    context.group = order.group;
    context.partyBots = order.bots;
    return context;
}

Player* FindCommandedBot(CommanderOrder const& order, std::string const& name)
{
    Player* bot = FindOnlineByName(name);
    if (!bot)
        return nullptr;
    for (Player* member : order.bots)
        if (member == bot)
            return bot;
    return nullptr;
}

} // namespace

// GUID -> a live, hostile, attackable unit on the lead bot's map and instance.
static Unit* ResolveOrderTarget(ChatHandler* handler, std::string const& intent,
    CommanderOrder const& order, std::string const& token)
{
    uint64 raw = 0;
    if (!RemoteCommander::ParseGuid(token, raw))
    {
        SendActionError(handler, intent, "invalid", "Name the target by GUID: 0x<hex> or a decimal number.");
        return nullptr;
    }
    // Map::GetUnit only finds units on the lead bot's own map and instance.
    Map* map = order.lead->GetMap();
    Unit* unit = map ? map->GetUnit(ObjectGuid(raw)) : nullptr;
    RemoteCommander::TargetFacts facts;
    facts.found = unit != nullptr;
    if (unit)
    {
        facts.inWorld = unit->IsInWorld();
        facts.groupMemberOrBot = BotManager::Instance().IsBot(unit->GetObjectGuid()) ||
            (order.group && order.group->IsMember(unit->GetObjectGuid()));
        facts.alive = unit->IsAlive();
        facts.friendly = sServerFacade.IsFriendlyTo(unit, order.judge);
        facts.attackable = order.judge->IsValidAttackTarget(unit);
        // Seen (stealth and invisibility detection, as the bots' own target
        // values check it) by a live bot of the group, and targetable.
        if (unit->IsTargetable(true, true))
            for (Player* bot : order.bots)
                if (bot->IsAlive() && bot->IsInWorld() && bot->GetMap() == map &&
                    unit->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true))
                {
                    facts.visible = true;
                    break;
                }
    }
    RemoteCommander::TargetVerdict verdict = RemoteCommander::CheckOrderTarget(facts);
    if (verdict != RemoteCommander::TargetVerdict::Ok)
    {
        SendActionError(handler, intent, RemoteCommander::TargetVerdictCode(verdict),
            RemoteCommander::TargetVerdictText(verdict));
        return nullptr;
    }
    return unit;
}

// Same per-bot steps as `.bot action attack`, with the lead bot's selection
// carrying the GUID-named target: AttackMyTargetAction reads its event
// owner's selection. The commander's own selection is never touched.
static bool CommanderAttack(ChatHandler* handler, CommanderOrder const& order, Unit* target,
    std::string const& intent)
{
    ObjectGuid const targetGuid = target->GetObjectGuid();
    uint32 succeeded = 0;
    for (Player* bot : order.bots)
    {
        if (!bot || !bot->IsInWorld() || bot->GetMap() != order.lead->GetMap())
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        RelaxTacticalMovement(ai);
        ai->ChangeStrategy("-wait for attack", BotState::BOT_STATE_ALL);
        bool accepted = true;
        if (!PlayerbotAI::IsHeal(bot))
        {
            // Set again for every bot: an earlier bot's first AI step may be
            // the lead bot's own, which can change its selection.
            order.lead->SetSelectionGuid(targetGuid);
            accepted = ExecuteQuietAction(ai, "attack my target", ai::Event("action attack", "", order.lead));
        }
        ExecuteQuietNextAction(ai, false);
        if (accepted)
            ++succeeded;
    }
    if (!succeeded)
    {
        SendActionError(handler, intent, "failed", "No bot of the group accepted the attack.");
        return true;
    }
    SendActionAck(handler, intent, "party", succeeded);
    return true;
}

// `.bot action focus skull` on a GUID: Skull goes on the target in the bound
// group, every bot attacks its Skull target.
static bool CommanderFocus(ChatHandler* handler, CommanderOrder const& order, Unit* target,
    std::string const& intent)
{
    order.group->SetTargetIcon(static_cast<uint8>(RtiTargetValue::GetRtiIndex("skull")), target->GetObjectGuid());
    uint32 succeeded = 0;
    for (Player* bot : order.bots)
    {
        if (!bot || !bot->IsInWorld())
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        RelaxTacticalMovement(ai);
        bool set = ExecuteQuietAction(ai, "rti", ai::Event("focus skull", "skull", order.lead));
        bool accepted = set && ExecuteQuietAction(ai, "attack rti target", ai::Event("focus skull", "", order.lead));
        ExecuteQuietNextAction(ai, true);
        if (accepted)
            ++succeeded;
    }
    if (!succeeded)
    {
        SendActionError(handler, intent, "failed", "No bot of the group accepted the focus order.");
        return true;
    }
    SendActionAck(handler, intent, "party", succeeded);
    return true;
}

// The lead bot walks to the point on its navmesh path (MoveTo through the
// mature stay/return-to-stay pair, inside instances too) and holds there; the
// rest of the group follows it.
static bool CommanderMove(ChatHandler* handler, CommanderOrder const& order, std::string const& args,
    std::string const& intent)
{
    RemoteCommander::MovePoint point;
    if (!RemoteCommander::ParseMovePoint(args, point))
    {
        SendActionError(handler, intent, "invalid", "Usage: .bot cmd move <x> <y> <z> (map coordinates on the lead bot's map)");
        return true;
    }
    Player* lead = order.lead;
    if (!lead->IsAlive() || lead->IsTaxiFlying() || lead->IsBeingTeleported())
    {
        SendActionError(handler, intent, "lead-unavailable", "The lead bot is dead, on a taxi or teleporting.");
        return true;
    }
    PlayerbotAI* leadAi = PlayerbotAIStorage::Instance().GetAI(lead);
    if (!leadAi)
    {
        SendActionError(handler, intent, "no-ai", "The lead bot has no AI.");
        return true;
    }

    float const dx = point.x - lead->GetPositionX();
    float const dy = point.y - lead->GetPositionY();
    float const dz = point.z - lead->GetPositionZ();
    RemoteCommander::MoveFacts facts;
    facts.distanceFromLead = std::sqrt(dx * dx + dy * dy + dz * dz);
    facts.maxDistance = sPlayerbotAIConfig.reactDistance;
    PathFinder path(lead);
    path.calculate(point.x, point.y, point.z, false);
    facts.pathType = static_cast<uint32>(path.getPathType());
    Vector3 const end = path.getActualEndPosition();
    float const ex = end.x - point.x;
    float const ey = end.y - point.y;
    float const ez = end.z - point.z;
    facts.endOffset = std::sqrt(ex * ex + ey * ey + ez * ez);
    RemoteCommander::MoveVerdict verdict = RemoteCommander::CheckMove(facts);
    if (verdict != RemoteCommander::MoveVerdict::Ok)
    {
        std::string message;
        if (verdict == RemoteCommander::MoveVerdict::TooFar)
            message = "The point is more than " + std::to_string(static_cast<uint32>(facts.maxDistance)) +
                " yd (AiPlayerbot.ReactDistance) from the lead bot. Move in shorter legs.";
        else if (verdict == RemoteCommander::MoveVerdict::NoPath)
            message = "The lead bot has no walkable path to that point.";
        else
            message = "The point is not on walkable ground (the path ends more than 5 yd from it).";
        SendActionError(handler, intent, RemoteCommander::MoveVerdictCode(verdict), message.c_str());
        return true;
    }

    // Same transition as `.bot action come`, with the point as the stay spot.
    leadAi->DropQueuedMovementOrders();
    PlayerConvenience::Instance().NoteHoldOrder(lead->GetObjectGuid());
    leadAi->Reset();
    leadAi->SetMovementStrategy("stay");
    ai::PositionMap& posMap = leadAi->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
    ai::PositionEntry stayPos = posMap["stay"];
    stayPos.Set(point.x, point.y, point.z, lead->GetMapId());
    posMap["stay"] = stayPos;
    ExecuteQuietAction(leadAi, "return to stay position",
        ai::Event("return to stay position", "stay", order.commander));

    // The group follows the lead bot.
    std::vector<Player*> followers;
    for (Player* bot : order.bots)
        if (bot != lead)
            followers.push_back(bot);
    uint32 following = FormUpOnLead(order.commander, lead, followers, false);
    SendActionAck(handler, intent, "party", following + 1, lead->GetName());
    return true;
}

static bool CommanderStayFollowRest(ChatHandler* handler, CommanderOrder const& order,
    std::string const& verb, std::string const& intent)
{
    uint32 succeeded = 0;
    if (verb == "follow")
        succeeded = FormUpOnLead(order.commander, order.lead, order.bots, false);
    else
    {
        for (Player* bot : order.bots)
        {
            if (!bot || !bot->IsInWorld())
                continue;
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai)
                continue;
            bool accepted = false;
            if (verb == "stay")
            {
                ai->DropQueuedMovementOrders();
                accepted = ExecuteQuietAction(ai, "stay chat shortcut", ai::Event("stay", "", order.commander));
            }
            else // rest: same as `.bot action rest`
            {
                bool rested = false;
                if (bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA))
                    rested = ExecuteQuietAction(ai, "drink", ai::Event("rest", "drink", order.commander)) || rested;
                if (bot->GetHealth() < bot->GetMaxHealth())
                    rested = ExecuteQuietAction(ai, "food", ai::Event("rest", "food", order.commander)) || rested;
                accepted = rested || (bot->GetHealth() >= bot->GetMaxHealth() &&
                    (bot->GetPowerType() != POWER_MANA || bot->GetPower(POWER_MANA) >= bot->GetMaxPower(POWER_MANA)));
            }
            if (accepted)
                ++succeeded;
        }
    }
    if (!succeeded)
    {
        SendActionError(handler, intent, "failed", "No bot of the group accepted the order.");
        return true;
    }
    SendActionAck(handler, intent, "party", succeeded, verb == "rest" ? "" : order.lead->GetName());
    return true;
}

// revive: speeds up the dead bot's own way back (release now, run to the
// corpse); the dead strategy then revives it at the corpse. A healer in the
// group still resurrects a body that has not released on its own.
static bool CommanderRevive(ChatHandler* handler, CommanderOrder const& order, Player* bot,
    std::string const& intent)
{
    if (bot->IsAlive())
    {
        SendActionError(handler, intent, "not-dead", "That bot is alive.");
        return true;
    }
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        SendActionError(handler, intent, "no-ai", "That bot has no AI.");
        return true;
    }
    bool released = ExecuteQuietAction(ai, "release", ai::Event("revive", "release", order.commander));
    bool running = ExecuteQuietAction(ai, "corpse run", ai::Event("revive", "corpse run", order.commander));
    if (!released && !running)
    {
        SendActionError(handler, intent, "failed", "The bot could neither release nor run back to its corpse.");
        return true;
    }
    SendActionAck(handler, intent, "bot:" + std::string(bot->GetName()), 1, released ? "release" : "corpse run");
    return true;
}

// summon: the `.bot summon` flow (3 s, portal, follow restored on arrival),
// with the lead bot as the summon point instead of the commander.
static bool CommanderSummon(ChatHandler* handler, CommanderOrder const& order, Player* bot,
    std::string const& intent)
{
    Player* lead = order.lead;
    if (bot == lead)
    {
        SendActionError(handler, intent, "invalid", "The lead bot is the summon point; summon another bot to it.");
        return true;
    }
    if (!lead->IsAlive() || lead->IsTaxiFlying() || lead->IsBeingTeleported())
    {
        SendActionError(handler, intent, "lead-unavailable", "The lead bot is dead, on a taxi or teleporting.");
        return true;
    }
    if (!bot->IsAlive() || bot->IsInCombat() || bot->IsTaxiFlying() || bot->IsBeingTeleported())
    {
        SendActionError(handler, intent, "unavailable", "That bot is dead, in combat, on a taxi or teleporting.");
        return true;
    }
    if (PlayerConvenience::Instance().IsBusy(bot->GetObjectGuid()))
    {
        SendActionError(handler, intent, "busy", "That bot already has a summon pending.");
        return true;
    }
    // As `.bot summon`: the summoner becomes the bot's master if it is not.
    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record || (record->masterGuid != order.commander->GetObjectGuid() &&
        !BotManager::Instance().BindBotMaster(bot->GetObjectGuid(), order.commander->GetObjectGuid())))
    {
        SendActionError(handler, intent, "failed", "The bot could not be assigned to you for the summon.");
        return true;
    }
    if (!PlayerConvenience::Instance().RequestSummon(order.commander, bot, lead))
    {
        SendActionError(handler, intent, "failed", "The summon was refused.");
        return true;
    }
    SendActionAck(handler, intent, "bot:" + std::string(bot->GetName()), 1, lead->GetName());
    return true;
}

static bool HandleCommanderOrder(ChatHandler* handler, char const* args)
{
    std::string text = Trim(args ? args : "");
    size_t split = text.find_first_of(" \t");
    std::string verb = text.substr(0, split);
    for (char& c : verb)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string rest = split == std::string::npos ? std::string() : Trim(text.substr(split + 1));
    std::string const intent = "cmd " + (verb.empty() ? std::string("unknown") : ProtocolSafe(verb));

    if (!RemoteCommander::IsKnownOrder(verb))
    {
        SendActionError(handler, intent, "invalid",
            "Usage: .bot cmd attack|pull|pullback <guid> [seconds] / cc <mark> <guid> [bot] / focus <guid> / move <x> <y> <z> / stay / follow / rest / revive <bot> / summon <bot>");
        return true;
    }

    CommanderOrder order;
    order.commander = Requester(handler);
    if (!order.commander || !order.commander->GetSession())
    {
        SendActionError(handler, intent, "requester", "You must be in-game.");
        return true;
    }
    if (!sPlayerbotAIConfig.remoteCommander)
    {
        SendActionError(handler, intent, "disabled",
            RemoteCommander::BindRefusalText(RemoteCommander::BindRefusal::Disabled));
        return true;
    }
    RemoteCommander::Binding const* binding = StandingBinding(order.commander->GetObjectGuid().GetRawValue());
    if (!binding)
    {
        SendActionError(handler, intent, "not-bound", "You command no group. Use .bot commander bind <LeadBot> first.");
        return true;
    }
    order.lead = FindCharacter(binding->leadGuid);
    order.group = order.lead ? order.lead->GetGroup() : nullptr;
    if (!order.lead || !order.group || !order.lead->IsInWorld())
    {
        SendActionError(handler, intent, "lead-unavailable", "The lead bot is between maps. Try again in a moment.");
        return true;
    }
    order.bots = CommandedBots(order.group, order.lead);
    order.judge = order.lead;
    if (!order.lead->IsAlive())
        for (Player* bot : order.bots)
            if (bot->IsAlive() && bot->IsInWorld() && bot->GetMap() == order.lead->GetMap())
            {
                order.judge = bot;
                break;
            }

    std::vector<std::string> parts = RemoteCommander::SplitOrderArgs(rest);
    RemoteCommander::OrderTier const tier = RemoteCommander::TierForOrder(verb);

    // Owner-or-GM orders name one bot of the group; the commander must
    // control it now (the bound commander alone is not enough).
    if (tier == RemoteCommander::OrderTier::OwnerOrGm)
    {
        Player* bot = parts.size() == 1 ? FindCommandedBot(order, parts[0]) : nullptr;
        if (!bot)
        {
            std::string message = "Usage: .bot cmd " + verb + " <bot of your commanded group>";
            SendActionError(handler, intent, "no-bot", message.c_str());
            return true;
        }
        BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
        if (!RemoteCommander::MayIssueOrder(sPlayerbotAIConfig.remoteCommander, true, tier,
            record && CanControlBot(order.commander, record)))
        {
            SendActionError(handler, intent, "denied", "Only this bot's owner (or a GameMaster) may do that.");
            return true;
        }
        return verb == "revive"
            ? CommanderRevive(handler, order, bot, intent)
            : CommanderSummon(handler, order, bot, intent);
    }

    // Tactical orders: the bound commander counts as a member of its group.
    if (!RemoteCommander::MayIssueOrder(sPlayerbotAIConfig.remoteCommander, true, tier, false))
    {
        SendActionError(handler, intent, "denied", "Order refused.");
        return true;
    }

    if (verb == "move")
        return CommanderMove(handler, order, rest, intent);
    if (verb == "stay" || verb == "follow" || verb == "rest")
    {
        if (!parts.empty())
        {
            std::string message = "Usage: .bot cmd " + verb;
            SendActionError(handler, intent, "invalid", message.c_str());
            return true;
        }
        return CommanderStayFollowRest(handler, order, verb, intent);
    }

    if (verb == "attack" || verb == "focus" || verb == "pull" || verb == "pullback")
    {
        bool const pull = verb == "pull" || verb == "pullback";
        std::string option;
        if (parts.empty() || parts.size() > (pull ? 2u : 1u))
        {
            std::string message = "Usage: .bot cmd " + verb + " <guid>" + (pull ? " [seconds]" : "");
            SendActionError(handler, intent, "invalid", message.c_str());
            return true;
        }
        if (parts.size() == 2)
        {
            char* end = nullptr;
            long seconds = std::strtol(parts[1].c_str(), &end, 10);
            if (!end || *end != '\0' || seconds < 0 || seconds > 60)
            {
                SendActionError(handler, intent, "invalid", "The join delay is 0-60 seconds.");
                return true;
            }
            option = std::to_string(seconds);
        }
        Unit* target = ResolveOrderTarget(handler, intent, order, parts[0]);
        if (!target)
            return true;
        if (verb == "attack")
            return CommanderAttack(handler, order, target, intent);
        if (verb == "focus")
            return CommanderFocus(handler, order, target, intent);
        BotCommandContext context = CommanderContext(order, target);
        return RunPullOrder(handler, context, order.lead, verb, option, intent);
    }

    // cc <mark> <guid> [bot]
    if (parts.size() < 2 || parts.size() > 3)
    {
        SendActionError(handler, intent, "invalid", "Usage: .bot cmd cc <mark> <guid> [bot]");
        return true;
    }
    std::string mark = parts[0];
    for (char& c : mark)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!IsRaidTargetMark(mark))
    {
        SendActionError(handler, intent, "invalid", "Unknown raid mark (star, circle, diamond, triangle, moon, square, cross, skull).");
        return true;
    }
    std::string ccIntent = "cc " + mark;
    if (parts.size() == 3)
    {
        Player* named = FindCommandedBot(order, parts[2]);
        if (!named)
        {
            SendActionError(handler, intent, "no-bot", "That bot is not in your commanded group.");
            return true;
        }
        ccIntent += " " + std::string(named->GetName());
    }
    Unit* target = ResolveOrderTarget(handler, intent, order, parts[1]);
    if (!target)
        return true;
    BotCommandContext context = CommanderContext(order, target);
    return RunCcOrder(handler, context, order.judge, ccIntent, intent);
}

void UpdateRemoteCommanders(uint32_t diff)
{
    // Nothing runs while no commander is bound.
    if (CommanderBindings().Empty())
    {
        s_commanderSweepMs = 0;
        if (!CommanderViews().empty())
            CommanderViews().clear();
        return;
    }
    s_commanderSweepMs += diff;
    if (s_commanderSweepMs >= RemoteCommander::kSweepIntervalMs)
    {
        s_commanderSweepMs = 0;
        for (uint64 commanderRaw : CommanderBindings().Commanders())
            StandingBinding(commanderRaw);
    }
    // Issue #884, phase 2: the perception feed of the bindings that stand.
    UpdateCommanderViews(diff);
}

bool IsCommandedBy(Player* commander, Player* bot)
{
    if (!commander || !bot)
        return false;
    RemoteCommander::Binding const* binding =
        CommanderBindings().FindByCommander(commander->GetObjectGuid().GetRawValue());
    if (!binding)
        return false;
    Player* lead = FindCharacter(binding->leadGuid);
    Group* group = lead ? lead->GetGroup() : nullptr;
    return group && group->GetId() == binding->groupId && group->IsMember(bot->GetObjectGuid());
}

void ClearRemoteCommanders()
{
    CommanderBindings().Clear();
    CommanderViews().clear();
    SavedFollowTargets().clear();
    s_commanderSweepMs = 0;
}

static bool HandleAhBot(ChatHandler* handler, char const* args)
{
    if (!handler)
        return false;

    Player* requester = Requester(handler);
    if (requester && !IsBotAdministrator(requester))
    {
        handler->PSendSysMessage("You do not have permission to manage the AH bot.");
        return true;
    }

    std::string argStr = args ? args : "";
    argStr = Trim(argStr);
    if (argStr.empty() || argStr == "help")
    {
        handler->PSendSysMessage("AHBot commands:");
        handler->PSendSysMessage("  .bot ah status - Display synthetic AH engine status and telemetry");
        handler->PSendSysMessage("  .bot ah reload - Reload price overrides and bans from ahbot_items");
        handler->PSendSysMessage("  .bot ah rebuild [all] - Restart market pass (all = expire active unbid synthetic items)");
        handler->PSendSysMessage("  .bot ah item <id> - View override for item");
        handler->PSendSysMessage("  .bot ah item <id> reset - Remove override for item");
        handler->PSendSysMessage("  .bot ah item <id> <value> [chance] [min] [max] - Set override (0 0 = blacklist)");
        return true;
    }

    std::stringstream ss(argStr);
    std::string subCmd;
    ss >> subCmd;
    for (char& c : subCmd) c = tolower(c);

    if (subCmd == "status")
    {
        std::string status = AhMarketService::Instance().GetStatus();
        handler->PSendSysMessage("%s", status.c_str());
        return true;
    }

    if (subCmd == "reload")
    {
        AhMarketService::Instance().ReloadOverrides();
        handler->PSendSysMessage("AHBot overrides reloaded from database.");
        return true;
    }

    if (subCmd == "rebuild")
    {
        std::string mode;
        ss >> mode;
        for (char& c : mode) c = tolower(c);
        bool all = (mode == "all");
        AhMarketService::Instance().RebuildMarket(all);
        if (all)
            handler->PSendSysMessage("AHBot market rebuild scheduled (expiring active unbid synthetic listings).");
        else
            handler->PSendSysMessage("AHBot market rebuild scheduled.");
        return true;
    }

    if (subCmd == "item")
    {
        uint32_t itemId = 0;
        if (!(ss >> itemId) || itemId == 0)
        {
            handler->PSendSysMessage("Usage: .bot ah item <id> [value [chance [min [max]]]] / reset");
            return true;
        }

        std::string nextToken;
        if (!(ss >> nextToken))
        {
            if (AhMarketService::Instance().IsItemBlacklisted(itemId))
            {
                handler->PSendSysMessage("AHBot item %u is blacklisted/banned.", itemId);
            }
            else
            {
                handler->PSendSysMessage("AHBot item %u has no blacklist or has an active price override.", itemId);
            }
            return true;
        }

        for (char& c : nextToken) c = tolower(c);
        if (nextToken == "reset")
        {
            AhMarketService::Instance().ResetItemOverride(itemId);
            handler->PSendSysMessage("AHBot item %u override reset.", itemId);
            return true;
        }

        uint32_t value = 0;
        try {
            value = std::stoul(nextToken);
        } catch (...) {
            handler->PSendSysMessage("Invalid value '%s' for item override.", nextToken.c_str());
            return true;
        }

        uint32_t chance = 100;
        uint32_t minAmount = 1;
        uint32_t maxAmount = 1;

        if (ss >> chance)
        {
            if (ss >> minAmount)
            {
                if (!(ss >> maxAmount))
                    maxAmount = minAmount;
            }
        }

        AhMarketService::Instance().SetItemOverride(itemId, value, chance, minAmount, maxAmount);
        if (value == 0 && chance == 0)
        {
            handler->PSendSysMessage("AHBot item %u blacklisted (value=0, chance=0).", itemId);
        }
        else
        {
            handler->PSendSysMessage("AHBot item %u override set: value=%u copper, chance=%u%%, amount=%u..%u.",
                itemId, value, chance, minAmount, maxAmount);
        }
        return true;
    }

    handler->PSendSysMessage("Unknown AHBot command '%s'. Try .bot ah help", subCmd.c_str());
    return true;
}


// ---- Managed random-bot pool (issue #265) ---------------------------------
// The module's AllCommandScript intercepts `.bot` before the core command
// table can protect a subcommand, so every permission check below is local.
// Adoption and account listing are server-console-only: no player, addon
// message, hired bot or headless session can reach them.
static bool IsServerConsole(ChatHandler* handler)
{
    if (!handler || handler->GetSession())
        return false;
#ifdef MANGOSSERVER_CHAT_H
    // Remote-access and Discord commands also use null-session CliHandler
    // instances, carrying the calling account's security level. ChatHandler
    // keeps GetAccessLevel() protected, so the concrete handler type is the only
    // way to read it: only the stdin console (and operator-written
    // `pending_commands` rows, which the core also queues as SEC_CONSOLE) carries
    // SEC_CONSOLE, so a remote user of any account rank is rejected here.
    CliHandler* cli = dynamic_cast<CliHandler*>(handler);
    return cli && cli->GetAccessLevel() == SEC_CONSOLE;
#else
    return true; // lens stub: the real build always takes the branch above
#endif
}

static bool RequirePoolAdmin(ChatHandler* handler)
{
    if (IsServerConsole(handler))
        return true;
    WorldSession* session = handler ? handler->GetSession() : nullptr;
    if (!session || !session->GetPlayer())
    {
        handler->PSendSysMessage("You must be in-game or at the server console.");
        return false;
    }
    if (session->GetSecurity() < SEC_GAMEMASTER)
    {
        handler->PSendSysMessage("Your account may not inspect the bot pool.");
        return false;
    }
    return true;
}

static bool RequireServerConsole(ChatHandler* handler)
{
    if (IsServerConsole(handler))
        return true;
    handler->PSendSysMessage("This command is only available at the server console.");
    return false;
}

static bool HandlePoolStatus(ChatHandler* handler)
{
    if (!RequirePoolAdmin(handler))
        return true;

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();
    RandomBotPoolReset& reset = RandomBotPoolReset::Instance();

    // Parsed from the module's startup configuration (aiplayerbot.conf is read
    // once at startup, so `.reload config` cannot change what a reset does).
    // This reports the configured mode even when the service never planned one.
    PoolResetSetting setting = ParsePoolResetSetting(sPlayerbotAIConfig.randomBotPoolReset);
    std::string mode = PoolResetModeName(setting.mode);
    if (setting.mode == PoolResetMode::Once)
        mode += ":" + setting.token;
    if (setting.mode == PoolResetMode::Invalid)
        mode += " (" + setting.error + ")";

    // Live count of the characters on managed pool accounts. A failed count is
    // reported as unavailable rather than as zero.
    uint32 managedCharacters = 0;
    std::string countError;
    bool haveCharacterCount = registry.CountManagedCharacters(managedCharacters, countError);

    handler->PSendSysMessage("TortoiseBots pool status:");
    handler->PSendSysMessage("  reset setting      : %s", mode.c_str());
    if (setting.mode == PoolResetMode::Once)
        handler->PSendSysMessage("  requested generation: %s", setting.token.c_str());
    handler->PSendSysMessage("  applied generation : %s",
        reset.AppliedGeneration().empty() ? "(none)" : reset.AppliedGeneration().c_str());
    handler->PSendSysMessage("  managed accounts   : %u%s",
        static_cast<uint32>(registry.AccountCount()),
        registry.IsValidated() ? "" : " (registry NOT validated)");
    if (haveCharacterCount)
        handler->PSendSysMessage("  managed characters : %u", managedCharacters);
    else
        handler->PSendSysMessage("  managed characters : (count unavailable: %s)", countError.c_str());
    handler->PSendSysMessage("  reset phase        : %s", reset.PhaseName());
    if (reset.TargetCount() && reset.Phase() == PoolResetPhase::SettlingAuctions)
        handler->PSendSysMessage("  reset progress     : %u/%u characters' auctions settled",
            reset.SettledAuctionOwnerCount(), reset.TargetCount());
    else if (reset.TargetCount())
        handler->PSendSysMessage("  reset progress     : %u/%u characters deleted",
            reset.ProcessedCount(), reset.TargetCount());
    // The service is the single authority for availability: it combines the
    // registry state with the reset state.
    handler->PSendSysMessage("  pool available     : %s",
        RandomBotService::Instance().IsPoolAvailable() ? "yes" : "no");
    if (!reset.LastFailure().empty())
        handler->PSendSysMessage("  last failure       : %s", reset.LastFailure().c_str());
    return true;
}

static bool HandlePoolAccounts(ChatHandler* handler)
{
    if (!RequireServerConsole(handler))
        return true;

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();
    if (!registry.IsValidated())
    {
        handler->PSendSysMessage("The managed-account registry could not be read: %s",
            registry.LastError().empty() ? "unknown error" : registry.LastError().c_str());
        return true;
    }

    uint32 total = 0;
    std::string countError;
    bool haveTotal = registry.CountManagedCharacters(total, countError);
    handler->PSendSysMessage("Managed pool accounts: %u, characters: %s",
        static_cast<uint32>(registry.AccountCount()),
        haveTotal ? std::to_string(total).c_str() : "(count unavailable)");
    for (PoolAccount const& account : registry.Accounts())
    {
        uint32 characters = 0;
        std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM `characters` WHERE `deleteDate` IS NULL AND `account` = '%u'", account.accountId));
        if (count)
            characters = count->Fetch()[0].GetUInt32();
        handler->PSendSysMessage("  #%u %s [%s] - %u character(s)%s",
            account.accountId, account.username.c_str(), RegistrationSourceName(account.source), characters,
            count ? "" : " (character count unavailable)");
    }
    if (!registry.AccountCount())
        handler->PSendSysMessage("  (none; the pool is empty)");
    return true;
}

static bool HandlePoolAdopt(ChatHandler* handler, char const* args)
{
    if (!RequireServerConsole(handler))
        return true;

    std::string sub = Trim(args ? args : "");
    std::string subLower = sub;
    for (char& c : subLower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();

    if (subLower == "preview")
    {
        RandomBotAccountRegistry::AdoptionPreview preview = registry.PreviewLegacyAdoption();
        if (!preview.ok)
        {
            handler->PSendSysMessage("Adoption preview failed: %s", preview.error.c_str());
            return true;
        }

        handler->PSendSysMessage("Accounts matching prefix '%s': %u", preview.prefix.c_str(),
            static_cast<uint32>(preview.accounts.size()));
        for (RandomBotAccountRegistry::LegacyAccount const& account : preview.accounts)
            handler->PSendSysMessage("  #%u %s - %u character(s) %s",
                account.accountId, account.username.c_str(), account.characterCount,
                account.alreadyRegistered ? "[already managed]" : "[NOT managed]");

        if (preview.accounts.empty())
        {
            handler->PSendSysMessage("Nothing to adopt.");
            return true;
        }

        handler->PSendSysMessage("Total: %u account(s), %u character(s); %u account(s) still need adoption.",
            static_cast<uint32>(preview.accounts.size()), preview.totalCharacters, preview.pendingAccounts);
        handler->PSendSysMessage("Adoption registers accounts only; no character is changed or deleted.");
        if (preview.pendingAccounts)
            handler->PSendSysMessage("To enroll them, run exactly: bot pool adopt confirm %s (valid for 5 minutes)",
                preview.challenge.c_str());
        else
            handler->PSendSysMessage("Every matching account is already managed.");
        return true;
    }

    if (subLower.rfind("confirm", 0) == 0)
    {
        std::string challenge = Trim(subLower.substr(7));
        if (challenge.empty())
        {
            handler->PSendSysMessage("Usage: bot pool adopt confirm <challenge> (from 'bot pool adopt preview')");
            return true;
        }

        RandomBotAccountRegistry::AdoptionConfirm confirm = registry.ConfirmLegacyAdoption(challenge, time(nullptr));
        if (!confirm.ok)
        {
            handler->PSendSysMessage("Adoption aborted: %s", confirm.error.c_str());
            return true;
        }
        handler->PSendSysMessage("Adoption complete: %u account(s) registered, %u already managed. No character was changed.",
            confirm.adopted, confirm.alreadyRegistered);
        return true;
    }

    handler->PSendSysMessage("Usage: bot pool adopt preview / bot pool adopt confirm <challenge>");
    return true;
}

static bool HandlePool(ChatHandler* handler, char const* args)
{
    std::string sub = Trim(args ? args : "");
    std::string subLower = sub;
    for (char& c : subLower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (subLower == "status")
        return HandlePoolStatus(handler);
    if (subLower == "accounts")
        return HandlePoolAccounts(handler);
    if (subLower.rfind("adopt", 0) == 0)
        return HandlePoolAdopt(handler, subLower.substr(5).c_str());

    handler->PSendSysMessage("Usage: bot pool status / accounts / adopt preview / adopt confirm <challenge>");
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
bool HandleChatCommand(ChatHandler* handler, char const* args)
{
    if (!handler || !args)
        return false;

    // Trim leading spaces
    while (*args == ' ' || *args == '\t') ++args;
    if (!*args)
    {
        handler->PSendSysMessage("Usage: .bot add/remove/logout/roster/action/commander/cmd/follow/invite/uninvite/kick/stay/guard/free/ready/attack/interrupt/formation/list/stats/status/lease/version/about/pullback/role/summon/command/hire/loot/repair/sell/rest/drink/eat/release/corpse run/learn/trade/inv/item/behavior/strategy/maintenance/autogear/ah/pool");
        return true;
    }

    // Extract subcommand
    std::string cmd;
    const char* p = args;
    while (*p && *p != ' ' && *p != '\t')
    {
        cmd += *p;
        ++p;
    }
    while (*p == ' ' || *p == '\t') ++p;
    const char* subArgs = p;

    // Normalize cmd to lowercase
    for (char& c : cmd) c = tolower(c);

    if (cmd == "ah" || cmd == "ahbot")
        return HandleAhBot(handler, subArgs);
    if (cmd == "action")
        return HandleAction(handler, subArgs);
    if (cmd == "commander")
        return HandleCommander(handler, subArgs);
    if (cmd == "cmd")
        return HandleCommanderOrder(handler, subArgs);
    if (cmd == "add")
        return HandleAdd(handler, subArgs);
    if (cmd == "remove")
        return HandleRemove(handler, subArgs);
    if (cmd == "logout")
        return HandleLogout(handler, subArgs);
    if (cmd == "roster")
        return HandleRoster(handler);
    if (cmd == "follow")
        return HandleFollow(handler, subArgs);
    if (cmd == "invite")
        return HandleInvite(handler, subArgs);
    if (cmd == "uninvite" || cmd == "kick")
        return HandleUninvite(handler, subArgs);
    if (cmd == "stay")
        return HandleStay(handler, subArgs);
    if (cmd == "guard")
        return HandleGuard(handler, subArgs);
    if (cmd == "free")
        return HandleFree(handler, subArgs);
    if (cmd == "ready")
        return HandleReady(handler, subArgs);
    if (cmd == "attack")
        return HandleAttack(handler, subArgs);
    if (cmd == "interrupt")
        return HandleInterrupt(handler, subArgs);
    if (cmd == "formation")
        return HandleFormation(handler, subArgs);
    if (cmd == "list")
        return HandleList(handler);
    if (cmd == "stats")
        return HandleStats(handler);
    if (cmd == "status")
        return HandleStatus(handler, subArgs);
    if (cmd == "lease")
        return HandleLease(handler, subArgs);
    if (cmd == "version" || cmd == "v" || cmd == "about" || cmd == "credits")
        return HandleVersion(handler);
    if (cmd == "pullback" || cmd == "pull-back")
        return HandlePullback(handler, subArgs);
    if (cmd == "role")
        return HandleRole(handler, subArgs);
    if (cmd == "summon")
        return HandleSummon(handler, subArgs);
    if (cmd == "maintenance")
        return HandleMaintenance(handler, subArgs);
    if (cmd == "autogear")
        return HandleAutogear(handler, subArgs);
    if (cmd == "hire")
        return HandleHire(handler, subArgs);
    if (cmd == "pool")
        return HandlePool(handler, subArgs);
    if (cmd == "loot")
        return HandleLoot(handler, subArgs);
    if (cmd == "repair")
        return HandleRepair(handler, subArgs);
    if (cmd == "sell")
        return HandleSell(handler, subArgs);
    if (cmd == "rest" || cmd == "drink" || cmd == "eat")
        return HandleRest(handler, subArgs);
    if (cmd == "release")
        return HandleRelease(handler, subArgs);
    if (cmd == "corpse")
    {
        std::string rest = Trim(subArgs ? subArgs : "");
        for (char& c : rest)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (rest == "run" || rest.empty())
            return HandleCorpseRun(handler, "");
        handler->PSendSysMessage("Usage: .bot corpse run");
        return true;
    }
    if (cmd == "corpserun")
        return HandleCorpseRun(handler, subArgs);
    if (cmd == "learn")
        return HandleLearn(handler, subArgs);
    if (cmd == "trade")
        return HandleTrade(handler, subArgs);
    if (cmd == "command")
        return HandleMatureCommand(handler, subArgs);
    if (cmd == "inv" || cmd == "inventory")
        return HandleInventory(handler, subArgs);
    if (cmd == "item")
        return HandleItem(handler, subArgs);
    if (cmd == "behavior" || cmd == "behaviour")
        return HandleBehavior(handler, subArgs);
    if (cmd == "strategy")
        return HandleStrategy(handler, subArgs);
    if (cmd == "help" || cmd == "h")
    {
        handler->PSendSysMessage("TBM:VERSION|%s", BuildVersion().c_str());
        return true;
    }

    handler->PSendSysMessage("Unknown bot command '%s'. Try .bot help", cmd.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access
bool TryHandleBotCommand(ChatHandler* handler, char const* text)
{
    if (!handler || !text)
        return false;
    // Check for ".ahbot" alias
    if (strncmp(text, "ahbot", 5) == 0 && (text[5] == '\0' || text[5] == ' ' || text[5] == '\t'))
    {
        const char* args = text + 5;
        while (*args == ' ' || *args == '\t') ++args;
        return HandleAhBot(handler, args);
    }
    // text is the full command after '.' — e.g., "bot add Dudette"
    // We only handle "bot ..." here; return false to let normal handler continue.
    if (strncmp(text, "bot", 3) != 0)
        return false;
    // Ensure "bot" is followed by space or end
    if (text[3] != '\0' && text[3] != ' ' && text[3] != '\t')
        return false;
    const char* args = text + 3;
    while (*args == ' ' || *args == '\t') ++args;
    return HandleChatCommand(handler, args);
}

} // namespace BotCommands
} // namespace TortoiseBots
