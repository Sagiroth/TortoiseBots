
#include "MaintenanceValues.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/playerbot.h"
#include "playerbot/WorldPosition.h"

using namespace ai;


//Cheapest green class rank the bot has not learned yet, 0 when the trainer has
//nothing left to teach.
static uint32 MinTrainableSpellCost(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!AI_VALUE2(uint32, "train cost", (uint32)TRAINER_TYPE_CLASS)) //Has nothing to train
        return 0;

    uint32 minSpellCost = UINT32_MAX;
    for (TrainerSpell const* trainable : AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", TRAINER_TYPE_CLASS))
        if (trainable && trainable->spellCost < minSpellCost)
            minSpellCost = trainable->spellCost;

    return minSpellCost == UINT32_MAX ? 0 : minSpellCost;
}

//Mirrors the partial-purse rule in RequestNamedTravelTargetAction ("trainer
//class"): the cheapest green class rank must fit free money for spells. What
//the purse is missing for that rank, 0 when nothing is missing.
static uint32 SpellMoneyMissing(PlayerbotAI* ai)
{
    uint32 minSpellCost = MinTrainableSpellCost(ai);

    if (!minSpellCost)
        return 0;

    AiObjectContext* context = ai->GetAiObjectContext();
    uint32 freeMoney = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells);

    return freeMoney < minSpellCost ? minSpellCost - freeMoney : 0;
}

bool ShouldSellValue::CantAffordNextSpell(PlayerbotAI* ai)
{
    return SpellMoneyMissing(ai) > 0;
}

//A broke bot that picks up one grey pelt is not a reason to leave the grind
//spot: it sells the pelt, is still broke, and walks back. So a small stock is
//only worth the walk when it actually buys the missing spell rank. A real
//batch is worth the walk on its own: the bags are filling anyway, and a bot
//with nothing left to train (a fresh level 1 has no green class rank yet)
//must still turn its loot into coin instead of hoarding it until the bags are
//full.
bool ai::SellableStockWorthAVendorTrip(PlayerbotAI* ai)
{
    //A vendor trip is an overworld errand: never worth it in the middle of a
    //run, and never worth overruling an explicit player master.
    if (!WorldPosition(ai->GetBot()).isOverworld() || ai->HasActivePlayerMaster())
        return false;

    AiObjectContext* context = ai->GetAiObjectContext();

    uint32 sellValue = 0;
    uint32 sellableCount = 0;
    for (Item* item : AI_VALUE2(std::list<Item*>, "inventory items", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_VENDOR)))
    {
        ItemPrototype const* proto = item->GetProto();

        if (!proto || !proto->SellPrice)
            continue;

        sellValue += proto->SellPrice * item->GetCount();
        sellableCount += item->GetCount();
    }

    if (!sellableCount)
        return false;

    //The batch thresholds scale down below level 5: a fresh bot's stock is one or
    //two greys (eight items and 60% bags are unreachable before level 5), so the
    //LowLevel* pair is what makes the batch rule fire for the starting pool.
    bool const beginner = ai->GetBot()->GetLevel() < 5;
    uint32 const minBatchCount = beginner ? sPlayerbotAIConfig.lowLevelVendorBatchMinCount : sPlayerbotAIConfig.vendorBatchMinCount;
    uint32 const minBatchBagSpace = beginner ? sPlayerbotAIConfig.lowLevelVendorBatchMinBagSpace : sPlayerbotAIConfig.vendorBatchMinBagSpace;

    if (sellableCount >= minBatchCount && AI_VALUE(uint8, "bag space") >= minBatchBagSpace)
        return true; //A real batch has piled up; the bags are filling anyway.

    uint32 moneyMissing = SpellMoneyMissing(ai);

    return moneyMissing > 0 && sellValue >= moneyMissing; //Selling this stock buys the spell.
}

//A member that is about to leave the party for a vendor must not be dragged
//into an elite or boss fight. Bag pressure is the only veto: "should sell" is
//also true for a broke bot that merely plans to liquidate its stock at the
//next vendor, and one such member must never freeze the whole party.
bool ShouldSellValue::GroupMemberLeavingForVendor(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!WorldPosition(ai->GetBot()).isOverworld()) //A dungeon run cannot detour to a vendor.
        return false;

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player || !ai->IsSafe(player))
            continue;

        if (!PlayerbotAIStorage::Instance().GetAI(player))
            continue;

        if (PAI_VALUE(uint8, "bag space") > 80 && PAI_VALUE(bool, "can sell"))
            return true;
    }

    return false;
}

bool ShouldAHSellValue::Calculate()
{
    if (ShouldSellValue::Calculate()) //We need space so we want to try to AH items anyway.
        return true;

    std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", "inventory");

    for (auto& item : items)
    {
        if (!item->GetUInt32Value(ITEM_FIELD_DURABILITY)) //Does the item need to be repaired?
            continue;

        uint32 maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);

        ItemPrototype const* ditemProto = item->GetProto();

        DurabilityCostsEntry const* dcost = sDurabilityCostsStore.LookupEntry(ditemProto->ItemLevel);
        if (!dcost)
            continue;

        uint32 dQualitymodEntryId = (ditemProto->Quality + 1) * 2;
        DurabilityQualityEntry const* dQualitymodEntry = sDurabilityQualityStore.LookupEntry(dQualitymodEntryId);
        if (!dQualitymodEntry)
            continue;

        uint32 dmultiplier = dcost->multiplier[ItemSubClassToDurabilityMultiplierId(ditemProto->Class, ditemProto->SubClass)];
        uint32 costs = uint32(maxDurability * dmultiplier * double(dQualitymodEntry->quality_mod));

        if (bot->GetMoney() && (costs * 100) / bot->GetMoney() <= 1) //Would repairing this item use more than 1% of our current gold?
            continue;

        if (!WorldPosition(bot).HasAreaFlag(AREA_FLAG_CAPITAL)) //We are not in a city so not easy to repair now.

        if (bot->GetMoney() && (costs * 100) / bot->GetMoney() <= 10) //Would repairing this item use more than 10% of our current gold?
                continue;

        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());

        if (usage != ItemUsage::ITEM_USAGE_AH && usage != ItemUsage::ITEM_USAGE_BROKEN_AH) //Do we want to AH this item?
            continue;

        return true; //We have an item that can be damaged (and gives significant repair cost) that we want to auction. We should auction it now!
    }

    return false;
}


bool CanGetMailValue::Calculate() {
    if (!ai->HasStrategy("rpg vendor", BotState::BOT_STATE_NON_COMBAT))
        return false;

    if (AI_VALUE(bool, "should sell"))
        return false;

    time_t cur_time = time(0);
    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master)
        return false;

    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        if ((*itr)->state == MAIL_STATE_DELETED || cur_time < (*itr)->deliver_time)
            continue;

        if ((*itr)->has_items || (*itr)->money)
        {
            return true;
        }
    }

    return false;
}

bool ShouldGetMailValue::Calculate() {
    time_t cur_time = time(0);

    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master)
        return false;

    bool hasGuildShareList = !AI_VALUE(std::vector<GuildShareItemEntry>, "guild share list").empty();

    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        if ((*itr)->state == MAIL_STATE_DELETED || cur_time < (*itr)->deliver_time)
            continue;

        int32 waitingInBoxTime = cur_time - (*itr)->deliver_time;

        if (!hasGuildShareList && waitingInBoxTime < HOUR) //Let mail sit in the inbox for atleast 1 hour
        {
            return false;
        }

        if ((*itr)->has_items && waitingInBoxTime > HOUR * 4) //Items are allowed to sit in the mail for max 4 hours.
        {
            return true;
        }

        if (hasGuildShareList && (*itr)->has_items && (*itr)->stationery == MAIL_STATIONERY_AUCTION && waitingInBoxTime > MINUTE) //If bot has guild share list and mail is from AH, take mail immediately.
        {
            return true;
        }

        if ((*itr)->money && AI_VALUE(bool, "should get money")) //We need money so we should get it.
        {
            return true;
        }
    }

    return false;
}