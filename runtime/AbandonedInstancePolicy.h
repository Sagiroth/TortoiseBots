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
    bool alive = false;
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
    if (!f.isHeadless || !f.inWorld || f.beingTeleported || !f.alive ||
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

} // namespace TortoiseBots
