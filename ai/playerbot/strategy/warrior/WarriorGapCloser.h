#pragma once

#include "playerbot/WarriorGapCloserPolicy.h"

class Unit;
class PlayerbotAI;

namespace ai
{
    // The gap closer the warrior should use this tick and on whom
    // (WarriorGapCloserPolicy.h holds the rules). A tank looks at the mob on
    // a non-tank group member first, then at its own target; a dps warrior
    // only at its own target, and only out of combat.
    WarriorGapCloser DecideWarriorGapCloser(PlayerbotAI* ai, Unit** target = nullptr);
}
