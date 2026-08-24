
#include "playerbot/playerbot.h"
#include "GuildBankAction.h"

#include "playerbot/strategy/values/ItemCountValue.h"
#include "Guild/Guild.h"
#include "Guild/GuildMgr.h"

using namespace ai;

bool GuildBankAction::Execute(Event& event)
{
    return false;
}

bool GuildBankAction::Execute(std::string text, GameObject* bank, Player* requester)
{
    bool result = true;

    IterateItemsMask mask = IterateItemsMask((uint8)IterateItemsMask::ITERATE_ITEMS_IN_EQUIP | (uint8)IterateItemsMask::ITERATE_ITEMS_IN_BAGS);

    std::list<Item*> found = ai->InventoryParseItems(text, mask);
    if (found.empty())
        return false;

    for (std::list<Item*>::iterator i = found.begin(); i != found.end(); i++)
    {
        Item* item = *i;
        if (item)
            result &= MoveFromCharToBank(item, bank, requester);
    }

    return result;
}

bool GuildBankAction::MoveFromCharToBank(Item* item, GameObject* bank, Player* requester)
{
    return false;
}
