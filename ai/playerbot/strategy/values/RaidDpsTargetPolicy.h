#pragma once
class Unit;
class PlayerbotAI;

namespace ai
{
    // Does not remove attackers from tank, threat, heal or crowd-control values.
    Unit* GetOnyxiaWhelpTarget(PlayerbotAI* ai);
    Unit* GetOnyxiaRangedTarget(PlayerbotAI* ai);
    Unit* GetActiveGarr(PlayerbotAI* ai);
    bool IsRaidDpsTargetExcluded(PlayerbotAI* ai, Unit* target);
}
