
#include "playerbot/playerbot.h"
#include "WtsAction.h"
#include "playerbot/AiFactory.h"
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

using namespace ai;

bool WtsAction::Execute(Event& event)
{
    Player* owner = event.GetOwner();
    if (!owner)
        return false;

    // Issue #469: the "I'll buy ..." tell answers a real player's linked
    // item, never another bot's chatter. Without this a Trade-channel item
    // link between two bots loops into tells no human reads.
    if (sRandomBotFacade.IsRandomBot(bot) && !isRealPlayer_Helper(owner))
        return false;


    std::ostringstream out;
    std::string text = event.GetParam();

    if (!sRandomBotFacade.IsRandomBot(bot))
        return false;

    std::string link = event.GetParam();

    ItemIds itemIds = chat->parseItems(link);
    if (itemIds.empty())
        return false;

    for (ItemIds::iterator i = itemIds.begin(); i != itemIds.end(); i++)
    {
        uint32 itemId = *i;
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            continue;

        std::ostringstream out; out << itemId;
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
        if (usage == ItemUsage::ITEM_USAGE_NONE)
            continue;

        int32 buyPrice = ItemUsageValue::GetBotBuyPrice(proto, bot);
        if (!buyPrice)
            continue;

        if (urand(0, 15) > 2)
            continue;

        std::ostringstream tell;
        tell << "I'll buy " << chat->formatItem(proto) << " for " << chat->formatMoney(buyPrice);

        // ignore random bot chat filter
        ai->TellPlayer(owner, tell.str());
    }

    return true;
}
