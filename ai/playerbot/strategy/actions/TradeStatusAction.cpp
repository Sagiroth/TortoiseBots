
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "TradeStatusAction.h"

#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/ServerFacade.h"
#include "runtime/AltBotService.h"
#include "../../runtime/HireLifecycle.h"
#include "../../runtime/PoolBotTradePolicy.h"
#include "playerbot/strategy/values/CraftValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "SetCraftAction.h"
#include "playerbot/policy/guild/EnchantTradePolicy.h"
#include "playerbot/GuildOrderEnchants.h"
#include "playerbot/GuildOrderCraft.h"
#include "runtime/EnchantCapital.h"
#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderEnchantLifecyclePolicy.h"
#include "runtime/GuildOrderEnchantPolicy.h"
#include "runtime/GuildOrderMeetPolicy.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/GuildOrderNeedPolicy.h"

using namespace ai;

bool TradeStatusAction::Execute(Event& event)
{
    Player* trader = bot->GetTrader();
    Player* master = GetMaster();
    if (!trader)
        return false;

    // Issue #469: pool-bot trade safety (donor EnableRandomBotTrading
    // parity). A masterless pool bot refuses a stranger's trade window at
    // the earliest point, before listing inventory or sharing conjured
    // goods. Owned/hired bots (live master, owner, or hire record) and
    // bot-to-bot RPG trades pass through untouched.
    if (!ai->HasRealPlayerMaster() && sRandomBotFacade.IsFreeBot(bot) &&
        !TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) &&
        isRealPlayer_Helper(trader) && !PoolBotMayTradeWith(trader))
    {
        ai->TellPlayer(trader, "I don't trade with strangers.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        WorldPacket p;
        uint32 status = 0;
        p << status;
        bot->GetSession()->HandleCancelTradeOpcode(p);
        return false;
    }
    // Issue #872 (B8): an autonomous alt never trades with a stranger. The
    // owner (or a GM) trades as before; a stranger's window is cancelled at
    // the earliest point, like the pool gate above.
    if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()) &&
        isRealPlayer_Helper(trader) && !ai->IsBotOwnerOrGm(*trader))
    {
        ai->TellPlayer(trader, "I don't trade with strangers.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        WorldPacket cancelTrade;
        uint32 cancelStatus = 0;
        cancelTrade << cancelStatus;
        bot->GetSession()->HandleCancelTradeOpcode(cancelTrade);
        return false;
    }

    bool shouldTrade = true;
    if (!PlayerbotAIStorage::Instance().GetAI(trader))
    {
        shouldTrade = false;
        // Issue #872 (B8): an autonomous alt trades only with its owner (or
        // a GM). Group membership must not open the window: a stranger's
        // group is exactly how they reach the alt.
        if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
            shouldTrade = ai->IsBotOwnerOrGm(*trader);
        else if (trader == master || IsInGroup_Helper(bot, trader))
        {
            shouldTrade = ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false, trader);
        }
        // Issue #469 reconciliation: the pool gate above already approved
        // this trader (open mode 2/3/4, ungrouped owner, or hire master),
        // but the legacy block only knows master/group. Without this the
        // approval is dead: the window would be cancelled right here.
        if (!shouldTrade && !ai->HasRealPlayerMaster() && sRandomBotFacade.IsFreeBot(bot) &&
            !TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) &&
            isRealPlayer_Helper(trader) && PoolBotMayTradeWith(trader))
        {
            shouldTrade = true;
        }
    }

    if (!shouldTrade)
    {
        // Enchant rows bypass the master/group gate: the hook owns the
        // packet (wrong-player cancel, trade checks, done) and returns
        // true when it ran. Anything else falls into the cancel below.
        if (HandleEnchantOrderTrade(event, trader))
            return true;
        WorldPacket noTrade;
        uint32 noTradeStatus = 0;
        noTrade << noTradeStatus;
        bot->GetSession()->HandleCancelTradeOpcode(noTrade);
        return false;
    }

    WorldPacket p(event.GetPacket());

    p.rpos(0);
    uint32 status;
    p >> status;

    if (status == TRADE_STATUS_TRADE_ACCEPT || (status == TRADE_STATUS_BACK_TO_TRADE && trader->GetTradeData() && trader->GetTradeData()->IsAccepted()))
    {
        WorldPacket p;
        uint32 status = 0;
        p << status;

        uint32 discount = sRandomBotFacade.GetTradeDiscount(bot, trader);
        if (CheckTrade())
        {
            int32 botMoney = CalculateCost(bot, true);

            std::map<uint32, uint32> givenItemIds, takenItemIds;
            for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
            {
                Item* item = trader->GetTradeData()->GetItem((TradeSlots)slot);
                if (item)
                    givenItemIds[item->GetProto()->ItemId] += item->GetCount();

                item = bot->GetTradeData()->GetItem((TradeSlots)slot);
                if (item)
                    takenItemIds[item->GetProto()->ItemId] += item->GetCount();
            }

            bot->GetSession()->HandleAcceptTradeOpcode(p);

            if (bot->GetTradeData())
            {
                sRandomBotFacade.SetTradeDiscount(bot, trader, discount);
                return false;
            }

            for (std::map<uint32, uint32>::iterator i = givenItemIds.begin(); i != givenItemIds.end(); ++i)
            {
                uint32 itemId = i->first;
                uint32 count = i->second;

                CraftData &craftData = AI_VALUE(CraftData&, "craft");
                if (!craftData.IsEmpty() && craftData.IsRequired(itemId))
                {
                    craftData.AddObtained(itemId, count);
                }
            }

            for (std::map<uint32, uint32>::iterator i = takenItemIds.begin(); i != takenItemIds.end(); ++i)
            {
                uint32 itemId = i->first;
                uint32 count = i->second;

                CraftData &craftData = AI_VALUE(CraftData&, "craft");
                if (!craftData.IsEmpty() && craftData.itemId == itemId)
                {
                    craftData.Crafted(count);
                }
            }

            return true;
        }
    }
    else if (status == TRADE_STATUS_BEGIN_TRADE)
    {
        if (!sServerFacade.isInFront(bot, trader, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
            sServerFacade.SetFacingTo(bot, trader);

        BeginTrade();

        return true;
    }

    return false;
}

void TradeStatusAction::BeginTrade()
{
    Player* trader = bot->GetTrader();
    if (!trader || PlayerbotAIStorage::Instance().GetAI(trader))
        return;

    WorldPacket p;
    bot->GetSession()->HandleBeginTradeOpcode(p);

    ListItemsVisitor visitor;
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);

    ai->TellPlayer(trader, "=== Inventory ===");
    ai->InventoryTellItems(trader, visitor.items, visitor.soulbound);

    if (sRandomBotFacade.IsRandomBot(bot))
    {
        uint32 discount = sRandomBotFacade.GetTradeDiscount(bot, ai->GetMaster());
        if (discount)
        {
            std::ostringstream out; out << "Discount up to: " << chat->formatMoney(discount);
            ai->TellPlayer(trader, out);
        }
    }

    AutoShareConjured(trader);
}

void TradeStatusAction::AutoShareConjured(Player* trader)
{
    if (!sPlayerbotAIConfig.autoShareConjuredOnTrade || !trader || !bot->GetTradeData())
        return;
    if (bot->GetClass() != CLASS_MAGE && bot->GetClass() != CLASS_WARLOCK)
        return;

    auto placeInTrade = [&](Item* item) -> bool
    {
        if (!item || item->IsInTrade() || !bot->GetTrader())
            return false;
        TradeData* trade = bot->GetTradeData();
        int8 tradeSlot = -1;
        for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT && tradeSlot == -1; ++i)
            if (trade->GetItem(TradeSlots(i)) == NULL)
                tradeSlot = static_cast<int8>(i);
        if (tradeSlot == -1)
            return false;
        WorldPacket packet(CMSG_SET_TRADE_ITEM, 3);
        packet << static_cast<uint8>(tradeSlot) << static_cast<uint8>(item->GetBagSlot()) << static_cast<uint8>(item->GetSlot());
        bot->GetSession()->HandleSetTradeItemOpcode(packet);
        return true;
    };

    auto firstConjured = [&](uint32 spellCategory) -> Item*
    {
        FindFoodVisitor visitor(bot, spellCategory, true);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        std::list<Item*>& found = visitor.GetResult();
        return found.empty() ? nullptr : found.front();
    };

    bool placed = false;
    if (bot->GetClass() == CLASS_MAGE)
    {
        if (Item* food = firstConjured(11))
            placed = placeInTrade(food) || placed;
        if (trader->GetPowerType() == POWER_MANA)
            if (Item* water = firstConjured(59))
                placed = placeInTrade(water) || placed;
    }
    else if (bot->GetClass() == CLASS_WARLOCK)
    {
        uint32 stoneId = 0;
        uint32 level = trader->GetLevel();
        if (level < 12) stoneId = 5512;
        else if (level < 24) stoneId = 5511;
        else if (level < 36) stoneId = 5509;
        else if (level < 48) stoneId = 5510;
        else stoneId = 9421;
        if (trader->HasItemCount(stoneId, 1))
            return;
        FindItemByIdVisitor visitor(stoneId);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        std::list<Item*>& found = visitor.GetResult();
        if (!found.empty())
            placed = placeInTrade(found.front()) || placed;
    }
    (void)placed;
}

bool TradeStatusAction::CheckTrade()
{
    Player* trader = bot->GetTrader();
    if (!bot->GetTradeData() || !trader || !trader->GetTradeData())
        return false;

    if (!ai->HasActivePlayerMaster() && PlayerbotAIStorage::Instance().GetAI(bot->GetTrader()))
    {
        bool isGivingItem = false;
        for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
        {
            Item* item = bot->GetTradeData()->GetItem((TradeSlots)slot);
            if (item)
            {
                isGivingItem = true;
                break;
            }
        }

        bool isGettingItem = false;
        for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
        {
            Item* item = trader->GetTradeData()->GetItem((TradeSlots)slot);
            if (item)
            {
                isGettingItem = true;
                break;
            }
        }

        if (!isGettingItem) //Enchanting an item.
            isGettingItem = (bot->GetTradeData()->GetItem(TRADE_SLOT_NONTRADED) && trader->GetTradeData()->GetSpell());

        if (isGettingItem)
        {
            std::string name = trader->GetName();
            if (bot->GetGroup() && bot->GetGroup()->IsMember(bot->GetTrader()->getObjectGuid()) && ai->HasRealPlayerMaster())
                ai->TellPlayerNoFacing(trader, "Thank you " + name + ".", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
        return isGettingItem;
    }

    if (!sRandomBotFacade.IsRandomBot(bot))
    {
        int32 botItemsMoney = CalculateCost(bot, true);
        int32 botMoney = bot->GetTradeData()->GetMoney() + botItemsMoney;
        int32 playerItemsMoney = CalculateCost(trader, false);
        int32 playerMoney = trader->GetTradeData()->GetMoney() + playerItemsMoney;
        if (playerMoney || botMoney)
        {
            ai->PlaySound(playerMoney < botMoney ? TEXTEMOTE_SIGH : TEXTEMOTE_THANK);
        }

        return true;
    }

    // Issue #469: buy-only / sell-only settle (donor modes 2/3 parity).
    // The inbound gate above already refused strangers in off/trusted
    // modes; this stops value flowing the wrong way when the window is
    // open (trusted partner, group invite race, or an open mode).
    if (!ai->HasRealPlayerMaster() && sRandomBotFacade.IsRandomBot(bot) &&
        !TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) &&
        isRealPlayer_Helper(trader) &&
        !(trader == GetMaster() || IsInGroup_Helper(bot, trader) || ai->IsBotOwnerOrGm(*trader) ||
          TortoiseBots::HireLifecycle::Instance().GetMaster(bot->GetObjectGuid()) == trader->GetObjectGuid()))
    {
        // Strangers only: an open mode lets the window open (PoolBotMayTradeWith),
        // but buy-only / sell-only still decide which way value may flow here.
        bool botGives = bot->GetTradeData()->GetMoney() != 0;
        bool playerGives = trader->GetTradeData()->GetMoney() != 0;
        for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT && !(botGives && playerGives); ++slot)
        {
            if (bot->GetTradeData()->GetItem((TradeSlots)slot))
                botGives = true;
            if (trader->GetTradeData()->GetItem((TradeSlots)slot))
                playerGives = true;
        }
        if (!TortoiseBots::PoolBotTradeSettleAllowed(
                TortoiseBots::ParsePoolBotTradeMode(sPlayerbotAIConfig.poolBotTradeMode),
                true, botGives, playerGives))
        {
            ai->TellPlayer(trader, "I can't complete that trade.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            ai->PlaySound(TEXTEMOTE_NO);
            return false;
        }
    }

    int32 botItemsMoney = CalculateCost(bot, true);
    int32 botMoney = bot->GetTradeData()->GetMoney() + botItemsMoney;
    int32 playerItemsMoney = CalculateCost(trader, false);
    int32 playerMoney = trader->GetTradeData()->GetMoney() + playerItemsMoney;

    for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
    {
        Item* item = bot->GetTradeData()->GetItem((TradeSlots)slot);
        if (item && !ItemUsageValue::GetBotSellPrice(item->GetProto(), bot))
        {
            std::ostringstream out;
            out << chat->formatItem(item) << " - This is not for sale";
            ai->TellPlayer(trader, out);
            ai->PlaySound(TEXTEMOTE_NO);
            return false;
        }

        item = trader->GetTradeData()->GetItem((TradeSlots)slot);
        if (item)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
            if ((botMoney && !ItemUsageValue::GetBotBuyPrice(item->GetProto(), bot)) || usage == ItemUsage::ITEM_USAGE_NONE)
            {
                std::ostringstream out;
                out << chat->formatItem(item) << " - I don't need this";
                ai->TellPlayer(trader, out);
                ai->PlaySound(TEXTEMOTE_NO);
                return false;
            }
        }
    }

    if (!botMoney && !playerMoney)
        return true;

    if (!botItemsMoney && !playerItemsMoney)
    {
        ai->TellError(trader, "There are no items to trade");
        return false;
    }

    int32 discount = (int32)sRandomBotFacade.GetTradeDiscount(bot, trader);
    int32 delta = playerMoney - botMoney;
    int32 moneyDelta = (int32)trader->GetTradeData()->GetMoney() - (int32)bot->GetTradeData()->GetMoney();
    bool success = false;
    if (delta < 0)
    {
        if (delta + discount >= 0)
        {
            if (moneyDelta < 0)
            {
                ai->TellPlayer(trader, "You can use discount to buy items only");
                ai->PlaySound(TEXTEMOTE_NO);
                return false;
            }
            success = true;
        }
    }
    else
    {
        success = true;
    }

    if (success)
    {
        sRandomBotFacade.AddTradeDiscount(bot, trader, delta);
        switch (urand(0, 4)) {
        case 0:
            ai->TellPlayer(trader, "A pleasure doing business with you");
            break;
        case 1:
            ai->TellPlayer(trader, "Fair trade");
            break;
        case 2:
            ai->TellPlayer(trader, "Thanks");
            break;
        case 3:
            ai->TellPlayer(trader, "Off with you");
            break;
        }
        ai->PlaySound(TEXTEMOTE_THANK);
        return true;
    }

    std::ostringstream out;
    out << "I want " << chat->formatMoney(-(delta + discount)) << " for this";
    ai->TellPlayer(trader, out);
    ai->PlaySound(TEXTEMOTE_NO);
    return false;
}

int32 TradeStatusAction::CalculateCost(Player* player, bool sell)
{
    Player* trader = bot->GetTrader();
    TradeData* data = player->GetTradeData();
    if (!data)
        return 0;

    uint32 sum = 0;
    for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
    {
        Item* item = data->GetItem((TradeSlots)slot);
        if (!item)
            continue;

        ItemPrototype const* proto = item->GetProto();
        if (!proto)
            continue;

        if (proto->Quality < ITEM_QUALITY_NORMAL)
            return 0;

        CraftData &craftData = AI_VALUE(CraftData&, "craft");
        if (!craftData.IsEmpty())
        {
            if (player == trader && !sell && craftData.IsRequired(proto->ItemId))
            {
                continue;
            }

            if (player == bot && sell && craftData.itemId == proto->ItemId && craftData.IsFulfilled())
            {
                sum += item->GetCount() * SetCraftAction::GetCraftFee(craftData);
                continue;
            }
        }

        if (sell)
        {
            sum += item->GetCount() * ItemUsageValue::GetBotSellPrice(proto, bot);
        }
        else
        {
            sum += item->GetCount() * ItemUsageValue::GetBotBuyPrice(proto, bot);
        }
    }

    return sum;
}

bool TradeStatusAction::PoolBotMayTradeWith(Player* trader)
{
    // Owned/hired bots never reach the gate (the callers check first), so
    // a live master, same-account owner, or hire record counts as trusted.
    // Anything else is a stranger unless the mode opens the window.
    if (!trader || trader == GetMaster() || IsInGroup_Helper(bot, trader))
        return true;
    if (ai->IsBotOwnerOrGm(*trader))
        return true;
    if (TortoiseBots::HireLifecycle::Instance().GetMaster(bot->GetObjectGuid()) == trader->GetObjectGuid())
        return true;
    return TortoiseBots::PoolBotTradeAllowed(
        TortoiseBots::ParsePoolBotTradeMode(sPlayerbotAIConfig.poolBotTradeMode),
        true, false, false, false);
}

namespace
{
// Live window snapshot for one enchant-trade verdict: reads the trader's
// normal slots and gold, the parked non-traded item, and the catalogue fit.
bool EnchantHookWindowFacts(Player* bot, Player* trader, uint32_t spellId,
    ai::EnchantTradeState& tradeState, std::string& currentEnchant)
{
    tradeState.traderIsGiver = false;
    tradeState.hasItem = false;
    tradeState.fitsEnchant = false;
    tradeState.hasPermanentEnchant = false;
    tradeState.replaceConfirmed = false;
    tradeState.traderExtras = false;
    currentEnchant.clear();
    TradeData* ours = bot ? bot->GetTradeData() : nullptr;
    TradeData* theirs = trader ? trader->GetTradeData() : nullptr;
    if (!ours || !theirs)
        return false;
    std::optional<TortoiseBots::GuildOrders::OrderRecord> row =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!row || row->giverGuidLow == 0)
        return false;
    tradeState.traderIsGiver = trader->getObjectGuid() ==
        ObjectGuid(HIGHGUID_PLAYER, row->giverGuidLow);
    tradeState.traderExtras = theirs->GetMoney() != 0;
    for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT && !tradeState.traderExtras; ++slot)
        if (theirs->GetItem(static_cast<TradeSlots>(slot)))
            tradeState.traderExtras = true;
    Item* parked = theirs->GetItem(TRADE_SLOT_NONTRADED);
    tradeState.hasItem = parked != nullptr;
    if (!parked || !parked->GetProto())
        return true;
    ItemPrototype const* proto = parked->GetProto();
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
    if (!spell)
        return true;
    TortoiseBots::GuildOrders::EnchantTargetFacts facts;
    facts.equippedItemClass = spell->EquippedItemClass;
    facts.subClassMask = spell->EquippedItemSubClassMask;
    facts.invTypeMask = spell->EquippedItemInventoryTypeMask;
    facts.itemTarget = true;
    facts.spellId = spellId;
    TortoiseBots::GuildOrders::EnchantTargetItem item;
    item.itemClass = proto->Class;
    item.subClass = proto->SubClass;
    item.inventoryType = proto->InventoryType;
    tradeState.fitsEnchant = TortoiseBots::GuildOrders::EnchantFitsItem(facts, item);
    uint32_t parkedEnchant = parked->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
    tradeState.hasPermanentEnchant = parkedEnchant != 0;
    if (parkedEnchant)
        currentEnchant = "that enchant";
    return true;
}
} // namespace

bool TradeStatusAction::HandleEnchantOrderTrade(Event& event, Player* trader)
{
    if (!bot || !trader)
        return false;
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->type != TortoiseBots::GuildOrders::OrderType::Enchant ||
        stored->cancelled || TortoiseBots::GuildOrders::IsFinished(stored->state) ||
        (stored->state != TortoiseBots::GuildOrders::OrderState::Coming &&
            stored->state != TortoiseBots::GuildOrders::OrderState::Enchanting))
        return false;
    TortoiseBots::GuildOrders::OrderRecord order = *stored;
    ObjectGuid giverGuid(HIGHGUID_PLAYER, order.giverGuidLow);
    bool open = bot->GetTradeData() != nullptr && trader->GetTradeData() != nullptr;

    WorldPacket packet(event.GetPacket());
    uint32_t status = 0;
    if (!packet.empty())
    {
        packet.rpos(0);
        packet >> status;
    }

    // A trade with anyone but the giver never touches the row's window:
    // refuse with the reason whisper and cancel. Covers a stranger opening
    // a trade with the bot while Coming/Enchanting.
    if (open && trader->getObjectGuid() != giverGuid)
    {
        ai->TellPlayerNoFacing(trader, ai::kEnchantWrongPlayerText,
            PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        WorldPacket cancel;
        uint32 cancelStatus = 0;
        cancel << cancelStatus;
        bot->GetSession()->HandleCancelTradeOpcode(cancel);
        return true;
    }



    // Cancelled/closed without completing: back to Coming so the walk
    // re-opens the trade. The session closes: the next window opens a new
    // try, and the replace answer resets with it.
    if (!open ||
        status == TRADE_STATUS_TRADE_CANCELED || status == TRADE_STATUS_CLOSE_WINDOW ||
        status == TRADE_STATUS_TRADE_REJECTED)
    {
        if (order.state == TortoiseBots::GuildOrders::OrderState::Enchanting)
            TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(),
                TortoiseBots::GuildOrders::OrderState::Coming, TortoiseBots::GuildOrders::OrderReason::None);
        TortoiseBots::GuildOrders::NoteEnchantTradeClosed(bot->GetGUIDLow());
        return open || status != 0;
    }

    // A materials trade while any reagent is still short in the bags: the
    // order state no longer says so once Meet moved it to Coming.
    bool needMaterials = false;
    for (ai::GuildOrderEnchantMaterial const& reagent : ai::GuildOrderEnchantReagents(order.itemId))
        if (bot->GetItemCount(reagent.itemId) < reagent.count)
            needMaterials = true;
    if (order.state == TortoiseBots::GuildOrders::OrderState::Coming)
    {
        TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(),
            TortoiseBots::GuildOrders::OrderState::Enchanting, TortoiseBots::GuildOrders::OrderReason::None);
        TortoiseBots::GuildOrders::NoteEnchantTradeOpened(bot->GetGUIDLow(), WorldTimer::getMSTime());
    }
    else if (order.state == TortoiseBots::GuildOrders::OrderState::Enchanting)
    {
        // First update of a giver-opened window: no open was tracked yet.
        TortoiseBots::GuildOrders::EnchantTradeSession session =
            TortoiseBots::GuildOrders::GetEnchantTrade(bot->GetGUIDLow());
        if (session.openedMs == 0)
            TortoiseBots::GuildOrders::NoteEnchantTradeOpened(bot->GetGUIDLow(), WorldTimer::getMSTime());
    }

    // Window updates run the per-trade verdicts. The materials trade owns
    // the row while reagents are still missing; else the enchant trade.
    if (needMaterials)
    {
        ai::MaterialsTradeState materials;
        for (ai::GuildOrderEnchantMaterial const& reagent : ai::GuildOrderEnchantReagents(order.itemId))
        {
            uint32_t have = bot->GetItemCount(reagent.itemId);
            uint32_t need = reagent.count > have ? reagent.count - have : 0;
            materials.allowed.push_back(ai::EnchantReagentNeed{ reagent.itemId, need });
        }
        TradeData* theirs = trader->GetTradeData();
        for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
            if (Item* offered = theirs->GetItem(static_cast<TradeSlots>(slot)))
                materials.offered.push_back(ai::OfferedStack{ offered->GetProto()->ItemId, offered->GetCount() });
        materials.traderGold = theirs->GetMoney();
        ai::MaterialsTradeResult checked = ai::CheckMaterialsTrade(materials);
        if (checked.verdict == ai::MaterialsTradeVerdict::NotMaterial)
        {
            ai->TellPlayerNoFacing(trader, ai::kMaterialsNotMaterialText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            WorldPacket cancel;
            uint32 cancelStatus = 0;
            cancel << cancelStatus;
            bot->GetSession()->HandleCancelTradeOpcode(cancel);
            return true;
        }
        if (checked.verdict == ai::MaterialsTradeVerdict::NoGold)
        {
            ai->TellPlayerNoFacing(trader, ai::kMaterialsNoGoldText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            WorldPacket cancel;
            uint32 cancelStatus = 0;
            cancel << cancelStatus;
            bot->GetSession()->HandleCancelTradeOpcode(cancel);
            return true;
        }
        // All materials offered and the giver accepted: accept, then count
        // the completion straight into the ready check. The window is gone
        // after the accept, so the check below runs on the live bags.
        if (theirs->IsAccepted())
        {
            WorldPacket accept;
            uint32 acceptStatus = TRADE_STATUS_TRADE_ACCEPT;
            accept << acceptStatus;
            bot->GetSession()->HandleAcceptTradeOpcode(accept);
            if (!bot->GetTrader())
            {
                TortoiseBots::GuildOrders::NoteSupplyProgress(bot->GetGUIDLow());
                TortoiseBots::GuildOrders::NoteEnchantTradeClosed(bot->GetGUIDLow());
                // The refresh runs on the bot's map thread with the spell
                // map readable: complete goes to the enchant trade at once,
                // still short stays Waiting needitems.
                std::optional<TortoiseBots::GuildOrders::OrderRecord> after =
                    TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
                if (after)
                {
                    bool complete = true;
                    for (ai::GuildOrderEnchantMaterial const& reagent :
                        ai::GuildOrderEnchantReagents(after->itemId))
                        if (bot->GetItemCount(reagent.itemId) < reagent.count)
                        {
                            complete = false;
                            break;
                        }
                    TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(),
                        complete ? TortoiseBots::GuildOrders::OrderState::Enchanting :
                            TortoiseBots::GuildOrders::OrderState::Waiting,
                        complete ? TortoiseBots::GuildOrders::OrderReason::None :
                            TortoiseBots::GuildOrders::OrderReason::NeedItems);
                }
            }
            return true;
        }
        return true;
    }

    TortoiseBots::GuildOrders::EnchantTradeSession session =
        TortoiseBots::GuildOrders::GetEnchantTrade(bot->GetGUIDLow());
    uint32_t nowMs = WorldTimer::getMSTime();

    // Try budget: an idle window past the deadline cancels and re-opens
    // (next try), or gives up after the last one: one whisper, meet state
    // back to Ready/Waiting. A trader mid-placement holds the window one
    // more deadline through the policy's traderActive input.
    bool traderActive = trader->GetTradeData() && trader->GetTradeData()->IsAccepted();
    switch (ai::CheckTradeTry(nowMs, session.openedMs, session.tries ? session.tries : 1, traderActive))
    {
        case ai::TradeTryAction::CancelRetry:
        {
            WorldPacket cancel;
            uint32 cancelStatus = 0;
            cancel << cancelStatus;
            bot->GetSession()->HandleCancelTradeOpcode(cancel);
            return true;
        }
        case ai::TradeTryAction::GiveUp:
        {
            ai->TellPlayerNoFacing(trader, ai::kTradeGiveUpText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            WorldPacket cancel;
            uint32 cancelStatus = 0;
            cancel << cancelStatus;
            bot->GetSession()->HandleCancelTradeOpcode(cancel);
            bool need = order.reason == TortoiseBots::GuildOrders::OrderReason::NeedItems;
            TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(),
                need ? TortoiseBots::GuildOrders::OrderState::Waiting :
                    TortoiseBots::GuildOrders::OrderState::Ready,
                need ? TortoiseBots::GuildOrders::OrderReason::NeedItems :
                    TortoiseBots::GuildOrders::OrderReason::None);
            TortoiseBots::GuildOrders::ClearMeetFailed(bot->GetGUIDLow());
            return true;
        }
        case ai::TradeTryAction::KeepWaiting:
            break;
    }

    // The replace wait runs on the same clock: a "yes" confirms (consumed
    // through the whisper hook below), anything else cancels the window and
    // keeps the materials.
    if (session.replaceAskedMs != 0 && ai::ReplaceWaitExpired(nowMs, session.replaceAskedMs))
    {
        WorldPacket cancel;
        uint32 cancelStatus = 0;
        cancel << cancelStatus;
        bot->GetSession()->HandleCancelTradeOpcode(cancel);
        return true;
    }

    ai::EnchantTradeState tradeState;
    std::string currentEnchant;
    if (!EnchantHookWindowFacts(bot, trader, order.itemId, tradeState, currentEnchant))
        return true;
    tradeState.replaceConfirmed = session.replaceConfirmed;
    switch (ai::CheckEnchantTrade(tradeState))
    {
        case ai::EnchantTradeVerdict::WrongPlayer:
            ai->TellPlayerNoFacing(trader, ai::kEnchantWrongPlayerText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            break;
        case ai::EnchantTradeVerdict::Extras:
            ai->TellPlayerNoFacing(trader, ai::kEnchantExtrasText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            break;
        case ai::EnchantTradeVerdict::NoItem:
            ai->TellPlayerNoFacing(trader, ai::kEnchantNoItemText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            break;
        case ai::EnchantTradeVerdict::WrongItem:
            ai->TellPlayerNoFacing(trader, ai::kEnchantWrongItemText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            break;
        case ai::EnchantTradeVerdict::AskReplace:
            // Ask once per window: the whisper hook confirms a "yes" from
            // the giver only, and the expiry above closes a silent window.
            if (session.replaceAskedMs == 0)
            {
                ai->TellPlayerNoFacing(trader, ai::EnchantAskReplaceText(currentEnchant),
                    PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                TortoiseBots::GuildOrders::NoteEnchantReplaceAsked(bot->GetGUIDLow(), nowMs);
            }
            break;
        case ai::EnchantTradeVerdict::Ok:
        {
            // Cast once, then accept: the core remembers the spell
            // (SetSpell) and applies it when both sides accept, taking the
            // bot's reagents. A cancelled trade consumes nothing.
            if (bot->GetTradeData() && bot->GetTradeData()->GetSpell() == 0)
            {
                // The client's own path: a cast aimed at the trader's
                // "Will not be traded" slot. The core only remembers it and
                // casts it, taking the reagents, when both sides accept.
                WorldPacket cast(CMSG_CAST_SPELL, 4 + 2 + 2);
                cast << uint32(order.itemId);
                cast << uint16(TARGET_FLAG_TRADE_ITEM);
                cast << ObjectGuid(uint64(TRADE_SLOT_NONTRADED)).WriteAsPacked();
                bot->GetSession()->HandleCastSpellOpcode(cast);
            }
            if (trader->GetTradeData() && trader->GetTradeData()->IsAccepted())
            {
                // The giver accepted last: snapshot the parked item's guid
                // first, so the post-accept verify reads the same item.
                Item* parkedBefore = trader->GetTradeData()->GetItem(TRADE_SLOT_NONTRADED);
                ObjectGuid parkedGuid = parkedBefore ? parkedBefore->GetObjectGuid() : ObjectGuid();
                WorldPacket accept;
                uint32 acceptStatus = TRADE_STATUS_TRADE_ACCEPT;
                accept << acceptStatus;
                bot->GetSession()->HandleAcceptTradeOpcode(accept);
                if (!bot->GetTrader() && !parkedGuid.IsEmpty())
                {
                    // The window is gone: the core applied the enchant with
                    // the accept. The ordered enchant's EffectMiscValue id
                    // must sit in the parked permanent slot now.
                    bool verified = false;
                    if (Item* done = giverGuid.GetCounter() ?
                        sObjectAccessor.FindPlayer(giverGuid)->GetItemByGuid(parkedGuid) : nullptr)
                    {
                        SpellEntry const* ordered = sServerFacade.LookupSpellInfo(order.itemId);
                        uint32_t want = 0;
                        if (ordered)
                            for (uint8_t e = 0; e < MAX_EFFECT_INDEX; ++e)
                                if (ordered->Effect[e] == SPELL_EFFECT_ENCHANT_ITEM)
                                    want = uint32_t(ordered->EffectMiscValue[e]);
                        uint32_t got = done->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
                        verified = TortoiseBots::GuildOrders::EnchantDoneVerified(got, want);
                    }
                    if (verified)
                    {
                        TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(),
                            TortoiseBots::GuildOrders::OrderState::Done, TortoiseBots::GuildOrders::OrderReason::None);
                        TortoiseBots::GuildOrders::NoteEnchantTradeClosed(bot->GetGUIDLow());
                        TortoiseBots::GuildOrders::ClearMeet(bot->GetGUIDLow());
                        // One-line thanks; the surplus reagents the order
                        // gained ride the delivery leg to the guild bank
                        // through the existing craft-leftovers path.
                        ai->TellPlayerNoFacing(trader, "Enchanted, thank you.",
                            PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                    }
                    else
                    {
                        TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(),
                            TortoiseBots::GuildOrders::OrderState::Ready, TortoiseBots::GuildOrders::OrderReason::None);
                        TortoiseBots::GuildOrders::NoteEnchantTradeClosed(bot->GetGUIDLow());
                    }
                }
            }
            break;
        }
    }
    return true;
}
