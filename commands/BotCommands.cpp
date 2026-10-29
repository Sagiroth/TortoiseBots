// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommands.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
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
#include "../ai/playerbot/RandomItemMgr.h"
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
#include "../runtime/PlayerAutogearPolicy.h"
#include "../runtime/GearItemLevelCapPolicy.h"
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

// ── TBM bot panel: inventory snapshot, item orders, behaviour toggles ──────
// The panel addresses items by the server's own bag/slot pair, so an order
// always lands on the exact item the player clicked - never on "the first
// item with this name" like the whisper grammar.

bool HandleAutogear(ChatHandler* handler, char const* args)
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

    // Autogear hands out no ammo: say so when the ranged weapon it left
    // equipped has none in the bags (auto shot cannot fire), like the tank's
    // "no shield equipped" warning.
    Item const* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    ItemPrototype const* rangedProto = ranged ? ranged->GetProto() : nullptr;
    if (rangedProto && rangedProto->Class == ITEM_CLASS_WEAPON && !factory.HasAmmoInBagsFor(rangedProto->SubClass))
    {
        bool const gun = rangedProto->SubClass == ITEM_SUBCLASS_WEAPON_GUN;
        handler->PSendSysMessage("Warning: %s has %s equipped but no %s in the bags - auto shot will not fire until %s gets some (owned-bot maintenance or hand them over).",
            name.c_str(), rangedProto->Name1, gun ? "bullets" : "arrows", name.c_str());
    }
    return true;
}

// `.autogear` — gear a real player the way a spawned pool bot gets geared:
// same candidate pool, score, required-level rule and spawn ceiling
// (GearItemLevelCap with RandomGearMaxItemLevelAboveLevel). Upgrades only;
// replaced pieces go to the bags or the target's own mailbox. A top-level
// command like `.ahbot`, not a `.bot` subcommand: owned bots keep
// `.bot autogear` above, and this handler refuses bots.
static bool IsChallengeModeCharacter(Player* player)
{
    // Every Turtle challenge mode except CHALLENGE_WAR_MODE, which is the
    // open-world PvP toggle rather than a gear restriction.
    return player->HasChallenge(CHALLENGE_SLOW_AND_STEADY) || player->HasChallenge(CHALLENGE_EXHAUSTION_MODE) ||
        player->HasChallenge(CHALLENGE_HARDCORE) || player->HasChallenge(CHALLENGE_VAGRANT_MODE) ||
        player->HasChallenge(CHALLENGE_BOARING_MODE) || player->HasChallenge(CHALLENGE_CRAFTMASTER) ||
        player->HasChallenge(CHALLENGE_LUNATIC) || player->HasChallenge(CHALLENGE_BREWMASTER) ||
        player->HasChallenge(CHALLENGE_HEROIC);
}

static bool HandlePlayerAutogear(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    std::string text = Trim(args ? args : "");
    if (text.empty())
    {
        handler->PSendSysMessage("Usage: .autogear [player name] <spec> [ilvl]");
        return true;
    }

    std::vector<std::string> tokens;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token)
        tokens.push_back(token);

    // Trailing digits are the optional ilvl argument; everything before is
    // [player name] <spec words...>.
    uint32 requestedIlvl = 0;
    if (!tokens.empty())
    {
        std::string const& last = tokens.back();
        bool digitsOnly = !last.empty() && last.size() <= 5;
        for (char c : last)
            if (c < '0' || c > '9')
                digitsOnly = false;
        if (digitsOnly)
        {
            requestedIlvl = static_cast<uint32>(stoul(last));
            tokens.pop_back();
        }
    }
    if (tokens.empty())
    {
        handler->PSendSysMessage("Usage: .autogear [player name] <spec> [ilvl]");
        return true;
    }

    // A leading spec word means no name was given; otherwise the first token
    // is the player name and the rest are the spec words ("enhancement tank").
    // A single token is always a spec word — with no spec following it, a
    // name cannot parse, and the unknown-spec reply lists the valid specs.
    std::string nameToken;
    size_t specStart = 0;
    if (tokens.size() >= 2 && !TortoiseBots::IsAutogearSpecToken(tokens[0]))
    {
        nameToken = tokens[0];
        specStart = 1;
    }
    std::string specToken = tokens[specStart];
    for (size_t i = specStart + 1; i < tokens.size(); ++i)
        specToken += " " + tokens[i];

    Player* target = nullptr;
    if (!nameToken.empty())
    {
        std::string name = nameToken;
        if (normalizePlayerName(name))
            target = sObjectMgr.GetPlayer(name.c_str());
        if (!target)
            target = sObjectAccessor.FindPlayerByName(name.c_str());
        if (!target)
        {
            handler->PSendSysMessage("Player '%s' is not online.", nameToken.c_str());
            return true;
        }
    }
    else
    {
        // No name: the selected player, or the caller themself when nothing
        // (or no player) is selected.
        Unit* selected = requester->GetSelectedUnit();
        target = selected ? selected->ToPlayer() : nullptr;
        if (!target)
            target = requester;
    }

    if (BotManager::Instance().IsBot(target->GetObjectGuid()))
    {
        handler->PSendSysMessage("'%s' is a bot - owned bots are geared with .bot autogear.", target->GetName());
        return true;
    }

    bool const shapeshifted = target->GetClass() == CLASS_DRUID && target->GetShapeshiftForm() != FORM_NONE;
    switch (TortoiseBots::DecidePlayerAutogearTargetState(target->IsAlive(), target->IsInCombat(), shapeshifted))
    {
        case TortoiseBots::PlayerAutogearTargetState::Dead:
            handler->PSendSysMessage("'%s' must be alive.", target->GetName());
            return true;
        case TortoiseBots::PlayerAutogearTargetState::InCombat:
            handler->PSendSysMessage("'%s' must be out of combat.", target->GetName());
            return true;
        case TortoiseBots::PlayerAutogearTargetState::Shapeshifted:
            handler->PSendSysMessage("'%s' must leave the shapeshift form first.", target->GetName());
            return true;
        case TortoiseBots::PlayerAutogearTargetState::Ready:
            break;
    }

    bool const callerIsGm = requester->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
    switch (TortoiseBots::DecidePlayerAutogearPermission(callerIsGm, sPlayerbotAIConfig.playerAutogearForAll,
        target == requester, IsChallengeModeCharacter(target)))
    {
        case TortoiseBots::PlayerAutogearPermission::Allowed:
            break;
        case TortoiseBots::PlayerAutogearPermission::GmOnly:
            handler->PSendSysMessage("Autogear is GM-only (AiPlayerbot.PlayerAutogearForAll = 1 lets every player gear themselves).");
            return true;
        case TortoiseBots::PlayerAutogearPermission::NotSelf:
            handler->PSendSysMessage("You may only gear yourself with .autogear.");
            return true;
        case TortoiseBots::PlayerAutogearPermission::ChallengeGmOnly:
            handler->PSendSysMessage("'%s' is on a challenge mode - only a GM may gear that character.", target->GetName());
            return true;
    }

    uint8 const classId = target->GetClass();
    std::string const scale = TortoiseBots::MatchAutogearSpec(classId, specToken);
    uint32 const specId = scale.empty() ? 0 : sRandomItemMgr.GetSpecIdFor(classId, scale);
    if (!specId)
    {
        std::string const valid = TortoiseBots::ListAutogearSpecs(classId);
        handler->PSendSysMessage("Unknown spec '%s'%s. Valid specs: %s",
            specToken.c_str(), target == requester ? "" : " for that class",
            valid.empty() ? "none for this class" : valid.c_str());
        return true;
    }

    // Spawn ceiling of a pool bot of the target's level; the ilvl argument
    // may only lower it below max level, and may raise it at max level.
    uint32 const defaultCeiling = TortoiseBots::GearItemLevelCap(target->GetLevel(),
        sPlayerbotAIConfig.randomGearMaxItemLevelAboveLevel, sPlayerbotAIConfig.randomGearMaxLevel);
    TortoiseBots::PlayerAutogearIlvl const ilvl = TortoiseBots::ResolvePlayerAutogearIlvl(
        target->GetLevel(), TortoiseBots::kAutogearMaxLevel, requestedIlvl, defaultCeiling);
    if (ilvl.clamped)
        handler->PSendSysMessage("Item level %u is above the ceiling for level %u; using %u.",
            requestedIlvl, target->GetLevel(), defaultCeiling);

    PlayerbotFactory factory(target, target->GetLevel());
    factory.AutogearPlayer(specId, ilvl.effective, ilvl.effective > defaultCeiling);

    Item const* ranged = target->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    ItemPrototype const* rangedProto = ranged ? ranged->GetProto() : nullptr;

    // Autogear hands out no ammo: warn when the equipped ranged weapon has
    // none in the bags, like the owned-bot command does.
    if (rangedProto && rangedProto->Class == ITEM_CLASS_WEAPON && !factory.HasAmmoInBagsFor(rangedProto->SubClass))
    {
        bool const gun = rangedProto->SubClass == ITEM_SUBCLASS_WEAPON_GUN;
        handler->PSendSysMessage("Warning: %s has %s equipped but no %s in the bags - auto shot will not fire until they get some.",
            target->GetName(), rangedProto->Name1, gun ? "bullets" : "arrows");
    }

    TortoiseBots::PlayerAutogearRunCounts counts;
    for (PlayerbotFactory::AutogearUpgrade const& upgrade : factory.autogearUpgrades)
        TortoiseBots::CountPlayerAutogearRecord(counts, upgrade.oldItemId, upgrade.newItemId, upgrade.mailed);

    if (factory.autogearUpgrades.empty())
        handler->PSendSysMessage("No upgrades for %s (%s) at item level %u - nothing changed.",
            target->GetName(), specToken.c_str(), ilvl.effective);
    else
        handler->PSendSysMessage("Geared %s as '%s': %u slot(s) equipped, %u replaced item(s) to the bags, %u mailed.",
            target->GetName(), specToken.c_str(), counts.slotsEquipped, counts.replaced - counts.mailed, counts.mailed);

    std::ostringstream logLine;
    logLine << "PlayerAutogear: caller=" << requester->GetName() << " target=" << target->GetName()
            << " spec=" << scale << " ilvl=" << ilvl.effective;
    for (PlayerbotFactory::AutogearUpgrade const& upgrade : factory.autogearUpgrades)
        logLine << " [" << uint32(upgrade.slot) << "] " << upgrade.oldItemId << "->" << upgrade.newItemId
            << (upgrade.mailed ? " (mailed)" : "");
    sLog.outString("%s", logLine.str().c_str());
    return true;
}

// `.bot help` answers with the addon protocol version line; the TBM addon
// parses it, so the output must stay exactly as it is.
bool HandleHelp(ChatHandler* handler, char const* /*args*/)
{
    handler->PSendSysMessage("TBM:VERSION|%s", BuildVersion().c_str());
    return true;
}

// One row per `.bot` subcommand: primary name, handler, short usage, aliases, and whether it shows
// in the top-level usage line. Dispatch and the docs check (tools/verify_command_docs.py) read every
// row; the usage line skips the work-in-progress commands while their flags are off (typed anyway,
// they answer 'disabled').
struct BotCommandEntry
{
    char const* name;
    bool (*handler)(ChatHandler*, char const*);
    char const* usage;
    std::initializer_list<char const*> aliases;
    bool (*visible)();
};

static bool AlwaysVisible() { return true; }
static bool CommanderVisible() { return RemoteCommander::CommanderSurfaceOn(CurrentCommanderGates()); }
static bool TraceVisible() { return sPlayerbotAIConfig.runTrace; }
static bool RunVisible() { return RemoteCommander::RunSurfaceOn(CurrentCommanderGates()); }

static BotCommandEntry const botCommands[] = {
    {"ah", HandleAhBot, "ah <status|reload|rebuild|item ...>", {"ahbot"}, AlwaysVisible},
    {"action", HandleAction, "action <intent>", {}, AlwaysVisible},
    {"commander", HandleCommander, "commander <bind|unbind|status|view ...>", {}, CommanderVisible},
    {"cmd", HandleCommanderOrder, "cmd <order>", {}, CommanderVisible},
    {"trace", HandleTrace, "trace <start|stop|status> ...", {}, TraceVisible},
    {"run", HandleDungeonRun, "run <start|pause|resume|skip|stop|status|list|here> ...", {}, RunVisible},
    {"add", HandleAdd, "add <characterName>", {}, AlwaysVisible},
    {"remove", HandleRemove, "remove <characterName>", {"rm"}, AlwaysVisible},
    {"logout", HandleLogout, "logout <online bot name>", {}, AlwaysVisible},
    {"roster", HandleRoster, "roster", {}, AlwaysVisible},
    {"follow", HandleFollow, "follow <botName>", {}, AlwaysVisible},
    {"invite", HandleInvite, "invite <online bot name>", {}, AlwaysVisible},
    {"uninvite", HandleUninvite, "uninvite <online bot name>", {"kick"}, AlwaysVisible},
    {"stay", HandleStay, "stay <online bot name>", {}, AlwaysVisible},
    {"guard", HandleGuard, "guard <online bot name>", {}, AlwaysVisible},
    {"free", HandleFree, "free <online bot name>", {}, AlwaysVisible},
    {"ready", HandleReady, "ready <online bot name>", {}, AlwaysVisible},
    {"attack", HandleAttack, "attack <online bot name>", {}, AlwaysVisible},
    {"interrupt", HandleInterrupt, "interrupt <online bot name>", {}, AlwaysVisible},
    {"formation", HandleFormation, "formation [botName] <formation>", {}, AlwaysVisible},
    {"cc", HandleCc, "cc <mark> [bot] | cc clear [bot]", {}, AlwaysVisible},
    {"list", HandleList, "list", {}, AlwaysVisible},
    {"stats", HandleStats, "stats", {}, AlwaysVisible},
    {"status", HandleStatus, "status <online bot name>", {}, AlwaysVisible},
    {"lease", HandleLease, "lease [status]", {}, AlwaysVisible},
    {"version", HandleVersion, "version", {"v", "about", "credits"}, AlwaysVisible},
    {"features", HandleFeatures, "features", {}, AlwaysVisible},
    {"pullback", HandlePullback, "pullback", {"pull-back"}, AlwaysVisible},
    {"role", HandleRole, "role <online bot name/self/your name> <tank/healer/dps/clear>", {}, AlwaysVisible},
    {"summon", HandleSummon, "summon <online bot name>", {}, AlwaysVisible},
    {"maintenance", HandleMaintenance, "maintenance <online bot name>", {}, AlwaysVisible},
    {"autogear", HandleAutogear, "autogear <online bot name> [quality|ilvl]", {}, AlwaysVisible},
    {"hire", HandleHire, "hire <class> [dps/tank/healer] ... | hire raid <preset> [tier]", {}, AlwaysVisible},
    {"pool", HandlePool, "pool <status|accounts|adopt ...>", {}, AlwaysVisible},
    {"loot", HandleLoot, "loot [on/off]", {}, AlwaysVisible},
    {"lootlog", HandleLootLog, "lootlog [cursor]", {}, AlwaysVisible},
    {"repair", HandleRepair, "repair", {}, AlwaysVisible},
    {"sell", HandleSell, "sell", {}, AlwaysVisible},
    {"downtime", HandleDowntime, "downtime [online bot name]", {}, AlwaysVisible},
    {"rest", HandleRest, "rest", {"drink", "eat"}, AlwaysVisible},
    {"release", HandleRelease, "release", {}, AlwaysVisible},
    {"corpse", HandleCorpseShortcut, "corpse run", {}, AlwaysVisible},
    {"corpserun", HandleCorpseRun, "corpserun", {}, AlwaysVisible},
    {"learn", HandleLearn, "learn", {}, AlwaysVisible},
    {"trade", HandleTrade, "trade", {}, AlwaysVisible},
    {"command", HandleMatureCommand, "command <botName> <Playerbot command>", {}, AlwaysVisible},
    {"inv", HandleInventory, "inv <online bot you control>", {"inventory"}, AlwaysVisible},
    {"item", HandleItem, "item <bot> <trade|equip|unequip|give ...>", {}, AlwaysVisible},
    {"behavior", HandleBehavior, "behavior <bot|party|raid> <behavior> <on|off>", {"behaviour"}, AlwaysVisible},
    {"totem", HandleTotem, "totem <bot|party|raid> ...", {}, AlwaysVisible},
    {"strategy", HandleStrategy, "strategy <+/-/~><strategy name> [role or botName] | strategy reset", {}, AlwaysVisible},
    {"help", HandleHelp, "help", {"h"}, AlwaysVisible},
};

static BotCommandEntry const* FindBotCommand(std::string const& cmd)
{
    for (BotCommandEntry const& entry : botCommands)
    {
        if (cmd == entry.name)
            return &entry;
        for (char const* alias : entry.aliases)
            if (cmd == alias)
                return &entry;
    }
    return nullptr;
}

static std::string BuildBotUsageLine()
{
    std::string usage = "Usage: .bot ";
    bool first = true;
    for (BotCommandEntry const& entry : botCommands)
    {
        if (!entry.visible())
            continue;
        if (!first)
            usage += '/';
        first = false;
        usage += entry.name;
    }
    return usage;
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
        handler->PSendSysMessage("%s", BuildBotUsageLine().c_str());
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

    BotCommandEntry const* entry = FindBotCommand(cmd);
    if (!entry)
    {
        handler->PSendSysMessage("Unknown bot command '%s'. Try .bot help", cmd.c_str());
        return true;
    }
    return entry->handler(handler, subArgs);
}

// pi-lens-ignore: clang:incomplete_member_access
bool TryHandleBotCommand(ChatHandler* handler, char const* text)
{
    if (!handler || !text)
        return false;
    // Check for ".autogear" (real players; bots use ".bot autogear")
    if (strncmp(text, "autogear", 8) == 0 && (text[8] == '\0' || text[8] == ' ' || text[8] == '\t'))
        return HandlePlayerAutogear(handler, text + 8);
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
