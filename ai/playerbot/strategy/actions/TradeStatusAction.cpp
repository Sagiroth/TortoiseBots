
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "TradeStatusAction.h"

#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/ServerFacade.h"
#include "../../runtime/HireLifecycle.h"
#include "../../runtime/PoolBotTradePolicy.h"
#include "playerbot/strategy/values/CraftValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "SetCraftAction.h"

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
    if (!ai->HasRealPlayerMaster() && sRandomBotFacade.IsRandomBot(bot) &&
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

    bool shouldTrade = true;
    if (!PlayerbotAIStorage::Instance().GetAI(trader))
    {
        shouldTrade = false;
        if (trader == master || IsInGroup_Helper(bot, trader))
        {
            shouldTrade = ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false, trader);
        }
        // Issue #469 reconciliation: the pool gate above already approved
        // this trader (open mode 2/3/4, ungrouped owner, or hire master),
        // but the legacy block only knows master/group. Without this the
        // approval is dead: the window would be cancelled right here.
        if (!shouldTrade && !ai->HasRealPlayerMaster() && sRandomBotFacade.IsRandomBot(bot) &&
            !TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) &&
            isRealPlayer_Helper(trader) && PoolBotMayTradeWith(trader))
        {
            shouldTrade = true;
        }
    }

    if (!shouldTrade)
    {
        WorldPacket p;
        uint32 status = 0;
        p << status;
        bot->GetSession()->HandleCancelTradeOpcode(p);
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
            {
                ai->TellPlayerNoFacing(trader, "Thank you " + name + ".", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }
            else
            {
                bot->Say("Thank you " + name + ".", (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
            }
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
