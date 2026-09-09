#include "AhMarketService.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "BotManager.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/RandomBotFacade.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../ai/playerbot/strategy/values/BudgetValues.h"
#include "../ai/playerbot/WorldPosition.h"
#include "../ai/playerbot/GuidPosition.h"

#include "AuctionHouse/AuctionHouseMgr.h"
#include "ObjectMgr.h"
#include "World.h"
#include "Player.h"
#include "WorldSession.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Maps/GridMap.h"
#include "Maps/Map.h"
#if __has_include("LFT/LFTMgr.h")
#include "LFT/LFTMgr.h"
#elif __has_include("LFTMgr.h")
#include "LFTMgr.h"
#endif
#ifndef MANGOSSERVER_LFTMGR_H
#error "TortoiseBots AhMarketService requires core PR #416 (LFT/LFTMgr.h with sLFTMgr.IsQueued/IsInOffer). Update Tortoise core or remove AhMarketService from the build."
#endif

#include "Database/DBCStores.h"
#include "Database/DatabaseEnv.h"
#include "LootMgr.h"
#include "Spells/SpellMgr.h"
#include "Item.h"

#include <list>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace TortoiseBots
{

AhMarketService& AhMarketService::Instance()
{
    static AhMarketService instance;
    return instance;
}

Unit* AhMarketService::FindNearbyAuctioneer(Player* bot, ::PlayerbotAI* ai)
{
    if (!bot || !ai)
        return nullptr;

    // Bounded scan via existing AI value: "nearest npcs" is sight-limited
    // and already cached by the AI's usual update. No world scan here.
    std::list<ObjectGuid> npcs = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest npcs")->Get();
    for (ObjectGuid const& guid : npcs)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
        if (npc)
            return npc;
    }
    return nullptr;
}

bool AhMarketService::IsUsableAuctioneerPoint(ai::WorldPosition const& pos) const
{
    if (!pos.isValid() || !pos.isOverworld())
        return false;
    if (!pos.loadMapAndVMap(0))
        return false;
    TerrainInfo const* terrain = pos.getTerrain();
    if (!terrain)
        return false;
    float groundZ = INVALID_HEIGHT;
    float maxZ = terrain->GetWaterOrGroundLevel(pos.getX(), pos.getY(), pos.getZ(), &groundZ, false);
    return groundZ > INVALID_HEIGHT && maxZ > INVALID_HEIGHT &&
           pos.getZ() >= groundZ && pos.getZ() <= maxZ + 2.0f;
}

bool AhMarketService::IsBotInBattlegroundOrInstance(Player* bot) const
{
    if (!bot)
        return true;
    // Exact target-core APIs: Player::InBattleGround() / InBattleGroundQueue()
    // and Map::IsDungeon() / IsBattleGround(). Verified against
    // tortoise-wow/src/game/Objects/Player.h and src/game/Maps/Map.h.
    if (bot->InBattleGround() || bot->InBattleGroundQueue())
        return true;
    if (Map* map = bot->GetMap())
        if (map->IsDungeon() || map->IsBattleGround())
            return true;
    return false;
}

bool AhMarketService::IsBotAvailableForMarket(Player* bot) const
{
    if (!bot)
        return false;
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported())
        return false;
    // Any grouped / manual-use bot — fail-closed (world-thread, no queue mutation).
    if (bot->GetGroup())
        return false;
    // Active PlayerbotAI player master (socket-backed Network master).
    if (::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
        if (ai->HasActivePlayerMaster())
            return false;
    // LFT queued / in-offer — world-thread read-only, no m_queue mutation.
    // Hard dependency on core PR #416 LFT queue seam (LFT/LFTMgr.h); build fails
    // via #error if absent — no silent fallback, no fake queue behavior.
    if (sLFTMgr.IsQueued(bot->GetObjectGuid()) || sLFTMgr.IsInOffer(bot->GetObjectGuid()))
        return false;
    if (IsBotInBattlegroundOrInstance(bot))
        return false;
    return true;
}

void AhMarketService::EnsurePositionsLoaded()
{
    if (m_positionsLoaded)
        return;
    m_positionsLoaded = true;

    // One-time startup/first-enabled-tick snapshot from authoritative core creature data.
    // Enumerates core creature data once (bounded one-shot cost) and filters by
    // creature_template.npc_flags AUCTIONEER (no DB query per tick, no invented
    // coords, no map writes, no per-tick scan; subsequent ticks do not scan).
    // Additionally filters event-unspawned, invalid/template-less, and
    // unknown-faction spawns via authoritative sObjectMgr / GuidPosition /
    // sFactionTemplateStore; any spawn missing a template or faction template
    // is dropped. Validation mirrors BotManager::IsUsableTeleportPoint (terrain + VMap).
    // Marked loaded even when no valid positions exist, so the service is dormant
    // until restart/data reload rather than retrying every tick.
    auto all = ai::WorldPosition().GetCreaturesNear();
    for (auto cpair : all)
    {
        if (!cpair)
            continue;
        uint32 entry = cpair->second.creature_id[0];
        if (!entry)
            continue;
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);
        if (!cInfo)
            continue;
        if ((cInfo->npc_flags & UNIT_NPC_FLAG_AUCTIONEER) == 0)
            continue;
        if (cInfo->flags_extra & CREATURE_FLAG_EXTRA_INVISIBLE)
            continue;
        // Authoritative filters: invalid template, event-unspawned, unknown faction.
        // Uses GuidPosition's in-memory sObjectMgr / sGameEventMgr / faction stores;
        // no DB, no tick scan. Fail closed on unknown faction.
        ai::GuidPosition gpos(cpair);
        if (!gpos.GetCreatureTemplate())
            continue;
        if (gpos.IsEventUnspawned())
            continue;
        if (!gpos.GetFactionTemplateEntry())
            continue;
        ai::WorldPosition pos(cpair);
        if (!pos.isValid() || !pos.isOverworld())
            continue;
        if (!IsUsableAuctioneerPoint(pos))
            continue;
        AuctioneerPos ap;
        ap.mapId = pos.getMapId();
        ap.x = pos.getX();
        ap.y = pos.getY();
        ap.z = pos.getZ();
        ap.o = pos.getO();
        ap.entry = entry;
        m_auctioneerPositions.push_back(ap);
    }

    if (m_auctioneerPositions.empty())
        sLog.outError("TortoiseBots: AhMarket no auctioneer positions found - market dormant until restart/data reload (snapshot marked loaded, no per-tick retry)");
    else
        sLog.outString("TortoiseBots: AhMarket loaded %u auctioneer positions", (uint32)m_auctioneerPositions.size());
}

bool AhMarketService::TryTeleportToAuctioneer(Player* bot)
{
    if (!bot || bot->IsBeingTeleported() || !bot->IsInWorld() || !bot->IsAlive())
        return false;
    // Fail-closed: grouped/manual-use, active master, LFT queue, or BG/instance.
    if (!IsBotAvailableForMarket(bot))
        return false;
    if (m_auctioneerPositions.empty())
        return false;

    // Bounded random picks, skip hostile or unknown-faction auctioneers for this bot.
    // All coords are from core creature spawns, validated via IsUsableAuctioneerPoint.
    // Fail closed: missing faction data is treated as hostile (never send a bot
    // to an unknown/hostile house). No DB, no map scan; uses GuidPosition faction store.
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        size_t idx = urand(0, (uint32)m_auctioneerPositions.size() - 1);
        AuctioneerPos const& pos = m_auctioneerPositions[idx];
        uint32 entry = pos.entry;

        // Faction check: don't teleport a bot to an auctioneer that is hostile
        // or whose faction is unknown. Fail closed on missing data.
        ai::GuidPosition gpos(HIGHGUID_UNIT, entry);
        auto* aucFac = gpos.GetFactionTemplateEntry();
        if (!aucFac)
            continue;
        ai::GuidPosition bpos(bot);
        auto* botFac = bpos.GetFactionTemplateEntry();
        if (!botFac)
            continue;
        if (gpos.IsHostileTo(bot))
            continue;

        float o = pos.o;
        if (!std::isfinite(o) || o == 0.0f)
            o = bot->GetOrientation();

        bool ok = bot->TeleportTo(pos.mapId, pos.x, pos.y, pos.z, o, 0);
        if (ok)
        {
            sLog.outString("TortoiseBots: AhMarket teleported bot %s to auctioneer %u at map %u %.1f %.1f %.1f",
                bot->GetName(), entry, pos.mapId, pos.x, pos.y, pos.z);
            return true;
        }
    }
    return false;
}

AhMarketService::PostResult AhMarketService::TryPostForBot(Player* bot, bool allowTeleport)
{
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return PostResult::Failed;
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported())
        return PostResult::Failed;
    // Fail-closed eligibility: grouped/manual-use, active master, LFT queued/in-offer, BG/instance.
    if (!IsBotAvailableForMarket(bot))
        return PostResult::Failed;
    if (!sRandomBotFacade.IsRandomBot(bot))
        return PostResult::Failed;

    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return PostResult::Failed;

    // Per-bot rate limit via facade value store (no DB, no map scan).
    int32 remaining = sRandomBotFacade.GetValueValidTime(bot->GetGUIDLow(), "ahMarketLastPost");
    if (remaining > 0)
        return PostResult::Failed;

    // Pre-check: does this bot have any AH-profitable item to sell?
    // Avoids wasteful teleport for empty bags.
    std::list<Item*> items;
    {
        auto val = ai->GetAiObjectContext()->GetValue<std::list<Item*>>("inventory items", "usage " + std::to_string((uint8)ai::ItemUsage::ITEM_USAGE_AH));
        if (val)
            items = val->Get();
    }
    if (items.empty())
        return PostResult::Failed;

    bool hasProfitable = false;
    for (Item* item : items)
    {
        if (!item || !item->GetProto())
            continue;
        ItemPrototype const* proto = item->GetProto();
        std::string qualifier = ai::ItemQualifier(item).GetQualifier();
        ai->GetAiObjectContext()->GetValue<ai::ItemUsage>("item usage", qualifier)->Reset();
        ai::ItemUsage usage = ai->GetAiObjectContext()->GetValue<ai::ItemUsage>("item usage", qualifier)->Get();
        if (usage != ai::ItemUsage::ITEM_USAGE_AH)
            continue;
        if (!ai::ItemUsageValue::IsMoreProfitableToSellToAHThanToVendor(proto, bot))
            continue;
        hasProfitable = true;
        break;
    }
    if (!hasProfitable)
        return PostResult::Failed;

    Unit* auctioneer = FindNearbyAuctioneer(bot, ai);
    if (!auctioneer)
    {
        if (!allowTeleport)
            return PostResult::Failed;
        // Only record attempt cooldown when a post/teleport attempt actually starts;
        // do not burn cooldown merely because teleport budget was exhausted.
        // Success overwrites with interval*2; batch caps attempted/teleported.
        {
            int32 attemptCooldown = (int32)sPlayerbotAIConfig.ahMarketInterval;
            if (attemptCooldown < 5) attemptCooldown = 5;
            if (attemptCooldown > 3600) attemptCooldown = 3600;
            sRandomBotFacade.SetValue(bot->GetGUIDLow(), "ahMarketLastPost", 1, "", attemptCooldown);
        }
        EnsurePositionsLoaded();
        if (TryTeleportToAuctioneer(bot))
            return PostResult::Teleported;
        return PostResult::Failed;
    }

    // Per-bot attempt/failure cooldown when post attempt actually starts;
    // preserves failure cooldown and successful interval*2.
    {
        int32 attemptCooldown = (int32)sPlayerbotAIConfig.ahMarketInterval;
        if (attemptCooldown < 5) attemptCooldown = 5;
        if (attemptCooldown > 3600) attemptCooldown = 3600;
        sRandomBotFacade.SetValue(bot->GetGUIDLow(), "ahMarketLastPost", 1, "", attemptCooldown);
    }

    AuctionHouseEntry const* ahEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->GetObjectGuid());
    if (!ahEntry)
        return PostResult::Failed;

    AuctionHouseObject* ahObject = sAuctionMgr.GetAuctionsMap(ahEntry);
    if (!ahObject)
        return PostResult::Failed;

    uint32 limit = sWorld.getConfig(CONFIG_UINT32_ACCOUNT_CONCURRENT_AUCTION_LIMIT);
    if (limit && ahObject->GetAccountAuctionCount(bot->GetSession()->GetAccountId()) >= limit)
        return PostResult::Failed;

    // Bounded single item per bot per interval; iterate in stable order but respect
    // existing cache. Break early on first profitable post to keep tick cheap.
    for (Item* item : items)
    {
        if (!item || !item->GetProto())
            continue;

        ItemPrototype const* proto = item->GetProto();

        // Re-verify usage via actual weight/pricing API (not just cached list).
        std::string qualifier = ai::ItemQualifier(item).GetQualifier();
        // Reset cached usage to ensure fresh weight/equip check.
        ai->GetAiObjectContext()->GetValue<ai::ItemUsage>("item usage", qualifier)->Reset();
        ai::ItemUsage usage = ai->GetAiObjectContext()->GetValue<ai::ItemUsage>("item usage", qualifier)->Get();
        if (usage != ai::ItemUsage::ITEM_USAGE_AH)
            continue;

        // Verify actual pricing API says AH is better than vendor.
        if (!ai::ItemUsageValue::IsMoreProfitableToSellToAHThanToVendor(proto, bot))
            continue;

        // Deposit via core-owned formula; uses live AuctionHouseEntry + Item.
        // No fake fallback: real handler semantics (deposit 0 means free listing).
        uint32 deposit = AuctionHouseMgr::GetAuctionDeposit(ahEntry, 8 * HOUR, item);

        // Free money for AH via actual BudgetValue ("free money for").
        // Uses bot's real gold + budget allocation, not a fabricated pool.
        ai->GetAiObjectContext()->GetValue<uint32>("free money for", std::to_string((uint32)ai::NeedMoneyFor::ah))->Reset();
        uint32 freeMoney = ai->GetAiObjectContext()->GetValue<uint32>("free money for", std::to_string((uint32)ai::NeedMoneyFor::ah))->Get();
        if (deposit > freeMoney)
            continue;
        if (deposit > bot->GetMoney())
            continue;

        // Price via market read model if available, falling back to bot sell multiplier
        uint32 count = item->GetCount();
        if (!count)
            count = 1;
        uint32 pricePerItem = ai::ItemUsageValue::DesiredPricePerItem(bot, proto, count, urand(40, 60));
        if (!pricePerItem)
        {
            uint32 basePerItem = ai::ItemUsageValue::GetBotSellPrice(proto, bot);
            if (!basePerItem)
                basePerItem = proto->SellPrice ? proto->SellPrice : 1;
            uint32 pct = urand(75, 100);
            pricePerItem = (basePerItem * pct) / 100;
        }
        if (!pricePerItem)
            pricePerItem = 1;
        uint32 totalPrice = pricePerItem * count;
        if (!totalPrice)
            totalPrice = 1;

        // Build native packet and transact through core handler (ownership, limits,
        // inventory removal, deposit, DB save all in core).
        ObjectGuid itemGuid = item->GetObjectGuid();
        uint32 bid = totalPrice * 95 / 100;
        if (!bid)
            bid = 1;
        uint32 buyout = totalPrice;
        uint32 etime = 8 * HOUR / MINUTE; // 8h auction, validated by handler

        WorldPacket packet;
        packet << auctioneer->GetObjectGuid();
        packet << itemGuid;
        packet << bid;
        packet << buyout;
        packet << etime;

        // Per-bot posting uses the same try_lock gate as per-bot AhAction; outer
        // Update already holds the lock, so this is just the native call.
        bot->GetSession()->HandleAuctionSellItem(packet);

        // Verify via legitimate ownership: item should no longer be in inventory
        // if core accepted the listing (deposit taken, auction created).
        if (bot->GetItemByGuid(itemGuid))
            continue; // handler rejected (e.g., soulbound, bag position, limit race)

        // Success: record rate-limit timestamp and log via native BotLog.
        sRandomBotFacade.SetValue(bot->GetGUIDLow(), "ahMarketLastPost", 1, "", (int32)sPlayerbotAIConfig.ahMarketInterval * 2);
        sPlayerbotAIConfig.logEvent(ai, "AhMarket", proto->Name1, std::to_string(proto->ItemId));
        sLog.outString("TortoiseBots: AhMarket bot %s posted %s x%u for %u buyout (deposit %u) via auctioneer %s",
            bot->GetName(), proto->Name1.c_str(), count, buyout, deposit, auctioneer->GetName());

        return PostResult::Posted;
    }

    return PostResult::Failed;
}

bool AhMarketService::IsSyntheticAuction(uint32_t auctionId) const
{
    return m_syntheticAuctions.count(auctionId) > 0;
}

bool AhMarketService::IsSyntheticAuction(AuctionEntry const* auction) const
{
    if (!auction)
        return false;
    return auction->owner == SYNTHETIC_OWNER_GUID || m_syntheticAuctions.count(auction->Id) > 0;
}

bool AhMarketService::IsSyntheticItem(uint32_t itemGuidLow) const
{
    return m_syntheticItemGuids.count(itemGuidLow) > 0;
}

bool AhMarketService::IsItemBlacklisted(uint32_t itemId) const
{
    auto it = m_overrides.find(itemId);
    if (it != m_overrides.end())
        return it->second.add_chance == 0 || it->second.value == 0;
    return false;
}

bool AhMarketService::AssertSyntheticIsolation(Player const* player, uint32_t itemGuidLow)
{
    if (!player)
        return true;
    ObjectGuid guid(HIGHGUID_ITEM, itemGuidLow);
    Item* item = player->GetItemByGuid(guid);
    if (item != nullptr)
    {
        sLog.outError("TortoiseBots: INVARIANT VIOLATION: synthetic item %u found in player %s inventory!",
            itemGuidLow, player->GetName());
        return false;
    }
    return true;
}

void AhMarketService::ReloadOverrides()
{
    m_overrides.clear();
    m_overridesLoaded = true;

    auto result = CharacterDatabase.Query("SELECT item, value, add_chance, min_amount, max_amount FROM ahbot_items");
    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            uint32 itemId = f[0].GetUInt32();
            if (!itemId)
                continue;
            ItemOverride ov;
            ov.value = f[1].GetUInt32();
            ov.add_chance = std::min<uint32>(100, f[2].GetUInt32());
            ov.min_amount = f[3].GetUInt32();
            ov.max_amount = std::max(ov.min_amount, f[4].GetUInt32());
            m_overrides[itemId] = ov;
        } while (result->NextRow());
        sLog.outString("TortoiseBots: AhMarket loaded %zu item overrides from ahbot_items", m_overrides.size());
    }
    else
    {
        sLog.outString("TortoiseBots: AhMarket no overrides in ahbot_items or table not found");
    }
}

bool AhMarketService::SetItemOverride(uint32_t itemId, uint32_t value, uint32_t addChance, uint32_t minAmount, uint32_t maxAmount)
{
    if (!itemId)
        return false;
    ItemOverride ov;
    ov.value = value;
    ov.add_chance = std::min<uint32>(100, addChance);
    ov.min_amount = minAmount;
    ov.max_amount = std::max(minAmount, maxAmount);
    m_overrides[itemId] = ov;

    CharacterDatabase.PExecute("REPLACE INTO ahbot_items (item, value, add_chance, min_amount, max_amount) VALUES ('%u', '%u', '%u', '%u', '%u')",
        itemId, value, ov.add_chance, ov.min_amount, ov.max_amount);
    return true;
}

bool AhMarketService::ResetItemOverride(uint32_t itemId)
{
    if (!itemId)
        return false;
    m_overrides.erase(itemId);
    CharacterDatabase.PExecute("DELETE FROM ahbot_items WHERE item = '%u'", itemId);
    return true;
}

void AhMarketService::RebuildMarket(bool all)
{
    m_pendingRebuild = all ? 2 : 1;
    m_phase = Phase::Idle;
    sLog.outString("TortoiseBots: AhMarket scheduled market rebuild (all=%u)", all ? 1 : 0);
}

std::string AhMarketService::GetStatus() const
{
    char const* phaseNames[] = {"Idle", "Gather", "Overrides", "Post", "Buy", "Expire"};
    std::ostringstream ss;
    ss << "AhMarket Status: Phase=" << phaseNames[static_cast<uint8_t>(m_phase)]
       << " SyntheticAuctions=" << m_syntheticAuctions.size()
       << " Stock=" << m_stock.size()
       << " Overrides=" << m_overrides.size()
       << " LastSliceUs=" << m_lastSliceUs
       << " MaxSliceUs=" << m_maxSliceUs
       << " TotalListed=" << m_totalListed
       << " TotalBought=" << m_totalBought
       << " TotalExpired=" << m_totalExpired
       << " ProtectedBids=" << m_totalProtectedBids;
    return ss.str();
}

void AhMarketService::EnsureSourcesLoaded()
{
    if (m_sourcesLoaded)
        return;
    m_sourcesLoaded = true;

    if (!m_overridesLoaded)
        ReloadOverrides();

    m_sources.resize(10);
    // Source 0..4: Creature drops (Normal, Elite, Rare Elite, World Boss, Rare)
    int32 creatureRange[5][4] = {
        {30, 35, 8, 12},
        {30, 34, 1, 2},
        {-10, 2, 1, 1},
        {-20, 1, 1, 1},
        {0, 10, 1, 1}
    };
    for (uint32 rank = 0; rank < 5; ++rank)
    {
        m_sources[rank].store = &LootTemplates_Creature;
        for (int i = 0; i < 4; ++i)
            m_sources[rank].range[i] = creatureRange[rank][i];
        std::string sql = "SELECT loot_id FROM creature_template WHERE loot_id<>0 AND `rank`=" + std::to_string(rank);
        auto res = WorldDatabase.Query(sql.c_str());
        if (res)
        {
            do {
                m_sources[rank].ids.push_back(res->Fetch()[0].GetUInt32());
            } while (res->NextRow());
        }
    }

    // Source 5: Disenchant
    m_sources[5].store = &LootTemplates_Disenchant;
    m_sources[5].range[0] = 10; m_sources[5].range[1] = 12; m_sources[5].range[2] = 1; m_sources[5].range[3] = 1;
    if (auto res = WorldDatabase.Query("SELECT DISTINCT entry FROM disenchant_loot_template"))
    {
        do { m_sources[5].ids.push_back(res->Fetch()[0].GetUInt32()); } while (res->NextRow());
    }

    // Source 6: Fishing
    m_sources[6].store = &LootTemplates_Fishing;
    m_sources[6].range[0] = 3; m_sources[6].range[1] = 5; m_sources[6].range[2] = 30; m_sources[6].range[3] = 40;
    if (auto res = WorldDatabase.Query("SELECT DISTINCT entry FROM fishing_loot_template"))
    {
        do { m_sources[6].ids.push_back(res->Fetch()[0].GetUInt32()); } while (res->NextRow());
    }

    // Source 7: Gameobject (chests / herbs / mining)
    m_sources[7].store = &LootTemplates_Gameobject;
    m_sources[7].range[0] = 13; m_sources[7].range[1] = 16; m_sources[7].range[2] = 7; m_sources[7].range[3] = 11;
    if (auto res = WorldDatabase.Query("SELECT DISTINCT gt.data1 FROM gameobject_template gt JOIN gameobject g ON g.id=gt.entry WHERE gt.type=3 AND g.spawntimesecsmax>0 AND gt.data1<>0"))
    {
        do { m_sources[7].ids.push_back(res->Fetch()[0].GetUInt32()); } while (res->NextRow());
    }

    // Source 8: Skinning
    m_sources[8].store = &LootTemplates_Skinning;
    m_sources[8].range[0] = 3; m_sources[8].range[1] = 5; m_sources[8].range[2] = 50; m_sources[8].range[3] = 50;
    if (auto res = WorldDatabase.Query("SELECT DISTINCT entry FROM skinning_loot_template"))
    {
        do { m_sources[8].ids.push_back(res->Fetch()[0].GetUInt32()); } while (res->NextRow());
    }

    // Source 9: Crafted items from professions
    m_sources[9].store = nullptr;
    m_sources[9].range[0] = 80; m_sources[9].range[1] = 90; m_sources[9].range[2] = 0; m_sources[9].range[3] = 50;
    std::set<uint32> crafts;
    for (uint32 id = 1; id < sSpellMgr.GetMaxSpellId(); ++id)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(id);
        if (spell && (spell->Attributes & 32) && (spell->Attributes & 65536))
        {
            for (uint32 e = 0; e < 3; ++e)
            {
                if (spell->Effect[e] == SPELL_EFFECT_CREATE_ITEM && sObjectMgr.GetItemPrototype(spell->EffectItemType[e]))
                    crafts.insert(spell->EffectItemType[e]);
            }
        }
    }
    m_sources[9].ids.assign(crafts.begin(), crafts.end());

    // Vendor items list
    if (auto res = WorldDatabase.Query("SELECT item FROM npc_vendor UNION SELECT item FROM npc_vendor_template"))
    {
        do { m_vendorItems.push_back(res->Fetch()[0].GetUInt32()); } while (res->NextRow());
    }

    sLog.outString("TortoiseBots: AhMarket loaded generation sources: %zu crafts, %zu vendor items",
        crafts.size(), m_vendorItems.size());
}

void AhMarketService::RefreshDynamicLevel()
{
    if (!sPlayerbotAIConfig.ahMarketDynamicLevel || time(nullptr) < m_nextLevelCheck)
        return;

    uint32 highest = 0;
    uint32 fallback = 0;
    for (auto const& pair : sWorld.GetAllSessions())
    {
        if (pair.second)
        {
            if (Player* p = pair.second->GetPlayer())
            {
                fallback = std::max(fallback, (uint32)p->GetLevel());
                if (pair.second->GetSecurity() == SEC_PLAYER)
                    highest = std::max(highest, (uint32)p->GetLevel());
            }
        }
    }

    m_maxRequiredLevel = highest ? highest : (fallback ? fallback : sPlayerbotAIConfig.ahMarketMaxLevel);
    m_maxItemLevel = m_maxRequiredLevel >= 60 ? 255 : m_maxRequiredLevel + 5;
    m_nextLevelCheck = time(nullptr) + sPlayerbotAIConfig.ahMarketLevelRefresh;
}

bool AhMarketService::IsItemEligible(ItemPrototype const* proto, bool forced) const
{
    if (!proto || !proto->Stackable || proto->Quality > sPlayerbotAIConfig.ahMarketMaxQuality)
        return false;
    if (IsItemBlacklisted(proto->ItemId))
        return false;
    if (forced)
        return true;
    if (proto->RequiredLevel > m_maxRequiredLevel || proto->ItemLevel > m_maxItemLevel)
        return false;
    if (proto->Bonding == BIND_WHEN_PICKED_UP || proto->Bonding == BIND_QUEST_ITEM)
        return false;
    if (proto->Flags & ITEM_FLAG_HAS_LOOT)
        return false;
    return true;
}

uint32_t AhMarketService::CalculatePrice(ItemPrototype const* proto) const
{
    if (!proto)
        return 0;
    auto it = m_overrides.find(proto->ItemId);
    if (it != m_overrides.end() && it->second.value > 0)
        return it->second.value;

    uint32 qualityMultiplier = 100;
    switch (proto->Quality)
    {
        case ITEM_QUALITY_POOR: qualityMultiplier = 50; break;
        case ITEM_QUALITY_NORMAL: qualityMultiplier = 100; break;
        case ITEM_QUALITY_UNCOMMON: qualityMultiplier = 200; break;
        case ITEM_QUALITY_RARE: qualityMultiplier = 400; break;
        case ITEM_QUALITY_EPIC: qualityMultiplier = 800; break;
        case ITEM_QUALITY_LEGENDARY: qualityMultiplier = 1600; break;
        case ITEM_QUALITY_ARTIFACT: qualityMultiplier = 3200; break;
        default: break;
    }

    uint64 base = proto->BuyPrice;
    if (!base || (proto->SellPrice && proto->BuyPrice / proto->SellPrice > 5))
        base = uint64(proto->SellPrice) * (proto->Quality <= 1 ? 4 : 5);
    if (!base)
        base = 1;

    uint32 price = uint32(std::min<uint64>(base * qualityMultiplier / 100, 0x7fffffffULL));
    return ApplyVariance(price);
}

uint32_t AhMarketService::ApplyVariance(uint32_t price) const
{
    if (!price || !sPlayerbotAIConfig.ahMarketVariance)
        return price ? price : 1;
    int32 variance = (int32)sPlayerbotAIConfig.ahMarketVariance;
    int32 delta = (int32)urand(0, variance * 2) - variance;
    int64 varied = int64(price) + (int64(price) * delta / 100);
    if (varied <= 0)
        return 1;
    return (uint32_t)std::min<int64>(varied, 0x7fffffffLL);
}

uint32_t AhMarketService::CalculateStack(ItemPrototype const* proto, uint32_t stockCount, uint32_t unitPrice) const
{
    if (!proto || !stockCount || !unitPrice || !proto->Stackable)
        return 0;
    uint32 maxAffordable = uint32(0x7fffffffULL / unitPrice);
    uint32 count = std::min<uint32>(stockCount, std::min<uint32>(proto->Stackable, maxAffordable));
    auto it = m_overrides.find(proto->ItemId);
    if (it != m_overrides.end())
    {
        if (it->second.min_amount)
            count = std::max(count, it->second.min_amount);
        if (it->second.max_amount)
            count = std::min(count, it->second.max_amount);
    }
    return count ? count : 1;
}

bool AhMarketService::PublishSyntheticAuction(uint32_t itemId, uint32_t count, uint32_t unitPrice)
{
    if (!itemId || !count || !unitPrice)
        return false;

    uint32 houseIds[] = {1, 6, 7};
    uint32 houseId = houseIds[m_currentHouse % 3];
    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
    if (!ahEntry)
        return false;

    AuctionHouseObject* ahObject = sAuctionMgr.GetAuctionsMap(ahEntry);
    if (!ahObject)
        return false;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return false;

    std::unique_ptr<Item> item(Item::CreateItem(itemId, count));
    if (!item)
        return false;

    // Isolation guarantee: Tag with SYNTHETIC_OWNER_GUID (0). Never enters player inventory.
    item->SetOwnerGuid(ObjectGuid(HIGHGUID_PLAYER, SYNTHETIC_OWNER_GUID));
    if (int32 property = Item::GenerateItemRandomPropertyId(itemId))
        item->SetItemRandomProperties(property);
    item->ClearUpdateMask(false);

    std::unique_ptr<AuctionEntry> auction(new AuctionEntry{});
    auction->Id = sObjectMgr.GenerateAuctionID();
    auction->itemGuidLow = item->GetGUIDLow();
    auction->itemTemplate = itemId;
    auction->owner = SYNTHETIC_OWNER_GUID;
    auction->ownerAccount = SYNTHETIC_OWNER_ACCOUNT;
    auction->buyout = uint32(std::min<uint64>(uint64(unitPrice) * count, 0x7fffffffULL));
    uint32 bidPct = urand(sPlayerbotAIConfig.ahMarketBidMin, sPlayerbotAIConfig.ahMarketBidMax);
    auction->startbid = std::max<uint32>(1, uint64(auction->buyout) * bidPct / 100);
    auction->bid = 0;
    auction->bidder = 0;
    auction->deposit = 0;
    auction->depositTime = time(nullptr);
    auction->expireTime = auction->depositTime + urand(sPlayerbotAIConfig.ahMarketTimeMin, sPlayerbotAIConfig.ahMarketTimeMax) * HOUR;
    auction->auctionHouseEntry = ahEntry;

    CharacterDatabase.BeginTransaction();
    item->SaveToDB();
    auction->SaveToDB();
    CharacterDatabase.CommitTransaction();

    uint32 itemGuidLow = item->GetGUIDLow();
    uint32 auctionId = auction->Id;
    sAuctionMgr.AddAItem(item.release());
    ahObject->AddAuction(auction.release());

    m_syntheticAuctions.insert(auctionId);
    m_syntheticItemGuids.insert(itemGuidLow);
    return true;
}

bool AhMarketService::BuyAuctionCandidate(AuctionEntry* auction, AuctionHouseObject* ahObject)
{
    (void)ahObject;
    if (!auction || auction->expireTime <= time(nullptr))
        return false;
    if (!auction->lockedIpAddress.empty() && auction->depositTime + 300 >= time(nullptr))
        return false;

    if (IsItemBlacklisted(auction->itemTemplate))
        return false;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(auction->itemTemplate);
    if (!proto)
        return false;

    Item* aItem = sAuctionMgr.GetAItem(auction->itemGuidLow);
    uint32 count = aItem ? aItem->GetCount() : 1;
    if (!count) count = 1;

    std::vector<Player*> bots = BotManager::Instance().GetAllBots();
    if (bots.empty())
        return false;

    std::vector<Player*> eligible;
    for (Player* bot : bots)
    {
        if (!IsBotAvailableForMarket(bot))
            continue;
        if (!sRandomBotFacade.IsRandomBot(bot))
            continue;
        if (auction->owner == bot->GetGUIDLow())
            continue;
        if (auction->ownerAccount == bot->GetSession()->GetAccountId())
            continue;
        if (auction->bidder == bot->GetGUIDLow())
            continue;
        if (auction->bidder && sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, auction->bidder)) == bot->GetSession()->GetAccountId())
            continue;
        eligible.push_back(bot);
    }
    if (eligible.empty())
        return false;

    Player* buyer = eligible[urand(0, eligible.size() - 1)];
    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(buyer);
    if (!ai)
        return false;

    Unit* auctioneer = FindNearbyAuctioneer(buyer, ai);
    if (!auctioneer)
        return false;

    uint32 fairUnit = CalculatePrice(proto);
    uint32 medianMarket = ai::ItemUsageValue::GetAHMedianBuyoutPricePerItem(proto, buyer);
    if (medianMarket > 0)
        fairUnit = (fairUnit + medianMarket) / 2;

    uint64 willingness = (uint64(fairUnit) * count * sPlayerbotAIConfig.ahMarketBuyValue) / 100;
    if (!willingness)
        return false;

    uint32 buyout = auction->buyout;
    uint32 curBid = auction->bid;
    uint32 outbid = auction->GetAuctionOutBid();
    uint32 nextBid = curBid ? (curBid + outbid) : auction->startbid;

    bool canBuyout = (buyout > 0 && willingness >= buyout);
    bool canBid = (!canBuyout && willingness >= nextBid && (buyout == 0 || nextBid < buyout));

    if (!canBuyout && !canBid)
        return false;

    uint32 targetPrice = canBuyout ? buyout : nextBid;

    if (buyer->GetMoney() < targetPrice)
        return false;

    ai->GetAiObjectContext()->GetValue<uint32>("free money for", std::to_string((uint32)ai::NeedMoneyFor::ah))->Reset();
    uint32 freeMoney = ai->GetAiObjectContext()->GetValue<uint32>("free money for", std::to_string((uint32)ai::NeedMoneyFor::ah))->Get();
    if (targetPrice > freeMoney)
        return false;

    if (sPlayerbotAIConfig.ahMarketMaxSpendPerBot > 0 && targetPrice > sPlayerbotAIConfig.ahMarketMaxSpendPerBot)
        return false;

    WorldPacket packet;
    packet << auctioneer->GetObjectGuid();
    packet << auction->Id;
    packet << targetPrice;

    buyer->GetSession()->HandleAuctionPlaceBid(packet);
    ++m_totalBought;
    sLog.outString("TortoiseBots: AhMarket buyer %s placed %s on auc %u (item %s x%u) for %u",
        buyer->GetName(), canBuyout ? "buyout" : "bid", auction->Id, proto->Name1.c_str(), count, targetPrice);

    return true;
}

void AhMarketService::StepWorkSlice()
{
    if (m_phase == Phase::Idle && !m_pendingRebuild && m_rebuildRemaining == 0 && time(nullptr) < m_nextCycleCheck)
        return;

    auto start = std::chrono::steady_clock::now();
    uint64 budgetUs = sPlayerbotAIConfig.ahMarketBudgetUs;
    uint32 maxOps = sPlayerbotAIConfig.ahMarketMaxOperations;

    uint32 ops = 0;
    while (ops < maxOps)
    {
        StepPhase();
        ++ops;
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= (int64)budgetUs)
            break;
        if (m_phase == Phase::Idle && !m_pendingRebuild && m_rebuildRemaining == 0)
            break;
    }

    auto totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    m_lastSliceUs = totalElapsed;
    m_maxSliceUs = std::max(m_maxSliceUs, (uint64_t)totalElapsed);
}

void AhMarketService::StepPhase()
{
    switch (m_phase)
    {
        case Phase::Idle:
        {
            if (m_pendingRebuild == 2)
            {
                m_scanHouse = 0;
                m_scanCursor = 0;
                m_phase = Phase::Expire;
                return;
            }
            if (m_pendingRebuild == 1)
            {
                m_pendingRebuild = 0;
                RefreshDynamicLevel();
                m_stock.clear();
                m_sourceIndex = 0;
                m_picksRemaining = -1;
                m_rollsRemaining = 0;
                m_phase = Phase::Gather;
                return;
            }

            m_actionCycle = (m_actionCycle + 1) % 6;
            m_currentHouse = m_actionCycle % 3;

            if (m_actionCycle < 3 && sPlayerbotAIConfig.ahMarketSyntheticSupply && urand(0, 99) < sPlayerbotAIConfig.ahMarketChanceSell)
            {
                RefreshDynamicLevel();
                m_stock.clear();
                m_sourceIndex = 0;
                m_picksRemaining = -1;
                m_rollsRemaining = 0;
                m_phase = Phase::Gather;
            }
            else if (m_actionCycle >= 3 && sPlayerbotAIConfig.ahMarketBuyer && urand(0, 99) < sPlayerbotAIConfig.ahMarketChanceBuy)
            {
                m_scanHouse = m_currentHouse;
                m_scanCursor = 0;
                m_phase = Phase::Buy;
            }
            else
            {
                FinishPass();
            }
            return;
        }
        case Phase::Gather:
            StepGather();
            return;
        case Phase::Overrides:
            StepOverrides();
            return;
        case Phase::Post:
            StepPost();
            return;
        case Phase::Buy:
            StepBuy();
            return;
        case Phase::Expire:
            StepExpire();
            return;
    }
}

void AhMarketService::StepGather()
{
    if (m_sourceIndex >= m_sources.size())
    {
        m_overrideCursor = 0;
        m_phase = Phase::Overrides;
        return;
    }

    GenerationSource const& s = m_sources[m_sourceIndex];
    if (s.ids.empty() || !s.range[1] || !s.range[3])
    {
        ++m_sourceIndex;
        m_picksRemaining = -1;
        return;
    }

    if (m_picksRemaining < 0)
        m_picksRemaining = int32(std::max<int64>(0, int64(urand(0, s.range[1] - s.range[0])) + s.range[0]));

    if (!m_rollsRemaining)
    {
        if (!m_picksRemaining)
        {
            ++m_sourceIndex;
            m_picksRemaining = -1;
            return;
        }
        --m_picksRemaining;
        m_selectedTemplate = s.ids[urand(0, s.ids.size() - 1)];
        if (!s.store)
        {
            auto p = sObjectMgr.GetItemPrototype(m_selectedTemplate);
            if (IsItemEligible(p, false) && p->Quality)
            {
                uint32 stack = p->Stackable ? p->Stackable : 1;
                m_stock[m_selectedTemplate] += std::max<uint32>(1, stack * urand(s.range[2], s.range[3]) / 100);
            }
            return;
        }
        m_rollsRemaining = urand(s.range[2], s.range[3]);
        if (!m_rollsRemaining)
            return;
    }

    --m_rollsRemaining;
    if (LootTemplate const* table = s.store->GetLootFor(m_selectedTemplate))
    {
        Loot loot(nullptr, 0);
        table->Process(loot, *s.store, s.store->IsRatesAllowed());
        for (auto const& item : loot.items)
        {
            if (IsItemEligible(sObjectMgr.GetItemPrototype(item.itemid), false))
                m_stock[item.itemid] += item.count;
        }
    }
}

void AhMarketService::StepOverrides()
{
    if (m_overrides.empty())
    {
        m_phase = Phase::Post;
        return;
    }

    auto it = m_overrides.upper_bound(m_overrideCursor);
    if (it == m_overrides.end())
    {
        m_phase = Phase::Post;
        return;
    }
    m_overrideCursor = it->first;
    if (it->second.add_chance && it->second.value)
    {
        if (urand(0, 99) < it->second.add_chance)
        {
            uint32 qty = urand(it->second.min_amount, it->second.max_amount);
            if (!qty) qty = 1;
            m_stock[it->first] += qty;
        }
    }
}

void AhMarketService::StepPost()
{
    if (m_stock.empty())
    {
        FinishPass();
        return;
    }

    auto it = m_stock.begin();
    uint32 itemId = it->first;
    uint32 stockCount = it->second;

    auto p = sObjectMgr.GetItemPrototype(itemId);
    auto ov = m_overrides.find(itemId);
    bool forced = (ov != m_overrides.end() && ov->second.add_chance > 0 && ov->second.value > 0);

    if (!stockCount || !IsItemEligible(p, forced))
    {
        m_stock.erase(it);
        return;
    }

    uint32 price = CalculatePrice(p);
    uint32 count = CalculateStack(p, stockCount, price);
    if (!count)
    {
        m_stock.erase(it);
        return;
    }

    if (PublishSyntheticAuction(itemId, count, price))
        ++m_totalListed;
    else
        ++m_totalFailed;

    if (count >= it->second)
        m_stock.erase(it);
    else
        it->second -= count;
}

void AhMarketService::StepBuy()
{
    uint32 houseIds[] = {1, 6, 7};
    uint32 houseId = houseIds[m_scanHouse % 3];
    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
    if (!ahEntry)
    {
        FinishPass();
        return;
    }

    AuctionHouseObject* ahObject = sAuctionMgr.GetAuctionsMap(ahEntry);
    if (!ahObject || !ahObject->GetAuctions())
    {
        FinishPass();
        return;
    }

    auto const* auctions = ahObject->GetAuctions();
    if (auctions->empty())
    {
        FinishPass();
        return;
    }

    auto it = auctions->upper_bound(m_scanCursor);
    if (it == auctions->end())
    {
        FinishPass();
        return;
    }

    AuctionEntry* auction = it->second;
    m_scanCursor = auction->Id;

    BuyAuctionCandidate(auction, ahObject);
}

void AhMarketService::StepExpire()
{
    uint32 houseIds[] = {1, 6, 7};
    uint32 houseId = houseIds[m_scanHouse % 3];
    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
    if (!ahEntry)
    {
        if (m_pendingRebuild)
        {
            m_pendingRebuild = 0;
            FinishPass();
        }
        else
            FinishPass();
        return;
    }

    AuctionHouseObject* ahObject = sAuctionMgr.GetAuctionsMap(ahEntry);
    if (!ahObject || !ahObject->GetAuctions())
    {
        FinishPass();
        return;
    }

    auto const* auctions = ahObject->GetAuctions();
    auto it = auctions->upper_bound(m_scanCursor);
    if (it == auctions->end())
    {
        ++m_scanHouse;
        m_scanCursor = 0;
        if (m_scanHouse >= 3)
        {
            m_pendingRebuild = 0;
            FinishPass();
        }
        return;
    }

    AuctionEntry* auction = it->second;
    m_scanCursor = auction->Id;

    if (!IsSyntheticAuction(auction))
        return;

    // Active bids represent committed player or bot currency.
    // Never expire or delete an auction that has an active bid, even during
    // admin rebuild-all. The listing must run its natural course to either
    // victory (SendAuctionWonMail) or outbid refund so money is preserved.
    if (auction->bid != 0)
    {
        ++m_totalProtectedBids;
        return;
    }

    uint32 itemGuidLow = auction->itemGuidLow;
    uint32 aucId = auction->Id;

    CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid='%u'", itemGuidLow);
    auction->DeleteFromDB();
    sAuctionMgr.RemoveAItem(itemGuidLow);
    ahObject->RemoveAuction(auction);
    m_syntheticAuctions.erase(aucId);
    m_syntheticItemGuids.erase(itemGuidLow);
    delete auction;
    ++m_totalExpired;
}

void AhMarketService::FinishPass()
{
    m_phase = Phase::Idle;
    m_nextCycleCheck = time(nullptr) + 20;
}

void AhMarketService::Update(uint32_t diff)
{
    if (!sPlayerbotAIConfig.ahMarketEnabled)
        return;
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotAutologin)
        return;

    // Respect existing per-bot AhAction mutex via try_lock (no wait).
    if (!sRandomBotFacade.m_ahActionMutex.try_lock())
        return;
    struct UnlockGuard
    {
        std::mutex& m;
        ~UnlockGuard() { m.unlock(); }
    } guard{sRandomBotFacade.m_ahActionMutex};

    EnsurePositionsLoaded();

    // 1. Phased synthetic supply & buyer engine with work budgets (Issue #88)
    if (sPlayerbotAIConfig.ahMarketSyntheticSupply || sPlayerbotAIConfig.ahMarketBuyer)
    {
        EnsureSourcesLoaded();
        StepWorkSlice();
    }

    // 2. Real-inventory seller loop
    uint32 intervalMs = sPlayerbotAIConfig.ahMarketInterval * 1000;
    if (intervalMs < 1000)
        intervalMs = 1000;
    if (intervalMs > 3600000)
        intervalMs = 3600000;

    m_elapsedMs += diff;
    if (m_elapsedMs < intervalMs)
        return;
    m_elapsedMs = 0;

    // Snapshot of online headless random bots (no DB query, no AH scan).
    std::vector<Player*> bots = BotManager::Instance().GetAllBots();
    std::vector<Player*> eligible;
    eligible.reserve(bots.size());
    for (Player* bot : bots)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported())
            continue;
        if (!IsBotAvailableForMarket(bot))
            continue;
        if (!sRandomBotFacade.IsRandomBot(bot))
            continue;
        if (!PlayerbotAIStorage::Instance().GetAI(bot))
            continue;
        eligible.push_back(bot);
    }
    if (eligible.empty())
        return;

    uint32 batch = sPlayerbotAIConfig.ahMarketBatchSize;
    if (!batch)
        batch = 1;
    if (batch > 5)
        batch = 5; // hard cap to keep tick cheap
    if (batch > eligible.size())
        batch = (uint32)eligible.size();

    uint32 posted = 0;
    uint32 teleported = 0;
    uint32 attempted = 0;
    size_t start = m_nextIndex % eligible.size();
    for (size_t offset = 0; offset < eligible.size() && attempted < batch; ++offset)
    {
        size_t idx = (start + offset) % eligible.size();
        Player* bot = eligible[idx];
        bool allowTeleport = teleported < batch;
        PostResult res = TryPostForBot(bot, allowTeleport);
        ++attempted;
        if (res == PostResult::Posted)
            ++posted;
        else if (res == PostResult::Teleported)
            ++teleported;
    }

    if (posted || teleported)
        m_nextIndex = (start + attempted) % eligible.size();
    else
        m_nextIndex = (start + 1) % eligible.size();

    if (posted || teleported)
        sLog.outString("TortoiseBots: AhMarket tick posted %u/%u teleported %u (eligible %u, auctioneers %u)",
            posted, batch, teleported, (uint32)eligible.size(), (uint32)m_auctioneerPositions.size());
}

} // namespace TortoiseBots
