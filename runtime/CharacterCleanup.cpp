#include "CharacterCleanup.h"

#include "../host/ModuleLog.h"

#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStores.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "Mail/Mail.h"

#include <memory>
#include <set>
#include <sstream>

namespace TortoiseBots
{

namespace
{
// Auctions live in the core's in-memory houses and are loaded before this
// module starts. A raw SQL delete would desynchronise them, so everything here
// goes through the same public interfaces the core's own cancel path uses.
// Distinct house objects are collected first: one object serves several
// houseIds, so iterating the DBC rows directly would visit it repeatedly.
//
// The bidder refund mirrors the core's auction-cancel mail semantics: the
// bidder gets the money back, the item belongs to the character being deleted
// and is destroyed with it.
//
// Returns false only when the bid cannot be resolved to an owner; the caller
// must then abort before the listing is touched, because that bid would
// otherwise be lost without a refund. A hardcore bidder is a deliberate
// no-refund (the core's cancel path skips them the same way), not a failure.
bool RefundBidder(AuctionEntry* auction, std::string& error)
{
    ObjectGuid bidderGuid(HIGHGUID_PLAYER, auction->bidder);
    Player* bidder = sObjectMgr.GetPlayer(bidderGuid);
    uint32 bidderAccountId = 0;
    if (!bidder)
        bidderAccountId = sObjectMgr.GetPlayerAccountIdByGUID(bidderGuid);
    if (!bidder && !bidderAccountId)
    {
        error = "auction " + std::to_string(auction->Id) + " holds bid " + std::to_string(auction->bid) +
            " from character " + std::to_string(auction->bidder) + ", which no longer exists";
        return false;
    }

    // Exactly the core's test (Player::IsHardcore for an online bidder, the
    // cached status otherwise, which excludes IMMORTAL and NONE).
    bool hardcore = false;
    if (bidder)
        hardcore = bidder->IsHardcore();
    else if (PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(auction->bidder))
        hardcore = data->uiHardcoreStatus == HARDCORE_MODE_STATUS_ALIVE ||
            data->uiHardcoreStatus == HARDCORE_MODE_STATUS_DEAD ||
            data->uiHardcoreStatus == HARDCORE_MODE_STATUS_HC60;

    if (hardcore)
    {
        TB_LOG_BASIC("TortoiseBots: auction %u bid by hardcore character %u is not refunded (core cancel semantics)",
            auction->Id, auction->bidder);
        return true;
    }

    if (bidder && bidder->GetSession())
        bidder->GetSession()->SendAuctionRemovedNotification(auction);

    std::ostringstream subject;
    subject << auction->itemTemplate << ":0:" << AUCTION_CANCELLED_TO_BIDDER;
    MailDraft(subject.str())
        .SetMoney(auction->bid)
        .SendMailTo(MailReceiver(bidder, bidderGuid), auction, MAIL_CHECK_MASK_COPIED);
    return true;
}
} // namespace

void CollectOwnedAuctions(uint32 guidLow, std::vector<OwnedAuction>& out)
{
    out.clear();
    std::set<AuctionHouseObject*> houses;
    for (uint32 i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
    {
        AuctionHouseEntry const* entry = sAuctionHouseStore.LookupEntry(i);
        if (!entry)
            continue;
        if (AuctionHouseObject* house = sAuctionMgr.GetAuctionsMap(entry))
            houses.insert(house);
    }

    for (AuctionHouseObject* house : houses)
        for (auto const& pair : *house->GetAuctions())
            if (pair.second && pair.second->owner == guidLow)
                out.push_back({ house, pair.second });
}

bool CharacterOwnsAuctions(uint32 guidLow, uint32& count)
{
    std::vector<OwnedAuction> owned;
    CollectOwnedAuctions(guidLow, owned);
    count = static_cast<uint32>(owned.size());
    return count != 0;
}

bool SettleCharacterAuctions(uint32 guidLow, uint32& settled, std::string& error)
{
    std::vector<OwnedAuction> owned;
    CollectOwnedAuctions(guidLow, owned);

    settled = 0;
    uint32 refunded = 0;
    for (OwnedAuction const& entry : owned)
    {
        AuctionEntry* auction = entry.auction;

        if (auction->bidder)
        {
            std::string refundError;
            if (!RefundBidder(auction, refundError))
            {
                error = refundError + "; refusing to delete the listing without a refund";
                return false;
            }
            ++refunded;
        }

        if (Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow))
        {
            item->DeleteFromDB();
            sAuctionMgr.RemoveAItem(auction->itemGuidLow);
            delete item;
        }

        auction->DeleteFromDB();
        entry.house->RemoveAuction(auction);
        delete auction;
        ++settled;
    }

    if (settled)
        TB_LOG_BASIC("TortoiseBots: settled %u auction(s) of character %u (%u bidder refund(s))",
            settled, guidLow, refunded);
    return true;
}

void DeleteCharacterEverywhere(uint32 guidLow, uint32 accountId)
{
    // Group membership, guild/petition membership, pets, items, mail, social
    // and the character rows themselves are the core's job.
    Player::DeleteFromDB(ObjectGuid(HIGHGUID_PLAYER, guidLow), accountId, true, true);

    // Module-owned per-character rows are not part of the core deletion. These
    // are direct (synchronous) executes: callers read the same rows back to
    // verify, and the character database's async queue must not hide them.
    CharacterDatabase.DirectPExecute("DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '%u'", guidLow);
    CharacterDatabase.DirectPExecute("DELETE FROM `ai_playerbot_custom_strategy` WHERE `owner` = '%u'", guidLow);
    CharacterDatabase.DirectPExecute("DELETE FROM `tortoise_bots_owned_character` WHERE `character_guid` = '%u'", guidLow);
    CharacterDatabase.DirectPExecute("DELETE FROM `tortoise_bots_armory_stats` WHERE `guid` = '%u'", guidLow);
    CharacterDatabase.DirectPExecute("DELETE FROM `tortoise_bots_hire` WHERE `character_guid` = '%u'", guidLow);
}

} // namespace TortoiseBots
