#pragma once
// May the bot take this loot slot? Mirrors WorldSession::HandleAutostoreLootItemOpcode (core LootHandler.cpp):
//  - not allowed for this player: never;
//  - is_blocked refuses only a non-quest item (the core sets is_blocked on every quest item once it enters a
//    player's quest list: "questitems use the blocked field for other purposes");
//  - a personal quest or free-for-all copy is the bot's own (GetSlotTypeForSharedLoot answers MAX for every
//    free-for-all item, so it must not decide those);
//  - a shared item needs a shared slot type.
namespace ai
{
    inline bool BotMayTakeLootSlot(bool allowedForPlayer, bool personalQuestItem, bool personalFfaItem,
                                   bool blocked, bool sharedSlotAllowed)
    {
        if (!allowedForPlayer)
            return false;
        if (blocked && !personalQuestItem)
            return false;
        if (personalQuestItem || personalFfaItem)
            return true;
        return sharedSlotAllowed;
    }
}
