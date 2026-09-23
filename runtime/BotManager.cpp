#include "BotManager.h"
#include "BotActivityLease.h"
#include "PlayerbotAIAdapter.h"
#include "PlayerbotAIStorage.h"
#include "GearSeedingGuard.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/RandomBotFacade.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../host/BotSessionAdapter.h"
#include "../commands/BotCommands.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
#include "AccountMgr.h"
#include "WorldPacket.h"
#include "Chat.h"
#include "Group/Group.h"
#include "Maps/GridMap.h"
#include "Map.h"
#include "../commands/BotCommands.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../ai/playerbot/WorldPosition.h"
#include "../ai/playerbot/strategy/values/TravelValues.h"

#include "Database/DatabaseEnv.h"
#include "../host/ModuleLog.h"

#include <algorithm>
#include <ctime>

namespace TortoiseBots {

namespace {
// One-shot headless RNDBOT scatter using persisted GenericRpg destinations.
// Fail-closed: any validation miss retains original position. No DB scan,
// no GenerateTravelNodes, no homebind update.
bool IsUsableTeleportPoint(ai::WorldPosition const& point)
{
    if (!point.isOverworld() || !point.isValid() || !point.loadMapAndVMap(0))
        return false;

    // loadMapAndVMap above validates the navmesh; getTerrain and
    // GetWaterOrGroundLevel load the map grid/VMAP and resolve terrain height.
    // Reject stale spawn Z instead of teleporting a bot into the ground or air.
    TerrainInfo const* terrain = point.getTerrain();
    if (!terrain)
        return false;

    float groundZ = INVALID_HEIGHT;
    float maxZ = terrain->GetWaterOrGroundLevel(point.getX(), point.getY(), point.getZ(), &groundZ, false);
    return groundZ > INVALID_HEIGHT && maxZ > INVALID_HEIGHT &&
        point.getZ() >= groundZ && point.getZ() <= maxZ + 2.0f;
}

// Shared level-fitting picker for login scatter and post-rez rescue.
// Probes GenericRpg destinations in the bot's ±5 validated level window and
// returns a terrain-validated point. Destinations stay TravelMgr-owned.
// Returns nullptr on any miss (fail-closed, original position retained).
ai::WorldPosition const* PickLevelFittingPoint(::Player* bot)
{
    if (!bot)
        return nullptr;
    auto& travelMgr = MaNGOS::Singleton<ai::TravelMgr>::Instance();
    // Use bounded validated levels: persisted ai_playerbot_zone_level first (with
    // parent-zone cached fallback), then immutable DBC AreaTable AreaLevel / parent
    // AreaLevel. No creature scan, no DB write, no lazy GetAreaLevel mutation.
    // An empty stock-install table therefore still allows DBC-validated scatter
    // while an entirely unvalidated point/area still fails closed.
    ai::PlayerTravelInfo info(bot);
    // Fetch without level filtering (onlyPossible=false) and without distance bias (maxDistance=0)
    // to avoid lazy IsPossible scans and to scatter across all level-appropriate zones, not just near logout pos.
    auto dests = travelMgr.GetDestinations(info, (uint32)ai::TravelDestinationPurpose::GenericRpg, {}, false, 0);
    if (dests.empty())
    {
        TB_LOG_DETAIL("TortoiseBots: random teleport no GenericRpg destinations for bot %s level %u", bot->GetName(), bot->GetLevel());
        return nullptr;
    }

    // Probe a bounded random subset. Destination points can be numerous, and
    // loading terrain/MMAP for every point would turn login into a world stall.
    constexpr uint32 maxDestinationAttempts = 32;
    constexpr uint32 maxPointAttempts = 8;
    ai::WorldPosition* chosen = nullptr;
    uint32 destinationAttempts = std::min<uint32>(maxDestinationAttempts, dests.size());
    // Level window rationale (both bounds, no speculative config):
    //  Upper +5 from local RpgTravelDestination::IsPossible (destAreaLevel > botLevel+5 => reject) and
    //   quest level gate (questLevel > botLevel+5 => reject).
    //  Lower -5 from donor AiPlayerbot.RandomBotTeleLevel=5 window where creature avg satisfies
    //   botLevel-5 <= creatureLevel <= botLevel (see RandomPlayerbotMgr::PrepareTeleportCache query delta in [0,5])
    //   and local Grind lower bound approx botLevel-12. Symmetric ±5 is the minimal conservative window
    //   that prevents both 60->Elwynn and 10->Winterspring mismatches without new config; enforced on
    //   bounded validated levels (TryGetValidatedAreaLevel: cached + DBC AreaLevel/parent), never via lazy GetAreaLevel/IsLocationLevelValid.
    int32 botLevel = (int32)bot->GetLevel();
    int32 lower = botLevel - 5;
    if (lower < 1) lower = 1;
    int32 upper = botLevel + 5;
    if (upper > 60) upper = 60;
    for (uint32 destinationAttempt = 0; destinationAttempt < destinationAttempts && !chosen; ++destinationAttempt)
    {
        ai::TravelDestination* destination = dests[urand(0, static_cast<uint32>(dests.size() - 1))];
        if (!destination)
            continue;

        auto points = destination->GetPoints();
        uint32 pointAttempts = std::min<uint32>(maxPointAttempts, points.size());
        for (uint32 pointAttempt = 0; pointAttempt < pointAttempts; ++pointAttempt)
        {
            ai::WorldPosition* point = points[urand(0, static_cast<uint32>(points.size() - 1))];
            if (!point)
                continue;
            // Reject unresolved area flag rather than silently using linkedZone fallback
            // from GetByAreaFlagAndMap. Allow safe parent-zone cached lookup via helper.
            if (point->getAreaFlag() == 0)
                continue;
            AreaTableEntry const* area = point->GetArea();
            if (!area)
                continue;
            uint32 zoneId = area->ZoneId ? area->ZoneId : area->Id;
            if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
                (PlayerbotAIConfig::IsIsolatedCustomZone(zoneId) || PlayerbotAIConfig::IsIsolatedCustomZone(area->Id)))
                continue;
            if (point->IsEnemyHomeZoneFor(info.GetTeam()))
                continue;
            int32 areaLevel;
            if (!travelMgr.TryGetValidatedAreaLevel(area->Id, areaLevel))
                continue;
            if (areaLevel < lower || areaLevel > upper)
                continue;
            if (IsUsableTeleportPoint(*point))
            {
                chosen = point;
                break;
            }
        }
    }

    if (!chosen)
    {
        TB_LOG_DETAIL("TortoiseBots: random teleport no valid overworld point for bot %s level %u", bot->GetName(), bot->GetLevel());
        return nullptr;
    }
    return chosen;
}

bool TryRandomTeleport(::Player* bot, BotRecord const& record)
{
    if (!sPlayerbotAIConfig.enableRandomTeleports)
        return false;
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;
    if (!record.random)
        return false;
    // Low-level bots are not scattered at all: below level 10 their quests are in the
    // starting area and a level-fitting inn elsewhere (a capital, say) puts zones they
    // cannot cross between them and their work.
    if (bot->GetLevel() < 10)
        return false;
    if (bot->IsBeingTeleported())
        return false;
    if (!bot->IsInWorld())
        return false;
    if (sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()))
    {
        TB_LOG_DEBUG("TortoiseBots: random teleport skipped pinned bot %s", bot->GetName());
        return false;
    }
    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        TB_LOG_DETAIL("TortoiseBots: random teleport no AI for bot %s, retaining position", bot->GetName());
        return false;
    }
    ai::WorldPosition const* chosen = PickLevelFittingPoint(bot);
    if (!chosen)
        return false;

    bool ok = bot->TeleportTo(chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ(), bot->GetOrientation(), 0);
    if (ok)
        TB_LOG_DETAIL("TortoiseBots: random teleport bot %s level %u to map %u %.1f %.1f %.1f", bot->GetName(), bot->GetLevel(), chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ());
    else
        sLog.outError("TortoiseBots: random teleport TeleportTo failed for bot %s to map %u %.1f %.1f %.1f, retaining position", bot->GetName(), chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ());
    return ok;
}
} // namespace

namespace {
// Fail-closed eligibility shared by death-driven and time-driven rescue: a
// random masterless ungrouped headless bot standing in a zone its level
// cannot survive (same +5 tolerance the travel and quest gates use).
// Unknown area levels fail closed. Pure predicates, no side effects.
// `why` (optional) receives the reason for a "no", for the post-revive diagnostic line.
bool GroupHasRealPlayer(::Player* bot)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->getSource())
            if (member != bot && member->GetSession() && !member->GetSession()->IsHeadless())
                return true;
    return false;
}

bool MisplacedBotEligible(::Player* bot, int32& areaLevelOut, std::string* why = nullptr)
{
    areaLevelOut = 0;
    auto no = [&](std::string const& reason) { if (why) *why = reason; return false; };
    if (!sPlayerbotAIConfig.relocateHopelessDeaths)
        return no("relocation disabled");
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return no("no headless session");
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->InBattleGround())
        return no(!bot->IsInWorld() ? "not in world" : bot->IsBeingTeleported() ? "being teleported" : "in a battleground");
    // A group with a real player in it is that player's business. A group of bots only is
    // not: the diagnostic showed 21 of 31 post-revive checks refused with "in a group" while
    // the bots kept dying in the same spot (bots group up among themselves all the time).
    // The misplaced bot leaves the group when it is moved (TeleportMisplacedBot).
    if (GroupHasRealPlayer(bot))
        return no("in a group with a player");
    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record || !record->random || record->lifecycle != BotLifecycle::InWorld)
        return no(!record ? "no bot record" : !record->random ? "not a random bot" : "record not in world");
    if (!record->masterGuid.IsEmpty())
        return no("has a master");
    if (sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()))
        return no("pinned bot");
    if (BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow()) == BotActivity::Dungeon)
        return no("dungeon crew");

    // Isolated custom starting zones (Alah'Thalas 2040, Thalassian Highlands 5225, Blackstone Island 5536)
    // lack walking paths/transports to the mainland; random bots here are always hopelessly misplaced.
    if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
        (PlayerbotAIConfig::IsIsolatedCustomZone(bot->GetAreaId()) || PlayerbotAIConfig::IsIsolatedCustomZone(bot->GetZoneId())))
    {
        areaLevelOut = 50;
        return true;
    }

    auto& travelMgr = MaNGOS::Singleton<ai::TravelMgr>::Instance();
    int32 areaLevel = 0;
    if (!travelMgr.TryGetValidatedAreaLevel(bot->GetAreaId(), areaLevel) || areaLevel <= 0)
        return no("area " + std::to_string(bot->GetAreaId()) + " has no validated level");
    if (areaLevel <= (int32)bot->GetLevel() + 5)
        return no("area " + std::to_string(bot->GetAreaId()) + " level " + std::to_string(areaLevel) + " fits level " + std::to_string(bot->GetLevel()) + "+5");
    areaLevelOut = areaLevel;
    return true;
}

// Sweep cadence and patience: a check every few minutes, relocation only
// after uninterrupted stranding, so briefly passing through is never caught.
constexpr uint32_t STRANDED_SWEEP_INTERVAL_MS = 5 * 60 * 1000;
constexpr time_t STRANDED_GRACE_SEC = 15 * 60;

// Shared destination leg: below level 10 the bot belongs at its race start
// (a capital the picker would choose lies behind zones it cannot cross
// alive, and the home bind may sit in another race's zone). Home bind stays
// as fallback. Otherwise a validated level-fitting point. Resets the death
// count on success so the fresh start is not mistaken for a loop.
bool TeleportMisplacedBot(::Player* bot, int32 areaLevel, const std::string& cause)
{
    // Leave a bot-only group first: the others stay where they are, the moved bot must not
    // be pulled back or keep them as "attackers" context from another zone.
    if (bot->GetGroup() && bot->GetSession())
    {
        WorldPacket leave;
        bot->GetSession()->HandleGroupDisbandOpcode(leave);
    }
    if (bot->GetLevel() < 10)
    {
        // Goblin and High Elf bots are spawned in Durotar/Elwynn with their homebind
        // set accordingly. GetPlayerInfo() returns the DBC island coordinates
        // (Blackstone Island 5536 and Thalassian Highlands 5225) which are bot-excluded,
        // so route these two races directly through homebind.
        bool useHomebindOverride = bot->GetRace() == RACE_GOBLIN || bot->GetRace() == RACE_HIGH_ELF;
        PlayerInfo const* info = useHomebindOverride ? nullptr : sObjectMgr.GetPlayerInfo(bot->GetRace(), bot->GetClass());
        bool moved = info && bot->TeleportTo(info->mapId, info->positionX, info->positionY, info->positionZ, info->orientation);
        if (!moved)
            moved = bot->TeleportToHomebind(0, false);
        if (!moved)
        {
            sLog.outError("TortoiseBots: misplaced-bot relocation to the starting area failed for bot %s, retaining position", bot->GetName());
            return false;
        }
        bot->SaveToDB();
    }
    else
    {
        ai::WorldPosition const* chosen = PickLevelFittingPoint(bot);
        if (!chosen)
            return false;
        if (!bot->TeleportTo(chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ(), bot->GetOrientation(), 0))
        {
            sLog.outError("TortoiseBots: misplaced-bot relocation TeleportTo failed for bot %s, retaining position", bot->GetName());
            return false;
        }
        bot->SaveToDB();
    }
    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (ai && ai->GetAiObjectContext())
    {
        if (auto* deathCountValue = ai->GetAiObjectContext()->GetValue<uint32>("death count"))
            deathCountValue->Reset();
    }
    sLog.outString("TortoiseBots: relocated %s %s level %u from area level %d",
        cause.c_str(), bot->GetName(), bot->GetLevel(), areaLevel);
    return true;
}
} // namespace

bool BotManager::RelocateHopelessBot(::Player* bot)
{
    // Diagnostic (15.9.): one line per revive saying why a bot was NOT relocated - a level-8
    // bot died fifteen times to level-20 spiders in Hillsbrad without a single relocation
    // although the zone table and the rule said it should have been.
    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    auto* deathCountValue = (ai && ai->GetAiObjectContext()) ? ai->GetAiObjectContext()->GetValue<uint32>("death count") : nullptr;
    uint32 deathCount = deathCountValue ? deathCountValue->Get() : 0;
    int32 areaLevel = 0;
    std::string why;
    if (!MisplacedBotEligible(bot, areaLevel, &why))
    {
        if (bot && deathCount)
            TB_LOG_BASIC("TortoiseBots: hopeless check %s level %u zone %u area %u deaths %u: not eligible - %s",
                bot->GetName(), bot->GetLevel(), bot->GetZoneId(), bot->GetAreaId(), deathCount, why.c_str());
        return false;
    }
    if (!ai || !ai->GetAiObjectContext())
        return false;
    uint32 minDeaths = ((bot->GetLevel() < 10 && areaLevel >= 20) ||
        (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
         (PlayerbotAIConfig::IsIsolatedCustomZone(bot->GetAreaId()) || PlayerbotAIConfig::IsIsolatedCustomZone(bot->GetZoneId())))) ? 1 : 2;
    if (deathCount < minDeaths)
    {
        TB_LOG_BASIC("TortoiseBots: hopeless check %s level %u zone %u area %u (level %d) deaths %u: below %u, not yet",
            bot->GetName(), bot->GetLevel(), bot->GetZoneId(), bot->GetAreaId(), areaLevel, deathCount, minDeaths);
        return false;
    }
    return TeleportMisplacedBot(bot, areaLevel, "hopeless bot (" + std::to_string(deathCount) + " deaths)");
}

bool BotManager::RelocateStrandedBot(::Player* bot)
{
    int32 areaLevel = 0;
    if (!MisplacedBotEligible(bot, areaLevel))
        return false;
    return TeleportMisplacedBot(bot, areaLevel, "stranded bot");
}

void BotManager::SweepStrandedBots(uint32_t diff)
{
    m_strandedSweepElapsedMs += diff;
    if (m_strandedSweepElapsedMs < STRANDED_SWEEP_INTERVAL_MS)
        return;
    m_strandedSweepElapsedMs = 0;
    if (!sPlayerbotAIConfig.relocateHopelessDeaths)
    {
        m_strandedSince.clear();
        return;
    }
    time_t now = time(nullptr);
    for (auto& [key, entry] : m_bots)
    {
        BotRecord& rec = entry.record;
        ::Player* p = nullptr;
        bool stranded = rec.random && rec.lifecycle == BotLifecycle::InWorld && rec.masterGuid.IsEmpty() &&
            (p = sObjectAccessor.FindPlayer(rec.characterGuid)) != nullptr &&
            p->GetSession() && p->GetSession()->IsHeadless() && p->IsInWorld() &&
            !GroupHasRealPlayer(p) && !p->InBattleGround() && !p->IsBeingTeleported() &&
            !sRandomBotFacade.IsPinnedBot(key);
        int32 areaLevel = 0;
        if (stranded)
        {
            if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
                (PlayerbotAIConfig::IsIsolatedCustomZone(p->GetAreaId()) || PlayerbotAIConfig::IsIsolatedCustomZone(p->GetZoneId())))
            {
                RelocateStrandedBot(p);
                m_strandedSince.erase(key);
                continue;
            }

            auto& travelMgr = MaNGOS::Singleton<ai::TravelMgr>::Instance();
            stranded = travelMgr.TryGetValidatedAreaLevel(p->GetAreaId(), areaLevel) && areaLevel > 0 &&
                areaLevel > (int32)p->GetLevel() + 5;
        }
        if (!stranded)
        {
            m_strandedSince.erase(key);
            continue;
        }
        time_t grace = (p->GetLevel() < 10 && areaLevel >= 20) ? 60 : STRANDED_GRACE_SEC;
        auto it = m_strandedSince.find(key);
        if (it == m_strandedSince.end())
        {
            m_strandedSince.emplace(key, now);
            continue;
        }
        if (now - it->second < grace)
            continue;
        m_strandedSince.erase(it);
        RelocateStrandedBot(p);
    }
    for (auto it = m_strandedSince.begin(); it != m_strandedSince.end();)
    {
        if (m_bots.find(it->first) == m_bots.end())
            it = m_strandedSince.erase(it);
        else
            ++it;
    }
}

// A Headless session must never render an owned bot as an account-level GM.
// Its Network owner retains all account privileges; this only normalizes the
// separately logged-in bot character after the core restores saved GM flags.
bool NormalizeHeadlessGmPresentation(::Player* bot)
{
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;

    uint32 const gmFlags = PLAYER_EXTRA_GM_ON | PLAYER_EXTRA_GM_ACCEPT_TICKETS |
        PLAYER_EXTRA_GM_INVISIBLE | PLAYER_EXTRA_GM_CHAT |
        PLAYER_EXTRA_GM_DISABLE_SOCIAL;
    uint32 const flags = bot->GetExtraFlags();
    if (!(flags & gmFlags) && (flags & PLAYER_EXTRA_ACCEPT_WHISPERS) &&
        bot->IsGMVisible() && !bot->IsGameMaster() && bot->GetInvincibilityHpThreshold() == 0)
    {
        return false;
    }

    bot->SetInvincibilityHpThreshold(0);
    bot->SetGameMaster(false);
    if (flags & PLAYER_EXTRA_GM_CHAT)
        bot->SetGMChat(false);
    if (flags & PLAYER_EXTRA_GM_ACCEPT_TICKETS)
        bot->SetAcceptTicket(false);
    if (flags & PLAYER_EXTRA_GM_DISABLE_SOCIAL)
        bot->SetGMSocials(true);
    if (!(flags & PLAYER_EXTRA_ACCEPT_WHISPERS))
        bot->SetAcceptWhispers(true);

    // Persist only the bot's presentation flags. The owner's account rank and
    // its Network session are untouched.
    bot->SetGMVisible(true);
    return true;
}

BotEntry::~BotEntry() = default;

BotManager& BotManager::Instance()
{
    static BotManager instance;
    return instance;
}

// A character with any primary profession has been through the skill initialisation
// (or learned one on its own) - either way it must not be re-rolled.
static bool HasPrimaryProfession(::Player* player)
{
    static uint16 const kPrimary[] = { SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_ENCHANTING, SKILL_ENGINEERING,
                                       SKILL_HERBALISM, SKILL_LEATHERWORKING, SKILL_MINING, SKILL_SKINNING, SKILL_TAILORING };
    for (uint16 id : kPrimary)
        if (player->HasSkill(id))
            return true;
    return false;
}

void BotManager::OnPlayerLogin(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    ::WorldSession* session = player->GetSession();
    if (!session || session->HasNetworkTransport())
    {
        if (it != m_bots.end())
            ReleaseToClient(player);
        RebindOwnedBots(player);
        return;
    }

    if (it == m_bots.end())
        return;

    NormalizeHeadlessGmPresentation(player);

    BotEntry& entry = it->second;
    BotRecord& record = entry.record;
    if (record.enteredWorld)
        return;

    if (!entry.aiAdapter)
    {
        ::Player* masterPlayer = nullptr;
        if (record.masterGuid && !record.masterGuid.IsEmpty())
            masterPlayer = sObjectAccessor.FindPlayer(record.masterGuid);

        entry.aiAdapter = std::make_unique<PlayerbotAIAdapter>(player, masterPlayer);
    }

    if (!entry.aiAdapter->IsInitialized() && !entry.aiAdapter->Initialize())
    {
        sLog.outError("TortoiseBots: PlayerbotAI attach failed for %s; stopping the Headless session",
            player->GetName());
        record.enteredWorld = false;
        record.lifecycle = BotLifecycle::Removing;
        record.syncedInWorld = false;
        sRandomBotFacade.MarkNativePlayersDirty();
        entry.aiAdapter->Shutdown();
        BotSessionAdapter::StopHeadlessSession(record.characterGuid, true);
        return;
    }

    // Do not publish an in-world/controllable record until the adapter has
    // registered the exact AI instance that owns this live Player.
    if (!entry.aiAdapter->IsUsable())
    {
        sLog.outError("TortoiseBots: PlayerbotAI attach for %s is not usable; stopping the Headless session",
            player->GetName());
        record.enteredWorld = false;
        record.lifecycle = BotLifecycle::Removing;
        record.syncedInWorld = false;
        sRandomBotFacade.MarkNativePlayersDirty();
        entry.aiAdapter->Shutdown();
        BotSessionAdapter::StopHeadlessSession(record.characterGuid, true);
        return;
    }

    record.enteredWorld = true;
    record.lifecycle = BotLifecycle::InWorld;
    record.syncedInWorld = true;
    sRandomBotFacade.MarkNativePlayersDirty();

    // Normalize Goblin and High Elf (and any random bot in custom isolated
    // starting zones lacking navmesh/transport to mainland) to standard faction starting zones.
    if (record.random && !sPlayerbotAIConfig.allowIsolatedCustomStartingZones)
    {
        uint32 zoneId = player->GetZoneId();
        uint32 areaId = player->GetAreaId();
        if (PlayerbotAIConfig::IsIsolatedCustomZone(zoneId) || PlayerbotAIConfig::IsIsolatedCustomZone(areaId))
        {
            if (player->GetTeam() == HORDE)
            {
                player->TeleportTo(1, -618.518f, -4251.67f, 38.718f, 0.0f);
                player->SetHomebindToLocation(WorldLocation(1, -618.518f, -4251.67f, 38.718f, 0.0f), 14);
                player->SaveToDB();
                TB_LOG_DETAIL("TortoiseBots: normalized Horde bot %s spawn from isolated zone %u to Valley of Trials", player->GetName(), zoneId);
            }
            else
            {
                player->TeleportTo(0, -8949.95f, -132.493f, 83.5312f, 0.0f);
                player->SetHomebindToLocation(WorldLocation(0, -8949.95f, -132.493f, 83.5312f, 0.0f), 12);
                player->SaveToDB();
                TB_LOG_DETAIL("TortoiseBots: normalized Alliance bot %s spawn from isolated zone %u to Northshire", player->GetName(), zoneId);
            }
        }
    }

    // Level seeding runs before the login scatter so PickLevelFittingPoint sees
    // the seeded level (the scatter itself is gated on level >= 10).
    if (record.random &&
        TortoiseBots::NeedsInitialGearSeeding(player->GetTotalPlayedTime(), sRandomBotFacade.GetValue(player->GetGUIDLow(), "levelSeeded")))
    {
        uint32 seedLevel = urand(sPlayerbotAIConfig.randomBotStartLevelMin,
            std::max(sPlayerbotAIConfig.randomBotStartLevelMin, sPlayerbotAIConfig.randomBotStartLevelMax));
        if (player->GetLevel() < seedLevel)
        {
            player->GiveLevel(seedLevel);
            sRandomBotFacade.SetValue(player->GetGUIDLow(), "levelSeeded", 1);
            TB_LOG_DETAIL("TortoiseBots: seeded fresh bot %s to level %u.", player->GetName(), seedLevel);
        }
    }

    // m_canDualWield is not persisted (the Player ctor clears it and the
    // spellbook passive chain does not re-fire on login), which would make
    // FindEquipSlot/CanEquipItem refuse every future offhand swap for
    // dual-wielders. Restore it for the four classes that train Dual Wield
    // (seed-time InitSkills grants the same flag).
    if (record.random && player->GetLevel() >= 10 &&
        (player->GetClass() == CLASS_WARRIOR || player->GetClass() == CLASS_HUNTER ||
         player->GetClass() == CLASS_ROGUE || player->GetClass() == CLASS_SHAMAN))
        player->SetCanDualWield(true);

    // Initial gear seeding is for fresh pool bots only, never earned progression.
    // See GearSeedingGuard.h for the dual-heuristic rule. Stamp after the
    // attempt, not before: a crash between stamp and equip must not matter, and
    // a bot that received no enrichment simply progresses on earned drops.
    // CharacterCreation creates level 1 starter kit; factory's InitEquipment
    // intentionally no-ops for <5. For any auto-created level >=5 (future
    // higher-level pool or manual leveling), trigger gear enrichment immediately
    // on world-thread login rather than waiting ~6h for RandomBotService's
    // randomize interval. Safe and synchronous; the existing
    // randomGearUpgradeEnabled setting controls this (default enabled).
    uint32 botGuidLow = player->GetGUIDLow();

    bool freshBot = TortoiseBots::NeedsInitialGearSeeding(
        player->GetTotalPlayedTime(), sRandomBotFacade.GetValue(botGuidLow, "seeded"));
    if (record.random && sPlayerbotAIConfig.randomGearUpgradeEnabled && player->GetLevel() >= 5 && freshBot)
    {
        sRandomBotFacade.UpdateGearSpells(player);
        sRandomBotFacade.SetValue(botGuidLow, "seeded", 1);
        TB_LOG_DETAIL("TortoiseBots: seeded starter gear for fresh bot %s.", player->GetName());
    }

    // Skills are separate from gear seeding. With DisableRandomLevels=1 a bot never goes
    // through Randomize(), the only caller of InitAllSkills(): it would keep weapon
    // skill 1 for good and never get a profession, so grind, craft and gather have
    // nothing to work with. Give such a bot the level-bound skill set once, at any
    // level; from then on it trains and skills up on its own. "Once" is decided from
    // the character itself - a bot that already has a primary profession is left
    // alone - because the facade values live in memory only and would not survive a
    // restart (which would re-roll professions every time).
    if (record.random && !HasPrimaryProfession(player))
    {
        PlayerbotFactory skills(player, player->GetLevel());
        skills.InitAllSkills();
        TB_LOG_DETAIL("TortoiseBots: bot %s received its level-bound skills and professions.", player->GetName());
    }

    // One-shot random scatter on headless login only; fail-closed, no DB mutation, no homebind.
    // Must run AFTER gear seeding and skills so the bot is fully initialized before any map transfer.
    TryRandomTeleport(player, record);

    TB_LOG_DETAIL("TortoiseBots: bot %s entered world through native PlayerScript", player->GetName());
}

void BotManager::OnPlayerBeforeLogout(::Player* player)
{
    if (!player)
        return;

    ::WorldSession* session = player->GetSession();
    if (!session || !session->IsHeadless())
        DetachOwnedBots(player);

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end())
        return;

    if (it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();

    if (it->second.record.lifecycle != BotLifecycle::Removing)
        it->second.record.lifecycle = BotLifecycle::Removing;
    it->second.record.syncedInWorld = false;
    sRandomBotFacade.MarkNativePlayersDirty();
}

void BotManager::OnPlayerLogout(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it != m_bots.end() && it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();
    if (it != m_bots.end())
        it->second.record.syncedInWorld = false;
    sRandomBotFacade.MarkNativePlayersDirty();
}

void BotManager::DetachOwnedBots(::Player* master)
{
    if (!master)
        return;

    ObjectGuid masterGuid = master->GetObjectGuid();
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.masterGuid != masterGuid)
            continue;

        ::Player* bot = sObjectAccessor.FindPlayer(entry.record.characterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
            continue;

        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
            entry.aiAdapter->DetachMaster();
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
            sLog.outError("TortoiseBots: cannot detach master for %s because the module AI adapter is unavailable",
                bot->GetName());

        TB_LOG_DEBUG("TortoiseBots: detached live master pointer %s from bot %s; ownership GUID retained",
            master->GetName(), bot->GetName());
    }
}

void BotManager::RebindOwnedBots(::Player* master)
{
    if (!master || !master->GetSession() || !master->GetSession()->HasNetworkTransport())
        return;

    ObjectGuid masterGuid = master->GetObjectGuid();
    uint32 masterAccountId = master->GetSession()->GetAccountId();
    Group* masterGroup = master->GetGroup();

    if (masterGroup && !masterGroup->IsLeader(masterGuid))
    {
        // If the current leader is a headless bot, transfer leadership to the live human player.
        Player* currentLeader = sObjectMgr.GetPlayer(masterGroup->GetLeaderGuid());
        if (currentLeader && currentLeader->GetSession() && currentLeader->GetSession()->IsHeadless())
        {
            masterGroup->ChangeLeader(masterGuid);
        }
    }

    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.lifecycle != BotLifecycle::InWorld)
            continue;

        bool shouldRebind = (entry.record.masterGuid == masterGuid);
        if (!shouldRebind && masterGroup && masterGroup->IsMember(entry.record.characterGuid))
        {
            uint32 botOwner = entry.record.ownerAccountId ? entry.record.ownerAccountId : entry.record.accountId;
            if (botOwner == masterAccountId || master->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
                shouldRebind = true;
        }

        if (!shouldRebind)
            continue;

        ::Player* bot = sObjectAccessor.FindPlayer(entry.record.characterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
            continue;

        entry.record.masterGuid = masterGuid;
        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
            entry.aiAdapter->RebindMaster(master);
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
            sLog.outError("TortoiseBots: cannot rebind master for %s because the module AI adapter is unavailable",
                bot->GetName());
        BotActivityLeaseManager::Instance().ClaimForMaster(entry.record.characterGuid.GetCounter());

        TB_LOG_DETAIL("TortoiseBots: rebound master %s to existing Headless bot %s; mature movement preserved",
            master->GetName(), bot->GetName());
    }
}

void BotManager::ReleaseToClient(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end())
        return;

    if (it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();

    TB_LOG_DETAIL("TortoiseBots: releasing module control of %s to a network client", player->GetName());
    uint32_t guidLow = player->GetObjectGuid().GetCounter();
    m_bots.erase(it);
    sRandomBotFacade.MarkNativePlayersDirty();
    // Human reclaim owns the character now: evict any background lease with
    // active cleanup, then drop the master lock so no ghost lease lingers.
    BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
    BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
}

bool BotManager::RunPendingAddRemoveTest(uint32_t accountId, ::ObjectGuid guid)
{
    HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(guid);
    if (st != HeadlessSessionState::NotFound ||
        sObjectAccessor.FindPlayer(guid) || FindBot(guid))
    {
        sLog.outError("TortoiseBots: PendingAddRemoveTest precondition failed for acct %u guid %s",
            accountId, guid.GetString().c_str());
        return false;
    }

    bool queued = AddBot(accountId, guid);
    bool removed = RemoveBot(guid, false);
    bool noSession = BotSessionAdapter::GetHeadlessSessionState(guid) == HeadlessSessionState::NotFound;
    bool noPlayer = !sObjectAccessor.FindPlayer(guid);
    bool noRecord = !FindBot(guid);

    bool passed = queued && removed && noSession && noPlayer && noRecord;
    TB_LOG_BASIC("TortoiseBots: PendingAddRemoveTest %s acct %u queued %u session %u player %u record %u",
        passed ? "PASSED" : "FAILED", accountId, queued, !noSession, !noPlayer, !noRecord);
    return passed;
}

bool BotManager::AddBot(uint32_t accountId, ::ObjectGuid guid, ::ObjectGuid masterGuid)
{
    return AddBotWithMaster(accountId, guid, masterGuid);
}

bool BotManager::AddRandomBot(uint32_t accountId, ::ObjectGuid guid)
{
    bool ok = AddBotWithMaster(accountId, guid, ::ObjectGuid());
    if (ok)
        if (BotRecord* record = FindBot(guid))
            record->random = true;
    return ok;
}

bool BotManager::AddBotWithMaster(uint32_t accountId, ::ObjectGuid guid, ::ObjectGuid masterGuid)
{
    uint32_t key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it != m_bots.end())
    {
        TB_LOG_DETAIL("TortoiseBots: AddBot guid %s already tracked (state %u enteredWorld %u)",
            guid.GetString().c_str(), static_cast<uint32_t>(it->second.record.lifecycle), it->second.record.enteredWorld);
        return false;
    }

    HeadlessSessionState state = BotSessionAdapter::GetHeadlessSessionState(guid);
    if (sObjectAccessor.FindPlayer(guid) ||
        state != HeadlessSessionState::NotFound)
    {
        sLog.outError("TortoiseBots: AddBot guid %s rejected because the character already has a live or pending session (state %u)",
            guid.GetString().c_str(), static_cast<uint32>(state));
        return false;
    }

    if (!BotSessionAdapter::StartHeadlessSession(accountId, guid))
        return false;

    BotEntry entry;
    entry.record.accountId = accountId;
    entry.record.characterGuid = guid;
    entry.record.masterGuid = masterGuid;
    entry.record.lifecycle = BotLifecycle::PendingAdd;
    m_bots.emplace(key, std::move(entry));
    sRandomBotFacade.MarkNativePlayersDirty();
    if (!masterGuid.IsEmpty())
        BotActivityLeaseManager::Instance().ClaimForMaster(key);
    TB_LOG_DETAIL("TortoiseBots: AddBot %s on acct %u master %s (PendingAdd, StartHeadlessSession)",
        guid.GetString().c_str(), accountId, masterGuid.GetString().c_str());
    return true;
}

bool BotManager::RegisterOwnedCharacter(uint32_t ownerAccountId, uint32_t characterAccountId,
    ::ObjectGuid characterGuid, ::ObjectGuid masterGuid)
{
    if (!ownerAccountId || characterGuid.IsEmpty())
        return false;

    bool stored = CharacterDatabase.PExecute(
        "REPLACE INTO `tortoise_bots_owned_character` "
        "(`character_guid`, `owner_account_id`, `character_account_id`, `master_guid`) "
        "VALUES ('%u', '%u', '%u', '%u')",
        characterGuid.GetCounter(), ownerAccountId, characterAccountId,
        masterGuid.IsEmpty() ? 0 : masterGuid.GetCounter());
    if (!stored)
    {
        sLog.outError("TortoiseBots: could not persist ownership for character %s (owner account %u)",
            characterGuid.GetString().c_str(), ownerAccountId);
        return false;
    }

    if (BotRecord* record = FindBot(characterGuid))
        record->ownerAccountId = ownerAccountId;
    return true;
}

bool BotManager::GetOwnedCharacter(::ObjectGuid characterGuid, OwnedCharacter& result)
{
    std::unique_ptr<QueryResult> query(CharacterDatabase.PQuery(
        "SELECT `owner_account_id`, `character_account_id`, `character_guid`, `master_guid` "
        "FROM `tortoise_bots_owned_character` WHERE `character_guid` = '%u' LIMIT 1",
        characterGuid.GetCounter()));
    if (!query)
        return false;

    Field* fields = query->Fetch();
    result.ownerAccountId = fields[0].GetUInt32();
    result.characterAccountId = fields[1].GetUInt32();
    result.characterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[2].GetUInt32());
    result.masterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[3].GetUInt32());
    return true;
}

std::vector<OwnedCharacter> BotManager::GetOwnedCharacters(uint32_t ownerAccountId)
{
    std::vector<OwnedCharacter> result;
    if (!ownerAccountId)
        return result;

    // Same-account characters are owned candidates even before their first
    // Headless login; explicit rows extend the roster to GM-owned accounts.
    std::unique_ptr<QueryResult> query(CharacterDatabase.PQuery(
        "SELECT COALESCE(o.`owner_account_id`, c.`account`), c.`account`, c.`guid`, "
        "COALESCE(o.`master_guid`, 0), c.`name`, c.`class`, c.`online`, c.`map`, "
        "c.`zone`, c.`position_x`, c.`position_y`, c.`position_z` "
        "FROM `characters` c "
        "LEFT JOIN `tortoise_bots_owned_character` o "
        "ON o.`character_guid` = c.`guid` AND o.`owner_account_id` = '%u' "
        "WHERE c.`deleteDate` IS NULL "
        "AND (c.`account` = '%u' OR o.`character_guid` IS NOT NULL) "
        "ORDER BY c.`name`, c.`guid`",
        ownerAccountId, ownerAccountId));
    if (!query)
        return result;

    do
    {
        Field* fields = query->Fetch();
        OwnedCharacter row;
        row.ownerAccountId = fields[0].GetUInt32();
        row.characterAccountId = fields[1].GetUInt32();
        row.characterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[2].GetUInt32());
        row.masterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[3].GetUInt32());
        row.name = fields[4].GetString();
        row.classId = static_cast<uint8_t>(fields[5].GetUInt32());
        row.characterOnline = fields[6].GetUInt32() != 0;
        row.mapId = fields[7].GetUInt32();
        row.zoneId = fields[8].GetUInt32();
        row.positionX = fields[9].GetFloat();
        row.positionY = fields[10].GetFloat();
        row.positionZ = fields[11].GetFloat();
        result.push_back(std::move(row));
    } while (query->NextRow());
    return result;
}

bool BotManager::RemoveBot(::ObjectGuid guid, bool save)
{
    uint32_t key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it == m_bots.end())
        return false;

    BotRecord& rec = it->second.record;
    // Reentrant removal from inside an AI update (UpdateBots sets
    // m_inBotUpdate): stopping the session now would run the logout hooks
    // synchronously, delete the updating PlayerbotAI out from under its own
    // UpdateAI stack, and erase this entry. Mark Removing, queue the stop for
    // the post-update drain, and return while every object is still alive.
    if (m_inBotUpdate)
    {
        rec.lifecycle = BotLifecycle::Removing;
        sRandomBotFacade.MarkNativePlayersDirty();
        BotActivityLeaseManager::Instance().Release(key, BotActivity::Grinding);
        bool queued = false;
        for (auto const& pending : m_pendingBotRemovals)
            if (pending.characterGuid.GetCounter() == key)
            {
                queued = true;
                break;
            }
        if (!queued)
        {
            PendingBotRemoval pending;
            pending.characterGuid = guid;
            pending.save = save;
            m_pendingBotRemovals.push_back(pending);
        }
        TB_LOG_DETAIL("TortoiseBots: RemoveBot %s deferred until AI update completes (Removing)",
            guid.GetString().c_str());
        return true;
    }

    rec.lifecycle = BotLifecycle::Removing;
    sRandomBotFacade.MarkNativePlayersDirty();
    // Grinding is indefinite with no timeout: clear it on logout so no ghost
    // lease lingers. Other activities are service-owned (LFT/BG/Trading via
    // Reconcile/Prune, PlayerMaster via Clear/ReleaseToClient).
    BotActivityLeaseManager::Instance().Release(key, BotActivity::Grinding);

    // Request core to stop the headless session while record remains so
    // PlayerScript logout hooks can shut down AI.
    BotSessionAdapter::StopHeadlessSession(guid, save);

    // If core already reports NotFound, erase immediately.
    if (BotSessionAdapter::GetHeadlessSessionState(guid) == HeadlessSessionState::NotFound)
    {
        // Check human reclaim: if player now has Network transport, core owns transfer.
        if (::Player* p = sObjectAccessor.FindPlayer(guid))
        {
            ::WorldSession* s = p->GetSession();
            if (s && s->HasNetworkTransport())
                TB_LOG_DETAIL("TortoiseBots: RemoveBot %s reclaimed by network — releasing", guid.GetString().c_str());
        }
        m_bots.erase(it);
        sRandomBotFacade.MarkNativePlayersDirty();
        TB_LOG_DETAIL("TortoiseBots: RemoveBot %s immediate NotFound — erased", guid.GetString().c_str());
        return true;
    }

    TB_LOG_DETAIL("TortoiseBots: RemoveBot %s (Removing; erasure deferred until NotFound)", guid.GetString().c_str());
    return true;
}

BotRecord* BotManager::FindBot(::ObjectGuid guid)
{
    auto it = m_bots.find(guid.GetCounter());
    if (it == m_bots.end())
        return nullptr;
    return &it->second.record;
}

bool BotManager::IsBot(::ObjectGuid guid) const
{
    return m_bots.find(guid.GetCounter()) != m_bots.end();
}

bool BotManager::IsRandomBot(::ObjectGuid guid) const
{
    auto it = m_bots.find(guid.GetCounter());
    return it != m_bots.end() && it->second.record.random;
}

std::vector<::Player*> BotManager::GetBotsForMaster(::ObjectGuid masterGuid) const
{
    std::vector<::Player*> result;
    for (auto const& entry : m_bots)
    {
        if (entry.second.record.masterGuid != masterGuid)
            continue;

        ::Player* player = sObjectAccessor.FindPlayer(entry.second.record.characterGuid);
        if (IsLiveHeadlessBot(entry.second, player))
            result.push_back(player);
    }
    return result;
}

std::vector<::Player*> BotManager::GetAllBots() const
{
    std::vector<::Player*> result;
    result.reserve(m_bots.size());
    for (auto const& entry : m_bots)
    {
        ::Player* player = sObjectAccessor.FindPlayer(entry.second.record.characterGuid);
        if (IsLiveHeadlessBot(entry.second, player))
            result.push_back(player);
    }
    return result;
}

bool BotManager::IsLiveHeadlessBot(BotEntry const& entry, ::Player* player) const
{
    if (!player || entry.record.lifecycle != BotLifecycle::InWorld || !player->IsInWorld())
        return false;

    ::WorldSession* session = player->GetSession();
    if (!session || !session->IsHeadless() || session->HasNetworkTransport())
        return false;
    if (!entry.aiAdapter || !entry.aiAdapter->IsUsable())
        return false;

    // Core owns Headless session; module bookkeeping is via BotRecord + AI.
    HeadlessSessionState state = BotSessionAdapter::GetHeadlessSessionState(entry.record.characterGuid);
    return state == HeadlessSessionState::Active || state == HeadlessSessionState::Loading;
}

bool BotManager::IsControllableBot(::Player* player) const
{
    if (!player)
        return false;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end() || !IsLiveHeadlessBot(it->second, player))
        return false;

    return BotSessionAdapter::GetHeadlessSessionState(it->second.record.characterGuid) ==
        HeadlessSessionState::Active;
}

bool BotManager::BindBotMaster(::ObjectGuid botGuid, ::ObjectGuid masterGuid)
{
    if (masterGuid.IsEmpty() || botGuid == masterGuid)
        return false;

    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* master = sObjectAccessor.FindPlayer(masterGuid);
    auto masterEntry = m_bots.find(masterGuid.GetCounter());
    bool moduleHeadlessMaster = master && masterEntry != m_bots.end() &&
        IsLiveHeadlessBot(masterEntry->second, master);
    if (!master || !master->IsInWorld() || !master->GetSession() ||
        (master->GetSession()->IsHeadless() && !moduleHeadlessMaster))
    {
        sLog.outError("TortoiseBots: cannot bind bot %s to missing or unsupported master %s",
            botGuid.GetString().c_str(), masterGuid.GetString().c_str());
        return false;
    }

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = nullptr;
    if (bot)
    {
        if (!IsLiveHeadlessBot(it->second, bot))
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because its live session is not module-owned Headless",
                botGuid.GetString().c_str());
            return false;
        }

        ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because mature PlayerbotAI is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
        if (!it->second.aiAdapter || !it->second.aiAdapter->IsInitialized())
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because its module AI adapter is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
    }

    it->second.record.masterGuid = masterGuid;
    if (ai)
        it->second.aiAdapter->RebindMaster(master);
    // Human claim wins (issue #89): evict any background lease with active
    // cleanup, then lock to PlayerMaster. Non-binding whispers never reach
    // here; only master-binding paths (.bot add, invite, follow) do.
    BotActivityLeaseManager::Instance().ClaimForMaster(botGuid.GetCounter());

    return true;
}

bool BotManager::ClearBotMaster(::ObjectGuid botGuid)
{
    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = nullptr;
    if (bot)
    {
        if (!IsLiveHeadlessBot(it->second, bot))
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because its live session is not module-owned Headless",
                botGuid.GetString().c_str());
            return false;
        }

        ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because mature PlayerbotAI is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
        if (!it->second.aiAdapter || !it->second.aiAdapter->IsInitialized())
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because its module AI adapter is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
    }

    it->second.record.masterGuid = ObjectGuid();
    if (ai)
        it->second.aiAdapter->DetachMaster();
    BotActivityLeaseManager::Instance().ReleaseMaster(botGuid.GetCounter());

    return true;
}

bool BotManager::SetBotFollow(::ObjectGuid botGuid, ::ObjectGuid masterGuid)
{
    if (!BindBotMaster(botGuid, masterGuid))
        return false;

    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!bot || !IsLiveHeadlessBot(it->second, bot) || !ai)
    {
        sLog.outError("TortoiseBots: SetBotFollow bot %s rejected because Headless bot or mature AI is unavailable",
            botGuid.GetString().c_str());
        return false;
    }

    ::Player* master = sObjectAccessor.FindPlayer(masterGuid);
    ai::Event followEvent("follow", "", master);
    if (!ai->DoSpecificAction("follow chat shortcut", followEvent, true))
    {
        sLog.outError("TortoiseBots: mature follow action failed for bot %s",
            botGuid.GetString().c_str());
        return false;
    }

    TB_LOG_DETAIL("TortoiseBots: SetBotFollow bot %s -> master %s",
        botGuid.GetString().c_str(), masterGuid.GetString().c_str());
    return true;
}

void BotManager::SetAutoTestEnabled(bool enable, uint32_t accountId, ::ObjectGuid guid)
{
    m_autoTestEnabled = enable;
    m_autoTestAccount = accountId;
    m_autoTestGuid = guid;
    m_autoTestTicks = 0;
    m_autoState = AutoState::Idle;
    m_autoTestPassed = false;
    TB_LOG_BASIC("TortoiseBots: AutoTest %s acct %u guid %s",
        enable ? "enabled" : "disabled", accountId, guid.GetString().c_str());
}

void BotManager::SetPacketBridgeTestEnabled(bool enable, uint32_t accountId,
    ::ObjectGuid masterGuid, ::ObjectGuid botGuid)
{
    m_packetTestEnabled = enable;
    m_packetTestAccount = accountId;
    m_packetTestMasterGuid = masterGuid;
    m_packetTestBotGuid = botGuid;
    m_packetTestTicks = 0;
    m_packetTestStage = 0;
    TB_LOG_BASIC("TortoiseBots: PacketBridgeTest %s acct %u master %s bot %s",
        enable ? "enabled" : "disabled", accountId,
        masterGuid.GetString().c_str(), botGuid.GetString().c_str());
}

void BotManager::UpdateBots(uint32_t diff)
{
    // Guard: AI updates below can request (their own or another bot's) removal.
    // RemoveBot defers the session stop while this is set; the queue drains
    // after the loop, when no PlayerbotAI Update remains on the stack.
    // RAII so an unexpected unwind still clears the flag; anything queued
    // drains at the end of the next successful pass.
    struct BotUpdateGuard
    {
        BotManager* manager;
        explicit BotUpdateGuard(BotManager* m) : manager(m) { manager->m_inBotUpdate = true; }
        ~BotUpdateGuard() { manager->m_inBotUpdate = false; }
    };
    BotUpdateGuard botUpdateGuard(this);

    std::vector<uint32_t> guids;
    guids.reserve(m_bots.size());
    for (auto const& kv : m_bots)
        guids.push_back(kv.first);

    for (uint32_t guidLow : guids)
    {
        auto it = m_bots.find(guidLow);
        if (it == m_bots.end())
            continue;

        BotEntry& entry = it->second;
        // A removal queued earlier in this same loop only takes effect at the
        // drain below, but the record is already Removing: never drive AI for
        // a bot whose session stop is pending.
        if (entry.record.lifecycle != BotLifecycle::InWorld)
            continue;

        // Headless players have no client to acknowledge a core near/far
        // teleport. The donor PlayerbotMgr drove this acknowledgement from its
        // session loop; this module owns that loop now, so do the same before
        // the normal AI usability gate. FindPlayer() intentionally excludes a
        // far-teleporting player, while the public NotInWorld lookup retains
        // the object long enough for HandleTeleportAck() to finish the move.
        ::Player* player = sObjectAccessor.FindPlayerNotInWorld(entry.record.characterGuid);
        if (player && player->GetSession() && player->GetSession()->IsHeadless() &&
            player->IsBeingTeleported() && entry.aiAdapter && entry.aiAdapter->IsInitialized())
        {
            // A far teleport can spend one tick in the core's pending queue
            // before ExecuteTeleportFar raises the ACK semaphore. Keep the
            // AI paused for that tick, but only acknowledge an actual near/far
            // transfer; acknowledging the pending marker would reset state
            // before the destination has been installed.
            PlayerbotAI* ai = entry.aiAdapter->GetAI();
            if ((player->IsBeingTeleportedNear() || player->IsBeingTeleportedFar()) && ai)
                ai->HandleTeleportAck();
            continue;
        }

        if (entry.aiAdapter && entry.aiAdapter->IsUsable())
        {
            entry.aiAdapter->Update(diff);
        }
    }

    m_inBotUpdate = false;

    if (!m_pendingBotRemovals.empty())
    {
        std::vector<PendingBotRemoval> pending;
        pending.swap(m_pendingBotRemovals);
        for (auto const& removal : pending)
            RemoveBot(removal.characterGuid, removal.save);
    }
}

void BotManager::OnWorldUpdate(uint32_t diff)
{
    // Core owns Headless session lifecycle (queue, LoginPlayer dispatch, reclaim).
    // Module only tracks BotRecord and polls adapter state. No pending promotion.
    for (auto it = m_bots.begin(); it != m_bots.end(); )
    {
        BotEntry& entry = it->second;
        BotRecord& rec = entry.record;
        ::Player* p = sObjectAccessor.FindPlayer(rec.characterGuid);
        HeadlessSessionState state = BotSessionAdapter::GetHeadlessSessionState(rec.characterGuid);

        if (rec.lifecycle == BotLifecycle::Removing)
        {
            // Ensure Stop was requested; if player was reclaimed by Network, core owns transfer.
            if (p && p->GetSession() && p->GetSession()->HasNetworkTransport())
            {
                TB_LOG_DETAIL("TortoiseBots: Bot %s reclaimed by network during removal — releasing",
                    rec.characterGuid.GetString().c_str());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                sRandomBotFacade.MarkNativePlayersDirty();
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            if (state == HeadlessSessionState::NotFound)
            {
                TB_LOG_DETAIL("TortoiseBots: Bot %s removal complete (NotFound)", rec.characterGuid.GetString().c_str());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                sRandomBotFacade.MarkNativePlayersDirty();
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            // Still pending/loading/active → keep record until NotFound.
            ++it;
            continue;
        }

        // Human reclaim detection via Network transport.
        if (p)
        {
            ::WorldSession* playerSess = p->GetSession();
            if (playerSess && playerSess->HasNetworkTransport())
            {
                TB_LOG_DETAIL("TortoiseBots: Bot %s reclaimed by network session acct %u — releasing",
                    rec.characterGuid.GetString().c_str(), playerSess->GetAccountId());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                sRandomBotFacade.MarkNativePlayersDirty();
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            if (p->IsInWorld())
            {
                if (!rec.enteredWorld)
                    OnPlayerLogin(p);

                // Map transfer toggles IsInWorld; mark dirty so the view
                // resyncs on the next tick instead of waiting for the 5s
                // fallback. (OnPlayerLogin already marks dirty on first entry.)
                if (!rec.syncedInWorld)
                {
                    rec.syncedInWorld = true;
                    sRandomBotFacade.MarkNativePlayersDirty();
                }

                // OnPlayerLogin owns the only promotion to InWorld. In
                // particular, do not overwrite Removing after an AI attach
                // failure just because the Player object is still present.
                if (rec.lifecycle == BotLifecycle::Removing)
                {
                    ++it;
                    continue;
                }

                if (rec.lifecycle != BotLifecycle::InWorld ||
                    !entry.aiAdapter || !entry.aiAdapter->IsUsable())
                {
                    sLog.outError("TortoiseBots: Bot %s lost usable PlayerbotAI; stopping the Headless session",
                        rec.characterGuid.GetString().c_str());
                    rec.enteredWorld = false;
                    rec.lifecycle = BotLifecycle::Removing;
                    rec.syncedInWorld = false;
                    sRandomBotFacade.MarkNativePlayersDirty();
                    BotSessionAdapter::StopHeadlessSession(rec.characterGuid, true);
                    ++it;
                    continue;
                }

                rec.enteredWorld = true;
                ++rec.ticksInWorld;
            }
            ++it;
            continue;
        }

        // No Player object; check core state.
        if (rec.lifecycle == BotLifecycle::PendingAdd)
        {
            if (state == HeadlessSessionState::NotFound)
            {
                sLog.outError("TortoiseBots: Bot %s login ended without a session (NotFound)",
                    rec.characterGuid.GetString().c_str());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                sRandomBotFacade.MarkNativePlayersDirty();
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            // Still Pending/Loading → keep waiting; core will drive to Active.
            ++it;
            continue;
        }

        if (state == HeadlessSessionState::NotFound)
        {
            TB_LOG_DETAIL("TortoiseBots: Bot %s session ended (NotFound) — releasing",
                rec.characterGuid.GetString().c_str());
            uint32_t guidLow = rec.characterGuid.GetCounter();
            it = m_bots.erase(it);
            sRandomBotFacade.MarkNativePlayersDirty();
            BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
            BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
            continue;
        }
        if (rec.syncedInWorld)
        {
            rec.syncedInWorld = false;
            sRandomBotFacade.MarkNativePlayersDirty();
        }
        ++it;
    }

    // Mature PlayerbotAI still exposes a donor-shaped population view for
    // perception/social queries. Refresh it from the authoritative native
    // records immediately before any AI update; it never owns sessions.
    PlayerbotAI::ProcessDelayedPackets();
    sRandomBotFacade.SyncNativePlayers();
    UpdateBots(diff);
    SweepStrandedBots(diff);

    if (m_autoTestEnabled)
        UpdateAutoTest(diff);

    if (m_packetTestEnabled)
        UpdatePacketBridgeTest(diff);
}

void BotManager::UpdatePacketBridgeTest(uint32_t diff)
{
    (void)diff;
    ++m_packetTestTicks;

    if (!m_packetTestAccount || m_packetTestMasterGuid.IsEmpty() ||
        m_packetTestBotGuid.IsEmpty() || m_packetTestMasterGuid == m_packetTestBotGuid)
    {
        sLog.outError("TortoiseBots: PacketBridgeTest invalid fixture");
        m_packetTestEnabled = false;
        return;
    }

    if (m_packetTestStage == 0)
    {
        if (!FindBot(m_packetTestMasterGuid) && !FindBot(m_packetTestBotGuid))
        {
            AddBot(m_packetTestAccount, m_packetTestMasterGuid);
            AddBotWithMaster(m_packetTestAccount, m_packetTestBotGuid, m_packetTestMasterGuid);
            m_packetTestStage = 1;
            m_packetTestTicks = 0;
        }
        else
        {
            sLog.outError("TortoiseBots: PacketBridgeTest fixture is already active");
            m_packetTestEnabled = false;
        }
        return;
    }

    Player* master = sObjectAccessor.FindPlayer(m_packetTestMasterGuid);
    Player* bot = sObjectAccessor.FindPlayer(m_packetTestBotGuid);
    if (m_packetTestStage == 1)
    {
        if (master && bot && master->IsInWorld() && bot->IsInWorld())
        {
            // Recreate the GM-invisible state a GM account would restore on a
            // Headless character, then require the bot-login normalization to
            // remove every GM-facing flag before command processing continues.
            bot->SetGMVisible(false);
            uint32 const botGmFlags = PLAYER_EXTRA_GM_ON |
                PLAYER_EXTRA_GM_ACCEPT_TICKETS | PLAYER_EXTRA_GM_INVISIBLE |
                PLAYER_EXTRA_GM_CHAT | PLAYER_EXTRA_GM_DISABLE_SOCIAL;
            bool headlessPresentationPassed = NormalizeHeadlessGmPresentation(bot) &&
                bot->IsGMVisible() && !bot->IsGameMaster() &&
                !(bot->GetExtraFlags() & botGmFlags);
            if (!headlessPresentationPassed)
            {
                sLog.outError("TortoiseBots: PacketBridgeTest headless GM presentation FAILED");
                m_packetTestStage = 3;
                m_packetTestTicks = 0;
                return;
            }

            WorldSession* originalSession = master->GetSession();
            WorldSession* syntheticNetwork = new WorldSession(
                m_packetTestAccount, nullptr, sAccountMgr.GetSecurity(m_packetTestAccount),
                time_t(0), LOCALE_enUS, "packet-test", 0, SessionTransport::Network);
            syntheticNetwork->SetPlayer(master);
            master->SetSession(syntheticNetwork);

            ChatHandler commandHandler(syntheticNetwork);
            std::string followCommand = "follow " + std::string(bot->GetName());
            std::string inviteCommand = "invite " + std::string(bot->GetName());
            bool commandSurfacePassed =
                BotCommands::HandleChatCommand(&commandHandler, "list") &&
                BotCommands::HandleChatCommand(&commandHandler, "stats") &&
                BotCommands::HandleChatCommand(&commandHandler, followCommand.c_str()) &&
                PlayerbotAIStorage::Instance().GetAI(bot) &&
                PlayerbotAIStorage::Instance().GetAI(bot)->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT);

            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest native command surface %s — list/stats/follow dispatched through ChatHandler",
                commandSurfacePassed ? "PASSED" : "FAILED");

            bool immediateInvitePassed =
                BotCommands::HandleChatCommand(&commandHandler, inviteCommand.c_str()) &&
                master->GetGroup() && bot->GetGroup() == master->GetGroup() &&
                master->GetGroup()->IsMember(bot->GetObjectGuid());

            master->SetSession(originalSession);
            syntheticNetwork->SetPlayer(nullptr);
            delete syntheticNetwork;

            if (!immediateInvitePassed)
            {
                sLog.outError("TortoiseBots: PacketBridgeTest immediate native invite FAILED");
                m_packetTestStage = 3;
                m_packetTestTicks = 0;
                return;
            }

            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest immediate native invite PASSED");
            m_packetTestStage = 2;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 600)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest login timeout");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 2)
    {
        if (master && bot && master->GetGroup() && bot->GetGroup() == master->GetGroup() &&
            master->GetGroup()->IsMember(bot->GetObjectGuid()))
        {
            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest group invite/accept PASSED — mature PlayerbotAI joined group");

            // Exercise the native uninvite handler for cleanup. Incoming packet
            // delivery is intentionally not injected here: the strict runtime
            // proof must come from a real Network session through Penqle's
            // ServerScript::CanPacketReceive hook.
            WorldSession* originalSession = master->GetSession();
            WorldSession* syntheticNetwork = new WorldSession(
                m_packetTestAccount, nullptr, sAccountMgr.GetSecurity(m_packetTestAccount),
                time_t(0), LOCALE_enUS, "packet-test", 0, SessionTransport::Network);
            syntheticNetwork->SetPlayer(master);
            master->SetSession(syntheticNetwork);

            WorldPacket uninvite(CMSG_GROUP_UNINVITE);
            uninvite << bot->GetName();
            syntheticNetwork->HandleGroupUninviteOpcode(uninvite);

            master->SetSession(originalSession);
            syntheticNetwork->SetPlayer(nullptr);
            delete syntheticNetwork;

            Map* botMap = bot->GetMap();
            if (!botMap)
            {
                sLog.outError("TortoiseBots: PacketBridgeTest missing bot map before stranded-session check");
                m_packetTestStage = 3;
                m_packetTestTicks = 0;
                return;
            }
            botMap->Remove(bot, false);
            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest forced an out-of-world Headless bot");
            m_packetTestStage = 4;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest group invite/accept FAILED");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 4)
    {
        bool released = BotSessionAdapter::GetHeadlessSessionState(m_packetTestBotGuid) ==
            HeadlessSessionState::NotFound;
        bool materialized = sObjectAccessor.FindPlayerNotInWorld(m_packetTestBotGuid) != nullptr;
        if (released && !materialized)
        {
            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest stranded Headless recovery PASSED");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest stranded Headless recovery FAILED");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 3)
    {
        if (FindBot(m_packetTestBotGuid))
            RemoveBot(m_packetTestBotGuid, true);
        if (FindBot(m_packetTestMasterGuid))
            RemoveBot(m_packetTestMasterGuid, true);

        if (!FindBot(m_packetTestBotGuid) && !FindBot(m_packetTestMasterGuid) &&
            BotSessionAdapter::GetHeadlessSessionState(m_packetTestBotGuid) == HeadlessSessionState::NotFound &&
            BotSessionAdapter::GetHeadlessSessionState(m_packetTestMasterGuid) == HeadlessSessionState::NotFound)
        {
            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest cleanup PASSED");
            m_packetTestEnabled = false;
        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest cleanup timed out");
            m_packetTestEnabled = false;
        }
    }
}

void BotManager::FinishAutoTest(bool passed)
{
    m_autoTestPassed = passed;
    if (FindBot(m_autoTestGuid))
        RemoveBot(m_autoTestGuid, true);
    m_autoState = AutoState::CleaningUp;
    m_autoTestTicks = 0;
}

void BotManager::UpdateAutoTest(uint32_t diff)
{
    (void)diff;
    ++m_autoTestTicks;

    switch (m_autoState)
    {
        case AutoState::Idle:
            if (m_autoTestTicks > 20)
            {
                TB_LOG_BASIC("TortoiseBots: AutoTest step 1 — login bot %s", m_autoTestGuid.GetString().c_str());
                if (AddBot(m_autoTestAccount, m_autoTestGuid))
                {
                    m_autoState = AutoState::LoggingIn;
                    m_autoTestTicks = 0;
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest could not queue its fixture");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::LoggingIn:
            if (BotRecord* rec = FindBot(m_autoTestGuid))
            {
                if (rec->enteredWorld)
                {
                    TB_LOG_BASIC("TortoiseBots: AutoTest step 2 — bot entered world, tick %u", rec->ticksInWorld);
                    m_autoState = AutoState::InWorld;
                    m_autoTestTicks = 0;
                }
                else
                {
                    if (m_autoTestTicks % 40 == 0)
                    {
                        ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(rec->characterGuid);
                        std::string sessInfo = (st != HeadlessSessionState::NotFound) ? "headless" : "<null>";
                        bool loading = (st == HeadlessSessionState::Loading || st == HeadlessSessionState::Pending);
                        std::string playerInfo = p ? (p->IsInWorld() ? "IsInWorld" : "not InWorld") : "FindPlayer null";
                        TB_LOG_DEBUG("TortoiseBots: AutoTest LoggingIn tick %u sess %s state %u acct %u loading %u player %s pending %u", m_autoTestTicks, sessInfo.c_str(), static_cast<uint32>(st), rec->accountId, loading, playerInfo.c_str(), st == HeadlessSessionState::Pending);
                        if (st != HeadlessSessionState::NotFound && p && p->GetSession())
                            TB_LOG_DEBUG("TortoiseBots:   sess details network %u headless %u", p->GetSession()->HasNetworkTransport(), p->GetSession()->IsHeadless());
                    }
                    if (m_autoTestTicks > 400)
                    {
                        ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(rec->characterGuid);
                        sLog.outError("TortoiseBots: AutoTest login timeout after %u ticks (state %u player %s)", m_autoTestTicks, static_cast<uint32>(st), p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                        FinishAutoTest(false);
                    }
                }
            }
            else
            {
                sLog.outError("TortoiseBots: AutoTest LoggingIn but FindBot null tick %u", m_autoTestTicks);
                if (m_autoTestTicks > 400)
                    FinishAutoTest(false);
            }
            break;
        case AutoState::InWorld:
            if (m_autoTestTicks > 600)
            {
                if (BotRecord* rec = FindBot(m_autoTestGuid))
                {
                    ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    if (p)
                    {
                        p->SaveToDB(false, false);
                        TB_LOG_BASIC("TortoiseBots: AutoTest step 3 — saved bot %s", p->GetName());
                    }
                    else
                    {
                        sLog.outError("TortoiseBots: AutoTest expected an in-world fixture before save");
                        FinishAutoTest(false);
                        break;
                    }
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest lost its fixture before save");
                    FinishAutoTest(false);
                    break;
                }
                m_autoState = AutoState::Saving;
                m_autoTestTicks = 0;
            }
            break;
        case AutoState::Saving:
            if (m_autoTestTicks > 20)
            {
                TB_LOG_BASIC("TortoiseBots: AutoTest step 4 — logout bot");
                if (RemoveBot(m_autoTestGuid, true))
                {
                    m_autoState = AutoState::LoggingOut;
                    m_autoTestTicks = 0;
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest could not request fixture logout");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::LoggingOut:
            if (m_autoTestTicks > 40)
            {
                if (!sObjectAccessor.FindPlayer(m_autoTestGuid) &&
                    !FindBot(m_autoTestGuid) &&
                    BotSessionAdapter::GetHeadlessSessionState(m_autoTestGuid) == HeadlessSessionState::NotFound)
                {
                    TB_LOG_BASIC("TortoiseBots: AutoTest step 5 — re-login bot");
                    if (AddBot(m_autoTestAccount, m_autoTestGuid))
                    {
                        m_autoState = AutoState::Relogging;
                        m_autoTestTicks = 0;
                    }
                    else
                    {
                        sLog.outError("TortoiseBots: AutoTest could not queue fixture relogin");
                        FinishAutoTest(false);
                    }
                }
                else if (m_autoTestTicks > 400)
                {
                    sLog.outError("TortoiseBots: AutoTest logout timeout");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::Relogging:
            if (BotRecord* rec = FindBot(m_autoTestGuid))
            {
                if (rec->enteredWorld)
                {
                    TB_LOG_BASIC("TortoiseBots: AutoTest step 6 — bot re-entered world, lifecycle PASSED; cleaning up");
                    FinishAutoTest(true);
                }
                else if (m_autoTestTicks > 400)
                {
                    sLog.outError("TortoiseBots: AutoTest relog timeout");
                    FinishAutoTest(false);
                }
                else if (m_autoTestTicks % 40 == 0)
                {
                    ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(rec->characterGuid);
                    TB_LOG_DEBUG("TortoiseBots: AutoTest Relogging tick %u state %u pending %u player %s", m_autoTestTicks, static_cast<uint32>(st), st == HeadlessSessionState::Pending, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                }
            }
            else if (m_autoTestTicks > 400)
            {
                sLog.outError("TortoiseBots: AutoTest relog lost its fixture");
                FinishAutoTest(false);
            }
            break;
        case AutoState::CleaningUp:
            if (!FindBot(m_autoTestGuid) &&
                !sObjectAccessor.FindPlayer(m_autoTestGuid) &&
                BotSessionAdapter::GetHeadlessSessionState(m_autoTestGuid) == HeadlessSessionState::NotFound)
            {
                TB_LOG_BASIC("TortoiseBots: AutoTest cleanup %s; diagnostic disabled",
                    m_autoTestPassed ? "PASSED" : "FAILED");
                m_autoTestEnabled = false;
                m_autoState = AutoState::Done;
            }
            else if (m_autoTestTicks > 400)
            {
                sLog.outError("TortoiseBots: AutoTest cleanup timed out; diagnostic disabled with lifecycle state still active");
                m_autoTestEnabled = false;
                m_autoState = AutoState::Done;
            }
            break;
        case AutoState::Done:
            break;
    }
}

} // namespace TortoiseBots
