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

#include <list>
#include <string>
#include <vector>
#include <sstream>

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

        // Price via actual sell multiplier (GetBuyMultiplier/GetSellMultiplier via ItemUsageValue::GetBotSellPrice)
        // Reuses sRandomItemMgr weight indirectly through usage, and sRandomBotFacade multiplier.
        uint32 basePerItem = ai::ItemUsageValue::GetBotSellPrice(proto, bot);
        if (!basePerItem)
            basePerItem = proto->SellPrice ? proto->SellPrice : 1;
        uint32 pct = urand(75, 100);
        uint32 pricePerItem = (basePerItem * pct) / 100;
        if (!pricePerItem)
            pricePerItem = 1;
        uint32 count = item->GetCount();
        if (!count)
            count = 1;
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

void AhMarketService::Update(uint32_t diff)
{
    if (!sPlayerbotAIConfig.ahMarketEnabled)
        return;
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotAutologin)
        return;

    EnsurePositionsLoaded();

    uint32 intervalMs = sPlayerbotAIConfig.ahMarketInterval * 1000;
    if (intervalMs < 1000)
        intervalMs = 1000;
    // Cap interval to avoid overflow and to keep cadence bounded (max 1h).
    if (intervalMs > 3600000)
        intervalMs = 3600000;

    m_elapsedMs += diff;
    if (m_elapsedMs < intervalMs)
        return;
    m_elapsedMs = 0;

    // Respect existing per-bot AhAction mutex via try_lock (no wait).
    if (!sRandomBotFacade.m_ahActionMutex.try_lock())
        return;
    struct UnlockGuard
    {
        std::mutex& m;
        ~UnlockGuard() { m.unlock(); }
    } guard{sRandomBotFacade.m_ahActionMutex};

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
        sLog.outString("TortoiseBots: AhMarket tick posted %u/%u teleported %u (eligible %u, auctioneers %u)", posted, batch, teleported, (uint32)eligible.size(), (uint32)m_auctioneerPositions.size());
}

} // namespace TortoiseBots
