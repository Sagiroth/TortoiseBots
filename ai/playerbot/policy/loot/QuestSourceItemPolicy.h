#pragma once

#include <cstdint>

namespace ai
{
    // Quest source-item need, mirroring the core's Player::HasQuestForItem ReqSource branch
    // (core Player.cpp): a quest's source item (ReqSourceId) is still needed while the bot
    // holds fewer than the item's MaxCount (unique items), else fewer than the quest's
    // ReqSourceCount, else fewer than one stack. Tested on its own; ItemUsageValue::IsNeededForQuest
    // supplies the quest fields and the bag count.
    inline bool QuestSourceItemStillNeeded(uint32_t heldCount, uint32_t maxCount, uint32_t reqSourceCount,
                                           uint32_t stackable)
    {
        if (maxCount && heldCount < maxCount)
            return true;

        if (reqSourceCount)
            return heldCount < reqSourceCount;

        return heldCount < stackable;
    }
}
