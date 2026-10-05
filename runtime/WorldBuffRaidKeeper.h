// Issue #492: KeepWorldBuffsInRaids + Upper Karazhan (814) coverage. The
// core strips the DB-listed spells on 10 raid maps (AuraRemovalMgr, fed by
// instance_buff_removal) but knows nothing of map 814. This keeper snapshots
// live buff auras with remaining time before the teleport, restores them
// after the map change when the keep flag is on, and strips the list in 814
// when the flag is off. Snapshots are memory-only: a logout mid-teleport
// loses the buffs, same as death by design (no DB write per teleport).
//
// Pure-callable core: map gating + the spell table live here and are
// unit-tested standalone. The teleport wiring calls into it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{

struct WorldBuffSnapshotEntry
{
    uint32_t spellId = 0;
    int32_t remainMs = 0;
    int32_t maxMs = 0;
};

// The 18 spells the core strips on raid entry (live instance_buff_removal,
// enabled=1 throughout). Silithyst (29534) is on no list and is never
// touched here either. Upper Kara 814 reuses the same set.
inline uint32_t const* RaidStripSpells(uint32_t& count)
{
    static uint32_t const spells[] = {
        15366,
        16609,
        18968,
        22817, 22818, 22820,
        22888,
        23735, 23736, 23737, 23738,
        23766, 23767, 23768, 23769,
        24425,
        26393,
        28681,
    };
    count = 18;
    return spells;
}

inline bool IsRaidStripSpell(uint32_t spellId)
{
    uint32_t count = 0;
    for (uint32_t const* p = RaidStripSpells(count); count > 0; --count, ++p)
        if (*p == spellId)
            return true;
    return false;
}

// In-memory guid -> snapshot. Keyed by the player's low guid; cleared on
// restore/strip so a second teleport without a snapshot is a no-op.
class WorldBuffRaidKeeper
{
public:
    void Snapshot(uint32_t guidLow, std::vector<WorldBuffSnapshotEntry> const& entries)
    {
        if (entries.empty())
            m_snapshots.erase(guidLow);
        else
            m_snapshots[guidLow] = entries;
    }

    bool Take(uint32_t guidLow, std::vector<WorldBuffSnapshotEntry>& out)
    {
        auto it = m_snapshots.find(guidLow);
        if (it == m_snapshots.end())
            return false;
        out = it->second;
        m_snapshots.erase(it);
        return true;
    }

    void Clear(uint32_t guidLow)
    {
        m_snapshots.erase(guidLow);
    }

    size_t Size() const { return m_snapshots.size(); }

private:
    std::unordered_map<uint32_t, std::vector<WorldBuffSnapshotEntry>> m_snapshots;
};

} // namespace TortoiseBots
