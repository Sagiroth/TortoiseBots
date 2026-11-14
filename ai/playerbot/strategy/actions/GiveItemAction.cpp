
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/playerbot.h"
#include "GiveItemAction.h"

#include "playerbot/strategy/values/ItemCountValue.h"
#include "../../../../runtime/RunGivenItemRegistry.h"

using namespace ai;

std::vector<std::string> split(const std::string &s, char delim);

bool GiveItemAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Unit* target = GetTarget();
    if (!target) return false;

    Player* receiver = dynamic_cast<Player*>(target);
    if (!receiver) return false;

    PlayerbotAI *receiverAi = PlayerbotAIStorage::Instance().GetAI(receiver);
    if (!receiverAi)
        return false;

    if (receiverAi->GetAiObjectContext()->GetValue<uint32>("item count", item)->Get())
        return true;

    bool moved = false;
    std::list<Item*> items = ai->InventoryParseItems(item, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    for (std::list<Item*>::iterator j = items.begin(); j != items.end(); j++)
    {
        Item* item = *j;

        // A dungeon run's given item (key, mallet) stays with its bot until the run takes it back.
        if (TortoiseBots::RunGivenItems::Has(item->GetGUIDLow()))
            continue;

        // Never hand over order stock to another bot.
        if (bot->GetGuildId() && GuildOrderConsumesItem(bot, item->GetProto()->ItemId))
            continue;

        if (receiver->CanUseItem(item->GetProto()) != EQUIP_ERR_OK)
            continue;

        ItemPosCountVec dest;
        InventoryResult msg = receiver->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg == EQUIP_ERR_OK)
        {
            // The link first: a stack that merges into the receiver's is freed by the move.
            std::string const link = chat->formatItem(item, item->GetCount());
            bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
            item->SetOwnerGuid(target->getObjectGuid());
            // Same flags as a trade (TradeHandler.cpp): a stack the giver already saved keeps its
            // character_inventory row, so the receiver's save updates it instead of inserting a second one.
            receiver->MoveItemToInventory(dest, item, true, true);
            moved = true;

            std::ostringstream out;
            out << "Got " << link << " from " << bot->GetName();
            receiverAi->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
        else
        {
            std::ostringstream out;
            out << "Cannot get " << chat->formatItem(item, item->GetCount()) << " from " << bot->GetName() << "- my bags are full";
            receiverAi->TellPlayerNoFacing(requester, out.str());
        }
    }

    return true;
}

Unit* GiveItemAction::GetTarget()
{
    return AI_VALUE2(Unit*, "party member without item", item);
}

Unit* GiveFoodAction::GetTarget()
{
    return AI_VALUE(Unit*, "party member without food");
}

Unit* GiveWaterAction::GetTarget()
{
    return AI_VALUE(Unit*, "party member without water");
}
