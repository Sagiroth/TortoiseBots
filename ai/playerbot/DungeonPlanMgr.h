#pragma once

#include "DungeonPlanPolicy.h"

namespace ai
{
    // The one plan table, filled by LoadDungeonPlans() and read-only afterwards.
    DungeonPlanTable& GetDungeonPlans();

    // Reads `ai_dungeon_plan`, `ai_dungeon_leg` and `ai_dungeon_step` once at
    // startup (one pass over each) and seals the table. A second call does nothing.
    void LoadDungeonPlans();
}
