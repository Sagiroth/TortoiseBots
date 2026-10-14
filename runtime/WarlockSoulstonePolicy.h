#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Warlock Soulstone upkeep: which stone the bot holds and uses, who it
// stones, and when it makes a new one. Pure decision logic over durable
// facts, unit-tested in tools/test_warlock_soulstone_policy.cpp; the value
// (values/PartyMemberToSoulstone), the trigger/action pair
// (warlock/WarlockTriggers, WarlockActions) and the "no soulstone" create
// trigger wire it to the live group and bags.
//
// Who is stoned (owner order 2026-10-15, donor mod-playerbots "ss healer"):
//   in a group  - the healer, else the tank, else the master/leader; one
//                 tier at a time. A tier with a member that lacks the aura
//                 but is not ready (out of range, no line of sight, dead,
//                 another warlock mid-cast) is waited for, not skipped; a
//                 tier whose members all hold the aura is done and the next
//                 tier is stoned (a second warlock stones the tank). Waiting: a used stone starts the item's 30 minute cooldown,
//                 so spending it on the leader because the healer is a few
//                 yards late would leave the healer unstoned for half an hour.
//   solo        - the warlock stones itself (unchanged behaviour).
// Never in combat. "Already has the aura" is any Soulstone Resurrection rank
// (the 30 minute buff of every rank is the same effect), so the stone is
// re-applied when the buff falls off or the target used it.
//
// Shards: Create Soulstone consumes a Soul Shard (6265). Shardless, the
// create step stays quiet instead of queueing a spell that fails isPossible
// every few seconds. Shards come from Drain Soul (see ClassConsumablePolicy.h)
// and, for hires and `.bot maintenance`, from PlayerbotFactory::InitReagents
// (a random 1-5 shards for a warlock of level 10+: stack 3 x2, capped at the keep band of 5).

namespace TortoiseBots
{

// Item entries of the conjured stones, 1.12 rank order (Create Soulstone
// (Minor) lvl 18, Lesser 30, plain 40, Greater 50, Major 60).
inline constexpr uint32_t WARLOCK_SOULSTONE_MINOR = 5232;
inline constexpr uint32_t WARLOCK_SOULSTONE_LESSER = 16892;
inline constexpr uint32_t WARLOCK_SOULSTONE_PLAIN = 16893;
inline constexpr uint32_t WARLOCK_SOULSTONE_GREATER = 16895;
inline constexpr uint32_t WARLOCK_SOULSTONE_MAJOR = 16896;
inline constexpr uint32_t WARLOCK_SOULSTONE_MIN_LEVEL = 18;

// Best first.
inline constexpr uint32_t WARLOCK_SOULSTONE_ITEMS[] = {
    WARLOCK_SOULSTONE_MAJOR, WARLOCK_SOULSTONE_GREATER, WARLOCK_SOULSTONE_PLAIN,
    WARLOCK_SOULSTONE_LESSER, WARLOCK_SOULSTONE_MINOR,
};

// The rank the bot can make at its level; 0 below Create Soulstone (Minor).
inline uint32_t SoulstoneItemForLevel(uint32_t level)
{
    if (level >= 60)
        return WARLOCK_SOULSTONE_MAJOR;
    if (level >= 50)
        return WARLOCK_SOULSTONE_GREATER;
    if (level >= 40)
        return WARLOCK_SOULSTONE_PLAIN;
    if (level >= 30)
        return WARLOCK_SOULSTONE_LESSER;
    if (level >= WARLOCK_SOULSTONE_MIN_LEVEL)
        return WARLOCK_SOULSTONE_MINOR;
    return 0;
}

// The stone to use: the best rank the bot holds, else the rank for its level.
// Matching only the current level's rank left a stone made at level 29 (or
// held across a level-up) unused forever, while the "no soulstone" check
// (any rank, by name) never made a new one.
template <typename CountFn>
inline uint32_t PickSoulstoneItem(uint32_t level, CountFn itemCount)
{
    for (uint32_t itemId : WARLOCK_SOULSTONE_ITEMS)
    {
        if (itemCount(itemId) > 0)
            return itemId;
    }
    return SoulstoneItemForLevel(level);
}

// Create Soulstone: only with the spell (level), a shard in the bags and no
// stone held; never in combat (the create spell is a 3 s cast and the shard
// is better spent after the pull).
struct SoulstoneCreateFacts
{
    uint32_t level = 0;
    uint32_t soulShards = 0;
    uint32_t soulstonesHeld = 0;
    bool inCombat = false;
};

inline bool ShouldCreateSoulstone(SoulstoneCreateFacts const& facts)
{
    if (facts.level < WARLOCK_SOULSTONE_MIN_LEVEL)
        return false;
    if (facts.soulstonesHeld > 0)
        return false;
    if (facts.soulShards == 0)
        return false;
    return !facts.inCombat;
}

// One group member as the warlock sees it.
struct SoulstoneCandidate
{
    bool isHealer = false;
    bool isTank = false;
    bool isLeader = false;           // the master or the group leader
    bool alive = true;
    bool inRange = true;             // within the use range of the stone
    bool inLineOfSight = true;
    bool hasSoulstoneAura = false;   // any Soulstone Resurrection rank
    bool targetedByOtherWarlock = false; // another warlock is mid-cast on it
};

struct SoulstoneFacts
{
    bool inGroup = false;
    bool botInCombat = false;
    bool botHasSoulstoneAura = false; // used for the solo case only
};

enum class SoulstoneChoiceKind
{
    None,
    Self,
    Member,
};

struct SoulstoneChoice
{
    SoulstoneChoiceKind kind = SoulstoneChoiceKind::None;
    size_t index = 0; // into the candidate list when kind == Member
};

enum class SoulstoneTier
{
    Healer,
    Tank,
    Leader,
    None,
};

inline SoulstoneTier SoulstoneTierOf(SoulstoneCandidate const& c)
{
    if (c.isHealer)
        return SoulstoneTier::Healer;
    if (c.isTank)
        return SoulstoneTier::Tank;
    if (c.isLeader)
        return SoulstoneTier::Leader;
    return SoulstoneTier::None;
}

inline bool IsSoulstoneReady(SoulstoneCandidate const& c)
{
    return c.alive && c.inRange && c.inLineOfSight && !c.hasSoulstoneAura && !c.targetedByOtherWarlock;
}

// `others` is every other group member on the warlock's map (never the
// warlock itself).
inline SoulstoneChoice DecideSoulstoneTarget(SoulstoneFacts const& facts,
                                             std::vector<SoulstoneCandidate> const& others)
{
    SoulstoneChoice none;
    if (facts.botInCombat)
        return none;

    if (!facts.inGroup)
    {
        if (facts.botHasSoulstoneAura)
            return none;
        SoulstoneChoice self;
        self.kind = SoulstoneChoiceKind::Self;
        return self;
    }

    static constexpr SoulstoneTier order[] = {SoulstoneTier::Healer, SoulstoneTier::Tank, SoulstoneTier::Leader};
    for (SoulstoneTier tier : order)
    {
        bool tierNeedsStone = false; // a member of the tier lacks the aura
        for (size_t i = 0; i < others.size(); ++i)
        {
            if (SoulstoneTierOf(others[i]) != tier)
                continue;
            if (!others[i].hasSoulstoneAura)
                tierNeedsStone = true;
            if (IsSoulstoneReady(others[i]))
            {
                SoulstoneChoice pick;
                pick.kind = SoulstoneChoiceKind::Member;
                pick.index = i;
                return pick;
            }
        }
        // A member lacks the aura but is not ready (far, dead, no line of
        // sight, another warlock casting): wait, do not fall through to a
        // lower tier (see the header comment). A tier whose members all hold
        // the aura is done, so the next tier is looked at (a second warlock
        // stones the tank).
        if (tierNeedsStone)
            return none;
    }
    return none;
}

} // namespace TortoiseBots
