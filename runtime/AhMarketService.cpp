#include "AhMarketService.h"
#include "runtime/policy/ah/AhBuyerPolicy.h"
#include "runtime/policy/ah/AhSellerTeleportPolicy.h"
#include "runtime/policy/ah/AhSellPolicy.h"
#include "BotActivityLease.h"
#include "runtime/policy/ah/TradingLeasePolicy.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "BotManager.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/RandomBotFacade.h"
#include "../ai/playerbot/RandomItemMgr.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/strategy/ItemVisitors.h"
#include "../ai/playerbot/strategy/values/BudgetValues.h"
#include "../ai/playerbot/WorldPosition.h"
#include "../ai/playerbot/GuidPosition.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../ai/playerbot/strategy/values/TravelValues.h"

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
#include "Item.h"
#include "../host/ModuleLog.h"

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
void AhMarketService::OnLeaseEvicted(uint32_t guidLow)
{
    if (!guidLow)
        return;
    // Clear the per-bot attempt cooldown so the bot rejoins the normal
    // market cadence instead of sitting out an evicted attempt.
    sRandomBotFacade.SetValue(guidLow, "ahMarketLastPost", 0, "", 0);
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
        TB_LOG_BASIC("TortoiseBots: AhMarket loaded %u auctioneer positions", (uint32)m_auctioneerPositions.size());
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

    // All coords are from core creature spawns, validated via IsUsableAuctioneerPoint.
    // Fail closed: missing faction data is treated as hostile (never send a bot
    // to an unknown/hostile house). No DB, no map scan; uses GuidPosition faction store.
    // Same-map nearest lift (live 2026-10-03: seller teleports yanked questing
    // bots across continents onto auctioneer spots, e.g. Dun Morogh -> Darnassus
    // AH; a random same-map pick still lands an idle Dun Morogh gnome in Booty
    // Bay). The organic buyer already requires a same-map house (AhBuyerPolicy.h);
    // a cross-map house is unroutable for the walk back. The lift goes to the
    // nearest non-hostile house on the bot's map, and only for a bot already
    // heading to an AH or standing near that house (Tharobraeth Dun Morogh ->
    // Stormwind AH shows even near hops hijack a quest trip). Anything else
    // keeps walking; the post is skipped until the bot gets there itself
    // (AhSellerTeleportPolicy.h).
    bool tripIsAH = false;
    if (::PlayerbotAI* marketAi = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        if (AiObjectContext* ctx = marketAi->GetAiObjectContext())
        {
            auto* holder = ctx->GetValue<ai::TravelTarget*>("travel target");
            ai::TravelTarget* target = holder ? holder->Get() : nullptr;
            ai::TravelDestination* dest = target ? target->GetDestination() : nullptr;
            if (target && target->IsActive() && dest &&
                typeid(*dest) != typeid(ai::NullTravelDestination))
                tripIsAH = dest->GetPurpose() == ai::TravelDestinationPurpose::AH;
        }
    }
    // Deterministic nearest pick: no urand, so retries aim at the same house.
    ai::GuidPosition bpos(bot);
    if (!bpos.GetFactionTemplateEntry())
        return false;
    AuctioneerPos const* nearest = nullptr;
    float nearestDistSq = 0.0f;
    for (AuctioneerPos const& pos : m_auctioneerPositions)
    {
        if (pos.mapId != bot->GetMapId())
            continue;
        // Faction check: never lift a bot to an auctioneer that is hostile
        // or whose faction is unknown. Fail closed on missing data.
        ai::GuidPosition gpos(HIGHGUID_UNIT, pos.entry);
        if (!gpos.GetFactionTemplateEntry())
            continue;
        if (gpos.IsHostileTo(bot))
            continue;
        float dx = pos.x - bot->GetPositionX();
        float dy = pos.y - bot->GetPositionY();
        float distSq = dx * dx + dy * dy;
        if (!nearest || distSq < nearestDistSq)
        {
            nearest = &pos;
            nearestDistSq = distSq;
        }
    }
    if (!nearest)
        return false;
    float nearestDist = sqrtf(nearestDistSq);
    if (!AhSellerTeleportAllowed(bot->GetMapId(), nearest->mapId, tripIsAH, nearestDist))
        return false;

    float o = nearest->o;
    if (!std::isfinite(o) || o == 0.0f)
        o = bot->GetOrientation();

    if (bot->TeleportTo(nearest->mapId, nearest->x, nearest->y, nearest->z, o, 0))
    {
        TB_LOG_DEBUG("TortoiseBots: AhMarket teleported bot %s to auctioneer %u at map %u %.1f %.1f %.1f",
            bot->GetName(), nearest->entry, nearest->mapId, nearest->x, nearest->y, nearest->z);
        return true;
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

        // The order keep set bypasses usage lists: never hand over the order
        // item, the supply item or a known-recipe reagent.
        if (bot->GetGuildId() &&
            ai::GuildOrderConsumesItem(bot, proto ? proto->ItemId : 0))
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
        TB_LOG_DEBUG("TortoiseBots: AhMarket bot %s posted %s x%u for %u buyout (deposit %u) via auctioneer %s",
            bot->GetName(), proto->Name1.c_str(), count, buyout, deposit, auctioneer->GetName());

        return PostResult::Posted;
    }

    return PostResult::Failed;
}

std::string AhMarketService::GetStatus() const
{
    char const* phaseNames[] = {"Idle", "Buy"};
    std::ostringstream ss;
    ss << "AhMarket Status: Seller=" << (sPlayerbotAIConfig.ahMarketEnabled ? "on" : "off")
       << " OrganicBuyer=" << (sPlayerbotAIConfig.ahMarketBuyer ? "on" : "off")
       << " Phase=" << phaseNames[static_cast<uint8_t>(m_phase)]
       << " Auctioneers=" << m_auctioneerPositions.size()
       << " LastSliceUs=" << m_lastSliceUs
       << " MaxSliceUs=" << m_maxSliceUs
       << " TotalBought=" << m_totalBought;
    return ss.str();
}

uint32_t AhMarketService::CalculateBuyerBaselinePrice(ItemPrototype const* proto) const
{
    if (!proto)
        return 0;
    uint32_t basePrice = ai::AhBuyerBasePrice(proto->BuyPrice, proto->SellPrice, uint32_t(proto->Quality));
    if (!basePrice)
        return 0;
    return ApplyBuyerVariance(basePrice);
}

uint32_t AhMarketService::ApplyBuyerVariance(uint32_t price) const
{
    if (!price || !sPlayerbotAIConfig.ahMarketVariance)
        return price ? price : 1;

    int32 variance = (int32)sPlayerbotAIConfig.ahMarketVariance;
    int32 delta = (int32)urand(0, variance * 2) - variance;
    return ai::AhBuyerVariedPrice(price, delta);
}

bool AhMarketService::BuyAuctionCandidate(AuctionEntry* auction, AuctionHouseObject* ahObject)
{
    if (!auction || !ahObject)
        return false;
    if (auction->expireTime <= time(nullptr))
        return false;
    if (!auction->lockedIpAddress.empty() && auction->depositTime + 300 >= time(nullptr))
        return false;

    if (ai::AhItemSellBlacklisted(auction->itemTemplate, sPlayerbotAIConfig.ahSellBlacklist))
        return false;

    // Players can auction items they created with transmogrification.
    if (sRandomItemMgr.IsTransmogClone(auction->itemTemplate))
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

    // Organic model (issue #405 rework, review 2, NO buyer teleport): bid
    // only with a pool bot ALREADY standing at an auctioneer of the auction's
    // house (arrived on its own feet). One pass, no blind spot (review
    // finding 6): examine == probe (ai::kBuyerExamineCap ==
    // ai::kBuyerProbeCap), so every touched entry is fully probed and
    // m_buyerScanIndex cycles the whole pool over passes.
    size_t poolSize = bots.size();
    size_t start = poolSize ? (m_buyerScanIndex % poolSize) : 0;
    Player* buyer = nullptr;
    ::PlayerbotAI* ai = nullptr;
    Unit* auctioneer = nullptr;
    uint32_t examined = 0;
    for (size_t offset = 0; offset < poolSize && examined < ai::kBuyerExamineCap && !buyer; ++offset)
    {
        size_t idx = (start + offset) % poolSize;
        Player* bot = bots[idx];
        ++examined;
        if (!bot || !IsBotAvailableForMarket(bot))
            continue;
        if (!sRandomBotFacade.IsRandomBot(bot))
            continue;
        // Lease arbitration: never pull a queued/trading/master-claimed bot
        // into a buyer bid. Host guards above stay authoritative.
        if (!BotActivityLeaseManager::Instance().IsAvailableForBackground(bot->GetGUIDLow()))
            continue;
        if (!bot->GetSession() || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported())
            continue;
        // Own auction, same-account auction, already-highest-bidder: the
        // shared AhSyntheticMarketPolicy exclusion rule.
        if (ai::AhBuyerExcluded(auction->owner, auction->ownerAccount, auction->bidder,
                bot->GetGUIDLow(), bot->GetSession()->GetAccountId()))
            continue;
        if (auction->bidder && sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, auction->bidder)) == bot->GetSession()->GetAccountId())
            continue;
        ::PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!botAi)
            continue;
        Unit* nearby = FindNearbyAuctioneer(bot, botAi);
        if (!nearby || !nearby->IsInWorld())
            continue;
        // Bind to the auction's house object: with faction-separated houses
        // the bid handler only sees auctions of the auctioneer's own house.
        // Null-checked: GetAuctionHouseEntry returns null for unmapped
        // factions and GetAuctionsMap dereferences its argument (server
        // crash if passed null; see review-y-405b finding 2).
        AuctionHouseEntry const* nearbyEntry = AuctionHouseMgr::GetAuctionHouseEntry(nearby);
        if (!nearbyEntry)
            continue;
        AuctionHouseObject* nearbyHouse = sAuctionMgr.GetAuctionsMap(nearbyEntry);
        if (!nearbyHouse || nearbyHouse != ahObject)
            continue;
        buyer = bot;
        ai = botAi;
        auctioneer = nearby;
    }
    if (poolSize)
        m_buyerScanIndex = (start + examined) % poolSize;
    if (examined >= poolSize)
        m_buyerScanIndex = 0;
    if (!buyer || !ai || !auctioneer)
        return false;

    uint32 medianMarket = ai::ItemUsageValue::GetAHMedianBuyoutPricePerItem(proto, buyer);
    uint64 willingness = ai::AhBuyerWillingness(CalculateBuyerBaselinePrice(proto), medianMarket, count,
        sPlayerbotAIConfig.ahMarketBuyValue);
    if (!willingness)
        return false;

    uint32 buyout = auction->buyout;
    uint32 curBid = auction->bid;
    uint32 outbid = auction->GetAuctionOutBid();

    uint32 targetPrice = 0;
    if (!ai::AhBuyerTargetPrice(willingness, buyout, curBid, outbid, auction->startbid, targetPrice))
        return false;

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

    BotActivity previousActivity = BotActivityLeaseManager::Instance().GetActivity(buyer->GetGUIDLow());
    if (!BotActivityLeaseManager::Instance().TryAcquire(buyer->GetGUIDLow(), BotActivity::Trading, 120000))
        return false;

    uint32 oldMoney = buyer->GetMoney();
    buyer->GetSession()->HandleAuctionPlaceBid(packet);
    if (buyer->GetMoney() < oldMoney)
        sPlayerbotAIConfig.logEvent(ai, "AhMarketBuy", proto->Name1, std::to_string(proto->ItemId));
    BotActivity restore = previousActivity == BotActivity::Grinding
        ? BotActivity::Grinding : BotActivity::Idle;
    BotActivityLeaseManager::Instance().Release(buyer->GetGUIDLow(), BotActivity::Trading, restore);
    ++m_totalBought;
    TB_LOG_DEBUG("TortoiseBots: AhMarket buyer %s placed %s on auc %u (item %s x%u) for %u",
        buyer->GetName(), (buyout > 0 && targetPrice == buyout) ? "buyout" : "bid",
        auction->Id, proto->Name1.c_str(), count, targetPrice);

    return true;
}

void AhMarketService::StepWorkSlice()
{
    if (m_phase == Phase::Idle && time(nullptr) < m_nextCycleCheck)
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
        if (m_phase == Phase::Idle)
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
            m_actionCycle = (m_actionCycle + 1) % 3;
            if (sPlayerbotAIConfig.ahMarketBuyer && urand(0, 99) < sPlayerbotAIConfig.ahMarketChanceBuy)
            {
                m_scanHouse = m_actionCycle;
                m_scanCursor = 0;
                m_phase = Phase::Buy;
            }
            else
            {
                FinishPass();
            }
            return;
        }
        case Phase::Buy:
            StepBuy();
            return;
    }
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
    if (!auction)
    {
        m_scanCursor = it->first;
        return;
    }
    m_scanCursor = auction->Id;

    BuyAuctionCandidate(auction, ahObject);
}

uint32_t AhMarketService::SellerIntervalMs()
{
    uint32 intervalMs = sPlayerbotAIConfig.ahMarketInterval * 1000;
    if (intervalMs < 1000)
        intervalMs = 1000;
    if (intervalMs > 3600000)
        intervalMs = 3600000;
    return intervalMs;
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

    // 1. Organic buyer scan with work budgets
    if (sPlayerbotAIConfig.ahMarketBuyer)
        StepWorkSlice();

    // 2. Real-inventory seller loop
    uint32 intervalMs = SellerIntervalMs();

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
        // Lease arbitration (issue #89): host guards above stay authoritative.
        // Trading holders stay eligible so a teleported bot keeps its lease
        // across travel ticks until Posted/Failed or the trip-lease timeout.
        BotActivity activity = BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow());
        if (activity != BotActivity::Idle && activity != BotActivity::Grinding && activity != BotActivity::Trading)
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
        uint32_t guidLow = bot->GetGUIDLow();
        BotActivity previousActivity = BotActivityLeaseManager::Instance().GetActivity(guidLow);
        // #404: the trip posts on the NEXT seller tick, so a flat 120 s lease raced the post at
        // the default 120 s tick (29 expiries vs ~1 post). Lease two ticks + margin, bounded.
        uint32_t tripMs = TradingTripLeaseMs(intervalMs);
        if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::Trading, tripMs))
            continue;
        bool allowTeleport = teleported < batch;
        PostResult res = TryPostForBot(bot, allowTeleport);
        ++attempted;
        if (res == PostResult::Posted)
        {
            ++posted;
            BotActivity restore = previousActivity == BotActivity::Grinding
                ? BotActivity::Grinding : BotActivity::Idle;
            BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::Trading, restore);
        }
        else if (res == PostResult::Teleported)
            ++teleported;
        else
        {
            BotActivity restore = previousActivity == BotActivity::Grinding
                ? BotActivity::Grinding : BotActivity::Idle;
            BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::Trading, restore);
        }
    }

    if (posted || teleported)
        m_nextIndex = (start + attempted) % eligible.size();
    else
        m_nextIndex = (start + 1) % eligible.size();

    if (posted || teleported)
        TB_LOG_DEBUG("TortoiseBots: AhMarket tick posted %u/%u teleported %u (eligible %u, auctioneers %u)",
            posted, batch, teleported, (uint32)eligible.size(), (uint32)m_auctioneerPositions.size());
}

} // namespace TortoiseBots
