#pragma once
class Unit;
class PlayerbotAI;

namespace ai
{
    // Does not remove attackers from tank, threat, heal or crowd-control values.
    bool IsRaidDpsTargetExcluded(PlayerbotAI* ai, Unit* target);
}
