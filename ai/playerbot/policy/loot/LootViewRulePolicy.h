#pragma once

namespace ai
{
    // Player 1.12 core LootView (LootMgr.cpp:1016-1019 quest, 1059-1062 conditional): a personal
    // non-FFA copy is hidden from a viewer while another member holds the round-robin turn and that
    // member has a copy in the same list. The holder's release (roundRobinPlayer = 0) lifts it.
    inline bool PersonalCopyVisible(bool freeforall, bool turnAssigned, bool viewerHoldsTurn,
                                    bool turnHolderHasCopyInList)
    {
        return freeforall || !turnAssigned || viewerHoldsTurn || !turnHolderHasCopyInList;
    }
}
