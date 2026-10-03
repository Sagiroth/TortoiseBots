#pragma once

#include <cstdint>

namespace ai
{
    // Loot-roll vote gates, donor parity (E08): mod-playerbots gates auto
    // votes by recipe learnability, duplicate uniques, class tokens and the
    // loot method (LootRollAction::Execute, CanBotUseToken, RollUniqueCheck);
    // ours mapped everything to NEED/GREED/PASS by item usage only, so pool
    // bots GREEDed soulbound recipes they cannot learn, NEEDed duplicate
    // uniques they cannot loot, and voted in FFA/master-loot rolls. Pure
    // data rules here, tested on their own; RollAction::CalculateRollVote
    // supplies bot state. 1.12 adaptation: the core has no DISENCHANT vote
    // (Group::CountRollVote only records NEED/GREED/PASS, anything else is
    // silently dropped and the roll never completes), so DISENCHANT usage
    // stays GREED and enchanters disenchant after winning via the existing
    // "disenchant random item" maintenance - recorded divergence, not a gap.
    //
    // Votes mirror RollVote (PASS=0, NEED=1, GREED=2) without including game
    // headers, keeping the standalone test free of the core.
    enum class LootRollVote : int { Pass = 0, Need = 1, Greed = 2 };

    // Shared rolls only happen under group loot / need-before-greed: under
    // free-for-all there is no roll window and under master loot the master
    // distributes, so any auto vote is PASS. Takes the raw LootMethod
    // (FREE_FOR_ALL=0, MASTER_LOOT=2).
    inline bool LootMethodTakesRolls(int lootMethod)
    {
        return lootMethod != 0 && lootMethod != 2;
    }

    // Recipe rolls (pool bots only at the call site; owned bots keep today's
    // vote): a recipe the bot can learn is NEEDed, a soulbound recipe it
    // cannot learn is PASSED (nobody else can use it), a tradeable one is
    // GREEDed for the auction house. Donor puts all three behind
    // LootRollRecipe=0 (default PASS); the pool NEEDs learnable ones because
    // no player competes for them. Learnability arrives as usage==SKILL,
    // which for ITEM_CLASS_RECIPE already means unlearned + usable
    // (ItemUsageValue::IsItemNeededForSkill).
    inline LootRollVote RecipeRollVote(bool canLearn, bool bindOnPickup)
    {
        if (canLearn)
            return LootRollVote::Need;

        return bindOnPickup ? LootRollVote::Pass : LootRollVote::Greed;
    }

    // Duplicate-unique gate (donor RollUniqueCheck at LootNeedRollLevel=1,
    // the donor default: NEED becomes GREED): a unique already equipped, or
    // a stack already at its MaxCount, cannot be looted again, so NEED is
    // pointless. Applies to every bot; explicit FORCE_NEED still wins at
    // the call site. totalCount includes equipped copies (GetItemCount
    // walks equipment + bags).
    inline bool UniqueCopyOwned(bool isUniqueEquipped, bool equippedSameEntry,
        std::uint32_t totalCount, std::uint32_t maxCount)
    {
        if (isUniqueEquipped && equippedSameEntry)
            return true;

        return maxCount > 0 && totalCount >= maxCount;
    }

    // Class-token gate (donor CanBotUseToken): epic junk-class tokens are
    // NEEDed only by a class in their AllowableClass mask. An empty mask
    // means unrestricted (all classes can use it) - small fix inside the
    // port, the donor GREEDs those. Pool bots only at the call site.
    inline bool TokenUsableByClass(std::uint32_t allowableClass, std::uint32_t classMask)
    {
        return allowableClass == 0 || (allowableClass & classMask) != 0;
    }
}
