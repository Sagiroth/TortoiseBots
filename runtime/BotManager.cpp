#include "BotManager.h"
#include "PlayerbotAIAdapter.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/RandomBotFacade.h"
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

#include <algorithm>

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

bool TryRandomTeleport(::Player* bot, BotRecord const& record)
{
    if (!sPlayerbotAIConfig.enableRandomTeleports)
        return false;
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;
    if (!record.random)
        return false;
    // Match the existing RPG travel safety gate: low-level bots must not be
    // scattered into NPC travel routes before they can survive the journey.
    if (bot->GetLevel() < 5)
        return false;
    if (bot->IsBeingTeleported())
        return false;
    if (!bot->IsInWorld())
        return false;
    if (sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()))
    {
        sLog.outString("TortoiseBots: random teleport skipped pinned bot %s", bot->GetName());
        return false;
    }
    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        sLog.outString("TortoiseBots: random teleport no AI for bot %s, retaining position", bot->GetName());
        return false;
    }
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
        sLog.outString("TortoiseBots: random teleport no GenericRpg destinations for bot %s level %u", bot->GetName(), bot->GetLevel());
        return false;
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
        sLog.outString("TortoiseBots: random teleport no valid overworld point for bot %s level %u", bot->GetName(), bot->GetLevel());
        return false;
    }

    bool ok = bot->TeleportTo(chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ(), bot->GetOrientation(), 0);
    if (ok)
        sLog.outString("TortoiseBots: random teleport bot %s level %u to map %u %.1f %.1f %.1f", bot->GetName(), bot->GetLevel(), chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ());
    else
        sLog.outError("TortoiseBots: random teleport TeleportTo failed for bot %s to map %u %.1f %.1f %.1f, retaining position", bot->GetName(), chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ());
    return ok;
}
} // namespace

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
        entry.aiAdapter->Shutdown();
        BotSessionAdapter::StopHeadlessSession(record.characterGuid, true);
        return;
    }

    record.enteredWorld = true;
    record.lifecycle = BotLifecycle::InWorld;

    // One-shot random scatter on headless login only; fail-closed, no DB mutation, no homebind
    TryRandomTeleport(player, record);

    // Prompt PlayerbotFactory enrichment for random bots where factory supports it.
    // CharacterCreation creates level 1 starter kit; factory's InitEquipment intentionally
    // no-ops for <5. For any auto-created level >=5 (future higher-level pool or manual
    // leveling), trigger gear enrichment immediately on world-thread login
    // rather than waiting ~6h for RandomBotService's randomize interval. Safe and synchronous;
    // the existing randomGearUpgradeEnabled setting controls this (default enabled), no core changes.
    if (record.random && sPlayerbotAIConfig.randomGearUpgradeEnabled && player->GetLevel() >= 5)
        sRandomBotFacade.UpdateGearSpells(player);

    sLog.outString("TortoiseBots: bot %s entered world through native PlayerScript", player->GetName());
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
}

void BotManager::OnPlayerLogout(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it != m_bots.end() && it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();
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

        sLog.outDebug("TortoiseBots: detached live master pointer %s from bot %s; ownership GUID retained",
            master->GetName(), bot->GetName());
    }
}

void BotManager::RebindOwnedBots(::Player* master)
{
    if (!master || !master->GetSession() || !master->GetSession()->HasNetworkTransport())
        return;

    ObjectGuid masterGuid = master->GetObjectGuid();
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.masterGuid != masterGuid ||
            entry.record.lifecycle != BotLifecycle::InWorld)
            continue;

        ::Player* bot = sObjectAccessor.FindPlayer(entry.record.characterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
            continue;

        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
            entry.aiAdapter->RebindMaster(master);
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
            sLog.outError("TortoiseBots: cannot rebind master for %s because the module AI adapter is unavailable",
                bot->GetName());

        sLog.outString("TortoiseBots: rebound master %s to existing Headless bot %s; mature movement preserved",
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

    sLog.outString("TortoiseBots: releasing module control of %s to a network client", player->GetName());
    m_bots.erase(it);
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
    sLog.outString("TortoiseBots: PendingAddRemoveTest %s acct %u queued %u session %u player %u record %u",
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
        sLog.outString("TortoiseBots: AddBot guid %s already tracked (state %u enteredWorld %u)",
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
    sLog.outString("TortoiseBots: AddBot %s on acct %u master %s (PendingAdd, StartHeadlessSession)",
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
    rec.lifecycle = BotLifecycle::Removing;

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
                sLog.outString("TortoiseBots: RemoveBot %s reclaimed by network — releasing", guid.GetString().c_str());
        }
        m_bots.erase(it);
        sLog.outString("TortoiseBots: RemoveBot %s immediate NotFound — erased", guid.GetString().c_str());
        return true;
    }

    sLog.outString("TortoiseBots: RemoveBot %s (Removing; erasure deferred until NotFound)", guid.GetString().c_str());
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
    return state == HeadlessSessionState::Active;
}

bool BotManager::IsControllableBot(::Player* player) const
{
    if (!player)
        return false;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    return it != m_bots.end() && IsLiveHeadlessBot(it->second, player);
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

    sLog.outString("TortoiseBots: SetBotFollow bot %s -> master %s",
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
    sLog.outString("TortoiseBots: AutoTest %s acct %u guid %s",
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
    sLog.outString("TortoiseBots: PacketBridgeTest %s acct %u master %s bot %s",
        enable ? "enabled" : "disabled", accountId,
        masterGuid.GetString().c_str(), botGuid.GetString().c_str());
}

void BotManager::UpdateBots(uint32_t diff)
{
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.lifecycle != BotLifecycle::InWorld)
            continue;
        if (entry.aiAdapter && entry.aiAdapter->IsUsable())
        {
            entry.aiAdapter->Update(diff);
        }
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
                sLog.outString("TortoiseBots: Bot %s reclaimed by network during removal — releasing",
                    rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
                continue;
            }
            if (state == HeadlessSessionState::NotFound)
            {
                sLog.outString("TortoiseBots: Bot %s removal complete (NotFound)", rec.characterGuid.GetString().c_str());
                it = m_bots.erase(it);
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
                sLog.outString("TortoiseBots: Bot %s reclaimed by network session acct %u — releasing",
                    rec.characterGuid.GetString().c_str(), playerSess->GetAccountId());
                it = m_bots.erase(it);
                continue;
            }
            if (p->IsInWorld())
            {
                if (!rec.enteredWorld)
                    OnPlayerLogin(p);

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
                it = m_bots.erase(it);
                continue;
            }
            // Still Pending/Loading → keep waiting; core will drive to Active.
            ++it;
            continue;
        }

        if (state == HeadlessSessionState::NotFound)
        {
            sLog.outString("TortoiseBots: Bot %s session ended (NotFound) — releasing",
                rec.characterGuid.GetString().c_str());
            it = m_bots.erase(it);
            continue;
        }
        ++it;
    }

    // Mature PlayerbotAI still exposes a donor-shaped population view for
    // perception/social queries. Refresh it from the authoritative native
    // records immediately before any AI update; it never owns sessions.
    PlayerbotAI::ProcessDelayedPackets();
    sRandomBotFacade.SyncNativePlayers();
    UpdateBots(diff);

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

            sLog.outString("TortoiseBots: PacketBridgeTest native command surface %s — list/stats/follow dispatched through ChatHandler",
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

            sLog.outString("TortoiseBots: PacketBridgeTest immediate native invite PASSED");
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
            sLog.outString("TortoiseBots: PacketBridgeTest group invite/accept PASSED — mature PlayerbotAI joined group");

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
            sLog.outString("TortoiseBots: PacketBridgeTest forced an out-of-world Headless bot");
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
            sLog.outString("TortoiseBots: PacketBridgeTest stranded Headless recovery PASSED");
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
            sLog.outString("TortoiseBots: PacketBridgeTest cleanup PASSED");
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
                sLog.outString("TortoiseBots: AutoTest step 1 — login bot %s", m_autoTestGuid.GetString().c_str());
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
                    sLog.outString("TortoiseBots: AutoTest step 2 — bot entered world, tick %u", rec->ticksInWorld);
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
                        sLog.outString("TortoiseBots: AutoTest LoggingIn tick %u sess %s state %u acct %u loading %u player %s pending %u", m_autoTestTicks, sessInfo.c_str(), static_cast<uint32>(st), rec->accountId, loading, playerInfo.c_str(), st == HeadlessSessionState::Pending);
                        if (st != HeadlessSessionState::NotFound && p && p->GetSession())
                            sLog.outString("TortoiseBots:   sess details network %u headless %u", p->GetSession()->HasNetworkTransport(), p->GetSession()->IsHeadless());
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
                        sLog.outString("TortoiseBots: AutoTest step 3 — saved bot %s", p->GetName());
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
                sLog.outString("TortoiseBots: AutoTest step 4 — logout bot");
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
                    sLog.outString("TortoiseBots: AutoTest step 5 — re-login bot");
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
                    sLog.outString("TortoiseBots: AutoTest step 6 — bot re-entered world, lifecycle PASSED; cleaning up");
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
                    sLog.outString("TortoiseBots: AutoTest Relogging tick %u state %u pending %u player %s", m_autoTestTicks, static_cast<uint32>(st), st == HeadlessSessionState::Pending, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
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
                sLog.outString("TortoiseBots: AutoTest cleanup %s; diagnostic disabled",
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
