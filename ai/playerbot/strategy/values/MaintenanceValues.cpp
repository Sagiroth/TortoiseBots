
#include "MaintenanceValues.h"
#include "NearbyServicePolicy.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/actions/AcceptQuestAction.h"
#include "playerbot/strategy/triggers/RpgTriggers.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/playerbot.h"
#include "playerbot/TravelMgr.h"
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

//An affordable class rank is still unlearned: the partial-purse rule the
//"trainer class" travel trigger uses, minus the journey.
static bool TrainerServiceNeeded(PlayerbotAI* ai)
{
    uint32 minSpellCost = MinTrainableSpellCost(ai);

    if (!minSpellCost)
        return false;

    AiObjectContext* context = ai->GetAiObjectContext();

    return AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells) >= minSpellCost;
}

//Sellable stock plus either the bag-pressure valve or the existing "should
//sell" rule (a real batch has piled up, or the stock funds the next spell
//rank). A vendor errand that already found nothing a vendor buys stays parked
//for a while (ParkVendorErrand), exactly like the trainer half below, so a bot
//cannot walk to the same vendor every five seconds to sell nothing.
static bool VendorServiceNeeded(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!AI_VALUE(bool, "can sell"))
        return false;

    if (AI_VALUE2(time_t, "manual time", "no travel purpose until::" + std::to_string((uint32)TravelDestinationPurpose::Vendor)) > time(0))
        return false;

    return NearbyServiceBagPressure(AI_VALUE(uint8, "bag space")) || AI_VALUE(bool, "should sell");
}

//A finished quest the bot can actually be paid for. A quest that is complete but
//cannot be rewarded (full bags, a money requirement) is left to the travel layer
//and the log upkeep instead of parking the bot at the taker.
bool ai::HasRewardableFinishedQuest(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();

    for (auto& [questId, status] : bot->getQuestStatusMap())
    {
        if (status.m_rewarded || status.m_status != QUEST_STATUS_COMPLETE)
            continue;

        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);

        if (quest && bot->CanRewardQuest(quest, false))
            return true;
    }

    return false;
}

GuidPosition ai::NearbyServiceTarget(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();

    //Random pool bots only, and only while idle: a bot with a player master or a
    //fight on its hands is not waiting for anything.
    if (!sRandomBotFacade.IsRandomBot(bot) || ai->HasRealPlayerMaster())
        return GuidPosition();

    if (!bot->IsAlive() || bot->IsInCombat() || !WorldPosition(bot).isOverworld())
        return GuidPosition();

    //A journey in flight owns the bot; a target it has already reached does not.
    //Only PREPARE/TRAVEL are blocked, so a bot parked at its destination (READY),
    //working there (WORK) or waiting out the cooldown services the NPC next to
    //it instead of standing still until the target expires.
    static_assert((int)TravelStatus::TRAVEL_STATUS_PREPARE == NEARBY_SERVICE_TRAVEL_STATUS_PREPARE,
        "NearbyServicePolicy travel statuses must match TravelStatus");
    static_assert((int)TravelStatus::TRAVEL_STATUS_TRAVEL == NEARBY_SERVICE_TRAVEL_STATUS_TRAVEL,
        "NearbyServicePolicy travel statuses must match TravelStatus");

    if (JourneyInFlightOwnsBot((int)AI_VALUE(TravelTarget*, "travel target")->GetStatus()))
        return GuidPosition();

    if (!AI_VALUE(bool, "can move around"))
        return GuidPosition();

    bool const needsTurnIn = HasRewardableFinishedQuest(ai);
    bool const needsAccept = AI_VALUE(uint8, "free quest log slots") > 0;
    bool const needsVendor = VendorServiceNeeded(ai);
    bool const needsTrainer = TrainerServiceNeeded(ai) &&
        AI_VALUE2(time_t, "manual time", "no travel purpose until::trainer class") <= time(0); //Respect the fruitless-visit park.

    if (!needsTurnIn && !needsAccept && !needsVendor && !needsTrainer)
        return GuidPosition();

    std::vector<GuidPosition> nearby;
    std::vector<NearbyServiceCandidate> candidates;

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "possible rpg targets"))
    {
        GuidPosition guidP(guid, bot->GetMapId(), bot->GetInstanceId());

        if (!guidP.IsCreature())
            continue;

        float const sqDistance = guidP.sqDistance(bot);

        if (sqDistance > NearbyServiceRangeSq())
            continue;

        //Not a service target: a random NPC, a quest giver with nothing to
        //offer, a wrong-class trainer. The order below is the ranking order in
        //NearbyServicePolicy.h, so an NPC that could serve two kinds gets the
        //stronger one.
        NearbyServiceKind kind = NearbyServiceKind::None;
        bool const isQuestGiver = guidP.HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);

        if (needsTurnIn && isQuestGiver && AI_VALUE2(bool, "can turn in quest npc", guidP.GetEntry()))
            kind = NearbyServiceKind::TurnIn;
        else if (needsAccept && isQuestGiver && AI_VALUE2(bool, "can accept quest npc", guidP.GetEntry()) &&
            AcceptAllQuestsAction::OffersAcceptableQuest(ai, bot, guidP.GetWorldObject(bot->GetInstanceId())))
            kind = NearbyServiceKind::Accept;
        else if (needsVendor && guidP.HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
            kind = NearbyServiceKind::Vendor;
        else if (needsTrainer && guidP.HasNpcFlag(UNIT_NPC_FLAG_TRAINER) &&
            RpgTrainTrigger::IsTrainerOf(guidP.GetCreatureTemplate(), bot) &&
            RpgTrainTrigger::TeachesAffordableSpell(ai, guidP, bot))
            kind = NearbyServiceKind::Trainer;

        nearby.push_back(guidP);
        candidates.push_back(NearbyServiceCandidate{ NearbyServiceRankOf(kind), sqDistance });
    }

    int const best = BestNearbyServiceCandidate(candidates);

    return best < 0 ? GuidPosition() : nearby[best];
}

//Is a vendor among the NPCs the bot can reach on foot right now? The same
//"possible rpg targets" set the near-service rule walks, so the two rules
//cannot disagree about what "within reach" means.
static bool VendorWithinNearbyServiceRange(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "possible rpg targets"))
    {
        GuidPosition guidP(guid, bot->GetMapId(), bot->GetInstanceId());

        if (!guidP.IsCreature() || !guidP.HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
            continue;

        if (guidP.sqDistance(bot) <= NearbyServiceRangeSq())
            return true;
    }

    return false;
}

//Bag pressure valve (travel half, issue #379). The near-service rule above only
//covers a vendor within 50 yd; a bot grinding in the field has none, and the
//Vendor travel purpose never fired for the pool at all - measured over 90
//minutes of stage-1 data: 46 % of the bots sat above 80 % bag fill with half
//their bags vendor trash, yet 0 SellAction rows, 0 BuyAction rows and not one
//Vendor travel target, while 279 trainer errands ran. So the bags reach a
//vendor only if something asks for the journey, and this is that ask.
//
//The stock test is `can sell`, the same vendor-usage answer the sell action
//hands over - and the rpg vendor strategy it requires, without which the bot
//would arrive and not sell at all. The old crude test ("any item with a sell
//price") counted the bot's own food and drink, so a bot at the pressure line
//with nothing but rations walked to town to sell nothing; the usage answer had
//to become trustworthy first, see ItemUsageValue's consumable branch (the item
//cheat no longer hides what the bot eats). The ten-minute park after a fruitless
//errand is read here too: the old request ignored it and re-armed on the very
//next tick.
bool ai::BagPressureVendorTrip(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!sRandomBotFacade.IsRandomBot(bot) || ai->HasRealPlayerMaster())
        return false;

    if (!bot->IsAlive() || bot->IsInCombat() || !WorldPosition(bot).isOverworld())
        return false;

    if (!NearbyServiceBagPressure(AI_VALUE(uint8, "bag space")))
        return false;

    if (AI_VALUE2(time_t, "manual time", "no travel purpose until::" + std::to_string((uint32)TravelDestinationPurpose::Vendor)) > time(0))
        return false;

    if (!AI_VALUE(bool, "can sell"))
        return false;

    return !VendorWithinNearbyServiceRange(ai);
}

//Nothing left to eat or drink: no hp food and, for a mana user, no mana drink
//the bot can actually use. Restocking is a real vendor errand only when the bot
//can pay for it, which the caller checks.
static bool OutOfRations(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    bool const usesMana = bot->GetPowerType() == POWER_MANA;
    bool hasFood = false;
    bool hasDrink = false;

    for (Item* item : AI_VALUE2(std::list<Item*>, "inventory items", "inventory"))
    {
        ItemPrototype const* proto = item->GetProto();

        if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE || bot->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;

        if (ItemUsageValue::IsHpFoodOrDrink(proto))
            hasFood = true;
        else if (usesMana && ItemUsageValue::IsManaFoodOrDrink(proto))
            hasDrink = true;
    }

    return !hasFood || (usesMana && !hasDrink);
}

//The real vendor need (issue #379 follow-up). The `should sell` && `can sell`
//pair that ranked the Vendor travel row collapsed, for a masterless pool bot, to
//"holds at least one item the usage classifier calls vendor trash" - `group or
//{should sell, can sell, following party}` is that pair (the bot wanders, so
//"following party" is true) and the row that needed the "free" strategy is dead
//for the pool. What it counted was the bot's own food: the random pool runs with
//RndBotCheats = repair,breath,item, and the consumable decision block was gated
//on `!HasCheat(item)`, so every pool bot's rations fell through to VENDOR. Live
//cycle 4: 2,300 Vendor picks in 65 minutes (4.18 per bot-hour, 112 bots at five
//picks or more, median 78 s between picks for the bots that churned, worst bot
//188), 1,286 of the 4,394 sale rows were the bot's own food and drink, and
//BuyAction never fired once. A journey is now worth it only for stock a vendor
//pays for and that is worth the walk, a durability below the repair threshold,
//or an empty food/drink bag - and never while the purpose is parked after a
//fruitless errand (ParkVendorErrand), which the old request ignored on the very
//next tick.
bool ai::VendorTripNeeded(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    if (AI_VALUE2(time_t, "manual time", "no travel purpose until::" + std::to_string((uint32)TravelDestinationPurpose::Vendor)) > time(0))
        return false;

    if (AI_VALUE(bool, "should repair"))
        return true;

    //A bot with a player master follows the player's journeys: it keeps the
    //loose `should sell` && `can sell` pair it always had - now that `can sell`
    //no longer counts the bot's own rations - and gets no solo food errand.
    if (ai->HasActivePlayerMaster())
        return AI_VALUE(bool, "should sell") && AI_VALUE(bool, "can sell");

    if (SellableStockWorthAVendorTrip(ai))
        return true;

    return OutOfRations(ai) && AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::consumables) > 0;
}

//May the bag-pressure vendor errand start while the bot already has a travel
//target? Only while that target is merely parked at its destination - and only
//for the Vendor purpose, so a relaxed guard cannot let gather/grind requests
//churn targets the way the "no active target" gate was written to prevent.
bool ai::VendorErrandWhileParked(PlayerbotAI* ai, const std::string& qualifier)
{
    if (qualifier != std::to_string((uint32)TravelDestinationPurpose::Vendor))
        return false;

    if (!BagPressureVendorTrip(ai))
        return false;

    AiObjectContext* context = ai->GetAiObjectContext();

    return !JourneyInFlightOwnsBot((int)AI_VALUE(TravelTarget*, "travel target")->GetStatus());
}

void ai::ParkVendorErrand(PlayerbotAI* ai, uint32 minutes)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    std::string const purposeKey = std::to_string((uint32)TravelDestinationPurpose::Vendor);

    SET_AI_VALUE2(bool, "no active travel destinations", purposeKey, true);
    SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purposeKey, time(0) + (time_t)minutes * MINUTE);
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