#pragma once

#include "BossTacticsPolicy.h"

namespace ai
{
    // The one table, filled by LoadBossTactics() and read-only afterwards.
    BossTacticTable& GetBossTactics();

    // Reads `ai_boss_tactic` once at startup (one pass over the rows) and seals
    // the table. A second call does nothing.
    void LoadBossTactics();
}
