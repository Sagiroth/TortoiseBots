// Vanilla/Turtle trade lockpicking action.

#include "playerbot/playerbot.h"
#include "UnlockTradedItemAction.h"
#include "Database/DBCStores.h"
#include "Objects/Item.h"
#include "Objects/ItemPrototype.h"
#include "Objects/Player.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"

#include <sstream>

inline constexpr uint32_t PICK_LOCK_SPELL_ID = 1804;

bool UnlockTradedItemAction::Execute(Event& event)
{
    Player* trader = bot->GetTrader();
    if (!trader)
        return false;

    TradeData* tradeData = bot->GetTradeData();
    if (!tradeData)
        return false;

    Item* lockbox = tradeData->GetTraderData()->GetItem(TRADE_SLOT_NONTRADED);
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (!lockbox)
    {
        ai->TellError(requester ? requester : bot, "No item in the Do Not Trade slot.");
        return false;
    }

    if (!CanUnlockItem(lockbox))
    {
        ai->TellError(requester ? requester : bot, "I cannot unlock this traded item.");
        return false;
    }

    return UnlockItem(lockbox, requester);
}

bool UnlockTradedItemAction::CanUnlockItem(Item* item)
{
    if (!item || !item->GetProto())
        return false;

    ItemPrototype const* itemTemplate = item->GetProto();
    if (bot->GetClass() != CLASS_ROGUE || !botAI->HasSkill(SKILL_LOCKPICKING) || !bot->HasSpell(PICK_LOCK_SPELL_ID))
        return false;

    if (itemTemplate->LockID == 0 || item->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_UNLOCKED))
        return false;

    LockEntry const* lockInfo = sLockStore.LookupEntry(itemTemplate->LockID);
    if (!lockInfo)
        return false;

    uint32 botSkill = bot->GetSkillValue(SKILL_LOCKPICKING);
    for (uint8 j = 0; j < MAX_LOCK_CASE; ++j)
    {
        if (lockInfo->Type[j] != LOCK_KEY_SKILL ||
            SkillByLockType(LockType(lockInfo->Index[j])) != SKILL_LOCKPICKING)
            continue;

        uint32 requiredSkill = lockInfo->Skill[j];
        if (botSkill >= requiredSkill)
            return true;

        std::ostringstream out;
        out << "Lockpicking skill too low (" << botSkill << "/" << requiredSkill << ") to unlock: "
            << itemTemplate->Name1;
        ai->TellPlayer(GetMaster() ? GetMaster() : bot, out.str());
        return false;
    }

    return false;
}

bool UnlockTradedItemAction::UnlockItem(Item* item, Player* requester)
{
    if (botAI->CastSpell(PICK_LOCK_SPELL_ID, bot->GetTrader(), item))
    {
        std::ostringstream out;
        out << "Picking Lock on traded item: " << item->GetProto()->Name1;
        ai->TellPlayer(requester ? requester : bot, out.str());
        return true;
    }

    ai->TellError(requester ? requester : bot, "Failed to cast Pick Lock on the traded item.");
    return false;
}
