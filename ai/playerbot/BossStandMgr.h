#pragma once

#include "BossStandPolicy.h"

#include <cstdint>

namespace ai
{
    // The stand sets (BossStandPolicy.h), filled by LoadBossStands() and read-only afterwards. Unlike the boss tactics
    // they are not part of `.bot run reloadplans`: a change to `ai_boss_stand` needs a restart.
    BossStandTable& GetBossStands();

    // Reads `ai_boss_stand` once at startup (one pass over the rows) and seals the table. A second call does nothing.
    void LoadBossStands();

    // One pass over `ai_boss_stand` into an empty table, then its seal: the rows skipped.
    std::uint32_t ReadBossStandRows(BossStandTable& table);
}
