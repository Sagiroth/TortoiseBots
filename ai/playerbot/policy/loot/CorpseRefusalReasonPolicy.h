#pragma once

#include <string>

namespace ai
{
    // Why a dead creature with loot is not this bot's to loot (debug text only). Order matters:
    // the first true cause wins, because each one hides the next in the core.
    inline std::string CorpseRefusalReason(bool presentAtKill, bool lootRolledQuestItems,
                                           bool hasPersonalItem, bool otherMembersTurn,
                                           bool reservedForTurnHolder)
    {
        if (!presentAtKill)        return "not in reward range at the kill (no loot right, no quest copy)";
        if (reservedForTurnHolder) return "quest item reserved for the round-robin member until it loots or releases";
        if (hasPersonalItem)       return "has own item but refused (unexpected)";
        if (!lootRolledQuestItems) return "no quest item rolled";
        if (otherMembersTurn)      return "another member's turn; no quest item for this bot (quest not INCOMPLETE, or the copy is taken)";
        return "no right for this bot";
    }
}
