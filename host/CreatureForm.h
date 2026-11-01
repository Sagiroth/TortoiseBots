#pragma once
// The entry a creature was spawned as when a script changed its entry away from it (DungeonRun::ScriptChangedEntry):
// the core's UpdateEntry keeps the guid, and the guid carries the spawn row's first creature id, so the row's own
// alternatives are known. Both the run leader's read and the combat monitor's death hook need it: a boss that
// changes entry mid-fight is still the boss.
#include "../runtime/DungeonRunPolicy.h"
#include "Creature.h"
#include "ObjectMgr.h"

namespace TortoiseBots
{
namespace DungeonRun
{

inline std::uint32_t ScriptChangedFrom(Creature const* c)
{
    if (!c)
        return 0;
    std::uint32_t const guidEntry = c->GetObjectGuid().GetEntry();
    std::uint32_t const current = c->GetEntry();
    if (!guidEntry || current == guidEntry)
        return 0;
    std::vector<std::uint32_t> spawnIds;
    if (CreatureData const* data = sObjectMgr.GetCreatureData(c->GetGUIDLow()))
        for (std::uint32_t id : data->creature_id)
            if (id)
                spawnIds.push_back(id);
    return ScriptChangedEntry(guidEntry, current, spawnIds);
}

} // namespace DungeonRun
} // namespace TortoiseBots
