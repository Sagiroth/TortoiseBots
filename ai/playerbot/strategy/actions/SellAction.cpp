
#include "playerbot/playerbot.h"
#include "SellAction.h"
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/MaintenanceValues.h"

using namespace ai;

class SellItemsVisitor : public IterateItemsVisitor
{
public:
    SellItemsVisitor(SellAction* action) : IterateItemsVisitor()
    {
        this->action = action;
    }

    virtual bool Visit(Item* item)
    {
        action->Sell(nullptr, item);
        return true;
    }

private:
    SellAction* action;
};

bool SellAction::Execute(Event& event)
{
    static uint32 minAutoSellItems = 5;
    static uint8 minAutoSellPercentageOfBag = 20;
    static uint8 maxAutoSellPercentageOfBag = 80;


    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    std::string text = event.GetParam();

    if (text == "*" || text.empty())
        text = "gray";

    std::list<Item*> items = ai->InventoryParseItems(text, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);

    if (event.GetSource() == "rpg action")
    {
        items.sort([](Item* i, Item* j) {return i->GetProto()->SellPrice * i->GetCount() < j->GetProto()->SellPrice * j->GetCount(); }); //Sell cheapest items first.
    }

    uint32 soldItems = 0;
    uint32 shouldSell = std::max(minAutoSellItems, uint32(items.size() * urand(minAutoSellPercentageOfBag, maxAutoSellPercentageOfBag) / 100));

    //The random cap exists so a bag-pressure top-up keeps some stock. A trip
    //made to fund a spell rank is pointless if it leaves the stock that still
    //covers the deficit behind: the bot would sell half, walk home and be sent
    //straight back. Liquidation is then all or nothing.
    bool const allOrNothing = event.GetSource() == "rpg action" && ShouldSellValue::CantAffordNextSpell(ai);

    for (std::list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
    {
        if (Sell(requester, *i))
            soldItems++;

        if (!allOrNothing && event.GetSource() == "rpg action" && soldItems >= shouldSell)
            break;
    }

    //An errand sale that sold nothing used to be invisible: 90 minutes of a live
    //pool produced 0 SellAction rows and no reason for them, so "no vendor wants
    //this stock" and "no vendor in reach" could not be told apart. One line per
    //five minutes per bot keeps a bot that cannot sell visible without flooding
    //bot_events.csv.
    if (!soldItems && event.GetSource() == "rpg action")
    {
        uint32 sellable = 0;
        uint32 vendorUsage = 0;
        for (Item* item : items)
        {
            if (item->GetProto()->SellPrice)
                sellable++;
            if (AI_VALUE2_LAZY(ItemUsage, "item usage", ItemQualifier(item).GetQualifier()) == ItemUsage::ITEM_USAGE_VENDOR)
                vendorUsage++;
        }

        //Nothing a vendor buys: standing at a vendor cannot finish this errand,
        //so stop asking for the Vendor travel target (and the near-service walk)
        //for a while instead of re-trying every tick.
        if (!sellable)
            ParkVendorErrand(ai, 10);

        if (AI_VALUE2(time_t, "manual time", "sell errand failed log") <= time(0))
        {
            SET_AI_VALUE2(time_t, "manual time", "sell errand failed log", time(0) + 5 * MINUTE);

            // The usage breakdown tells "usage said VENDOR but SellPrice is 0"
            // (issue #408) apart from "nothing was even classified VENDOR".
            std::string reason = sellable ? "no vendor in reach" :
                (vendorUsage ? "no vendor-usable stock" : "no vendor-usage stock");
            sPlayerbotAIConfig.logEvent(ai, "SellErrandFailed", reason,
                std::to_string(items.size()) + ":" + std::to_string(vendorUsage));
        }
    }

    // A sale pays off the vendor trip that brought the bot here: clear the
    // "one vendor journey at a time" window (issue #399) so the need - which
    // is false now anyway, the stock just sold - re-arms honestly on the next
    // pickup instead of waiting out the ten minutes.
    if (soldItems)
        RESET_AI_VALUE2(time_t, "manual time", "vendor trip since");

    return soldItems;
}

bool SellAction::Sell(Player* requester, FindItemVisitor* visitor)
{
    bool didSell = false;
    ai->InventoryIterateItems(visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    std::list<Item*> items = visitor->GetResult();
    for (std::list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
    {
        didSell |= Sell(requester, *i);
    }

    return didSell;
}

bool SellAction::HasVendorStock(Creature* vendor)
{
    if (!vendor)
        return false;
    VendorItemData const* items = vendor->GetVendorItems();
    VendorItemData const* templateItems = vendor->GetVendorTemplateItems();
    return (items && !items->Empty()) || (templateItems && !templateItems->Empty());
}

bool SellAction::Sell(Player* requester, Item* item)
{
    bool didSell = false;

    std::ostringstream out;
    std::list<ObjectGuid> vendors = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest npcs")->Get();

    for (std::list<ObjectGuid>::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        ObjectGuid vendorguid = *i;
        Creature *pCreature = bot->GetNPCIfCanInteractWith(vendorguid,UNIT_NPC_FLAG_VENDOR);
        if (!pCreature)
            continue;

        // #404: Terry Palin (entry 1650) carries the vendor flag but no npc_vendor rows, so
        // every gossip hello at him prints a core outErrorDb line ("empty trading item list"). World
        // DB data - never touched from the module - so skip flagged-but-stockless NPCs before
        // interacting: quest givers like Palin (quest 60042) are still served via the quest verbs.
        if (!HasVendorStock(pCreature))
            continue;
        if (!item->GetProto()->SellPrice)
        {
            if(ai->HasActivePlayerMaster())
                out << "Unable to sell " << chat->formatItem(item);

            continue;
        }

        ObjectGuid itemguid = item->getObjectGuid();
        uint32 count = item->GetCount();

        uint32 botMoney = bot->GetMoney();

        sPlayerbotAIConfig.logEvent(ai, "SellAction", item->GetProto()->Name1, std::to_string(item->GetProto()->ItemId));

        WorldPacket p;
        p << vendorguid << itemguid << count;
        bot->GetSession()->HandleSellItemOpcode(p);

        if (ai->HasCheat(BotCheatMask::gold))
        {
            bot->SetMoney(botMoney);
        }

        out << "Selling " << chat->formatItem(item);
        if (sPlayerbotAIConfig.globalSoundEffects)
            bot->PlayDistanceSound(120);

        didSell = true;

        // A vendor run is autonomous AI; the per-item "Selling X" whisper
        // reaches /say or party chat when the requester is another pool bot.
        // Keep the confirmation for a live player; else stay silent.
        if (requester && isRealPlayer_Helper(requester))
            ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        break;
    }

    return didSell;
}