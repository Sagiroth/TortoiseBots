#pragma once

// Rules for evacuating random or autonomous bots left inside a dungeon or raid
// instance with no real player in their group and no master in the instance.
//
// Pure and core-free, so tools/test_abandoned_instance_policy.cpp includes it
// directly.

#include <cstdint>

namespace TortoiseBots
{

struct AbandonedInstanceFacts
{
    bool isHeadless = false;
    bool inWorld = false;
    bool beingTeleported = false;
    bool inBattleground = false;
    bool onInstanceMap = false;
    bool randomOrAutonomous = false;
    bool inWorldLifecycle = false;
    bool hired = false;
    bool pinned = false;
    bool dungeonCrew = false;
    bool groupHasRealPlayer = false;
    bool hasMaster = false;
    bool masterInSameInstance = false;
};

inline bool IsAbandonedInInstance(AbandonedInstanceFacts const& f)
{
    // Dead bots count too: one that keeps dying to the instance's mobs never
    // stays alive for the whole grace period.
    if (!f.isHeadless || !f.inWorld || f.beingTeleported ||
        f.inBattleground || !f.onInstanceMap)
        return false;
    if (!f.randomOrAutonomous || !f.inWorldLifecycle)
        return false;
    if (f.hired || f.pinned || f.dungeonCrew)
        return false;
    if (f.groupHasRealPlayer)
        return false;
    if (f.hasMaster && f.masterInSameInstance)
        return false;
    return true;
}

inline bool AbandonedInstanceGraceElapsed(std::int64_t firstSeenSec, std::int64_t nowSec,
    std::uint32_t graceSec)
{
    if (graceSec == 0 || nowSec < firstSeenSec)
        return false;
    return static_cast<std::uint64_t>(nowSec - firstSeenSec) >= graceSec;
}

struct OrphanedBotGroupFacts
{
    bool isHeadless = false;
    bool inWorld = false;
    bool beingTeleported = false;
    bool inBattleground = false;
    bool randomBot = false;
    bool inWorldLifecycle = false;
    bool hasMaster = false;
    bool hired = false;
    bool pinned = false;
    bool dungeonCrew = false;
    bool commandedGroup = false;
    bool inGroup = false;
    bool groupHasRealPlayer = false;
};

inline bool IsOrphanedInBotGroup(OrphanedBotGroupFacts const& f)
{
    if (!f.isHeadless || !f.inWorld || f.beingTeleported || f.inBattleground)
        return false;
    if (!f.randomBot || !f.inWorldLifecycle || f.hasMaster)
        return false;
    if (f.hired || f.pinned || f.dungeonCrew || f.commandedGroup)
        return false;
    if (!f.inGroup || f.groupHasRealPlayer)
        return false;
    return true;
}

} // namespace TortoiseBots
