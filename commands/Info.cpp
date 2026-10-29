// pi-lens-ignore-file: all
// pi-lens-ignore: clang:pp_file_not_found
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"
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

bool HandleList(ChatHandler* handler, char const* /*args*/)
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
bool HandleStats(ChatHandler* handler, char const* /*args*/)
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
char const* MovementName(PlayerbotAI* ai)
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
bool HandleStatus(ChatHandler* handler, char const* args)
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
bool HandleVersion(ChatHandler* handler, char const* /*args*/)
{
    // Any player may ask; the version is public build metadata, not control.
    handler->PSendSysMessage("TortoiseBots %s", BuildVersion().c_str());
    handler->PSendSysMessage("TBM:VERSION|%s", BuildVersion().c_str());
    handler->PSendSysMessage("%s", AttributionLine().c_str());
    return true;
}
// `.bot features all <text>`: every bot config key holding <text>, for the
// addon's Info search. Capped, since the file holds thousands of keys.
static bool SendConfigSearch(ChatHandler* handler, std::string const& filter)
{
    constexpr size_t kMaxShown = 200;
    std::vector<std::pair<std::string, std::string>> const entries = sPlayerbotAIConfig.FindConfigEntries(filter);
    size_t const shown = std::min(entries.size(), kMaxShown);
    handler->PSendSysMessage("Config: %u keys match '%s'.", uint32(entries.size()), filter.c_str());
    handler->PSendSysMessage("TBM:CONFIG_BEGIN|%u", uint32(entries.size()));
    for (size_t i = 0; i < shown; ++i)
    {
        std::string key = entries[i].first;
        if (key.compare(0, 12, "AiPlayerbot.") == 0)
            key = key.substr(12);
        std::string value = entries[i].second;
        for (char& c : value)
            if (c == '|')
                c = '/';
        handler->PSendSysMessage("TBM:CONFIG|%s|%s", key.c_str(), value.c_str());
    }
    handler->PSendSysMessage("TBM:CONFIG_END|%u|%u", uint32(shown), uint32(entries.size()));
    return true;
}

// The server's player-facing switches, for the addon's Info window: one
// summary line for chat and TBM:FEATURE|<key>|<value>|<label> per switch.
bool HandleFeatures(ChatHandler* handler, char const* args)
{
    std::string const words = Trim(args ? args : "");
    if (words == "all" || words.compare(0, 4, "all ") == 0)
    {
        std::string const filter = Trim(words.substr(3));
        if (filter.size() < 2)
        {
            handler->PSendSysMessage("Usage: .bot features all <at least two letters of a setting name>");
            return true;
        }
        return SendConfigSearch(handler, filter);
    }
    struct Feature { char const* key; std::string value; char const* label; };
    auto onOff = [](bool on) { return std::string(on ? "on" : "off"); };
    PlayerbotAIConfig const& c = sPlayerbotAIConfig;
    std::vector<Feature> const features = {
        { "dungeon-run", onOff(c.dungeonSelfClear), "Dungeon runs" },
        { "player-led", onOff(c.dungeonRunPlayerLed), "Player-led runs (I lead)" },
        { "remote-commander", onOff(c.remoteCommander), "Remote commander" },
        { "dungeon-tactics", onOff(c.dungeonTactics), "Boss tactics for every bot" },
        { "bot-lfg", onOff(c.botLfgGroups), "Bot LFG groups" },
        { "hiring", onOff(c.hireEnabled), "Hiring" },
        { "hire-max", std::to_string(c.hireMaxBotsPerPlayer), "Hires per player" },
        { "hire-pvp", onOff(c.hireAllowPvP), "Hires in battlegrounds" },
        { "hire-anywhere", onOff(c.hireAnywhere), "Hire away from recruiters" },
        { "hire-tier", std::to_string(c.hireGearStartTier), "Hire gear start tier" },
        { "world-buffs", onOff(c.worldBuffsEnabled), "World buffs from recruiters" },
        { "autogear-all", onOff(c.playerAutogearForAll), "Autogear for every player" },
        { "alt-bots", onOff(c.altBots), "Alt bots" },
        { "ah-market", onOff(c.ahMarketEnabled), "Auction house market" },
        { "random-bots", std::to_string(c.maxRandomBots), "World bots (max)" },
        { "run-trace", onOff(c.runTrace), "Run trace" },
    };
    std::string summary;
    for (Feature const& f : features)
        summary += (summary.empty() ? "" : ", ") + std::string(f.key) + " " + f.value;
    handler->PSendSysMessage("Server features: %s", summary.c_str());
    for (Feature const& f : features)
        handler->PSendSysMessage("TBM:FEATURE|%s|%s|%s", f.key, f.value.c_str(), f.label);
    return true;
}
bool HandleLease(ChatHandler* handler, char const* args)
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
bool HandleRoster(ChatHandler* handler, char const* /*args*/)
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
    // Issue #872: rows that currently play on their own, in one extra line so
    // the strict roster row stays unchanged for older addons (they ignore it).
    std::string altBots;
    for (OwnedCharacter const& row : rows)
        if (AltBotService::Instance().IsAutonomous(row.characterGuid))
        {
            Player* alt = sObjectAccessor.FindPlayer(row.characterGuid);
            altBots += (altBots.empty() ? "" : ",") + ProtocolSafe(alt ? alt->GetName() : row.name);
        }
    handler->PSendSysMessage("TBM:ALTBOTS|%s", altBots.c_str());
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
    // BOTSTATE snapshot; loot-log = ".bot lootlog" and its TBM:LOOT history
    // lines; downtime = ".bot downtime" town errands for own alts;
    // dungeon-run = ".bot run" and its TBM:RUN status lines,
    // only while the dungeon run is enabled. Old addons ignore unknown lines and items.
    bool const dungeonRun = RemoteCommander::RunSurfaceOn(CurrentCommanderGates());
    handler->PSendSysMessage("TBM:CAPS|pull-seconds,flee,inventory,behavior,claimed-roster,loot-log,downtime%s",
        dungeonRun ? ",dungeon-run" : "");
    // Transport trailer: the addon sends its next commands over the addon
    // channel only while this says "party". See AddonCommandChannel.
    handler->PSendSysMessage("TBM:TRANSPORT|%s", AddonCommandChannel(requester));
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
