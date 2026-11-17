#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace TortoiseBots
{

// One level 1 alt a level 60 pool bot rolled (tortoise_bots_lineage). A
// character without a row is the founder of its own lineage.
struct LineageRecord
{
    uint32_t guid = 0;
    uint32_t parentGuid = 0;
    uint32_t founderGuid = 0;
    std::string name;
    uint8_t race = 0;
    uint8_t cls = 0;
    int64_t createdAt = 0;
    int64_t reached60At = 0;
};

// The lineage table, loaded once at startup and kept in memory; writes go to
// the database asynchronously. Rows are few (a handful per hour at most).
class LineageStore
{
public:
    static LineageStore& Instance();

    void Load();
    // Pool reset: a new world, the history goes with it.
    void Clear();

    bool IsAlt(uint32_t guid) const;
    uint32_t FounderOf(uint32_t guid) const;
    // Founder plus every alt of the line.
    uint32_t LineageSize(uint32_t founderGuid) const;
    // Classes of the line's alts (bit 1 << (class - 1)); the founder's own
    // class is the caller's to add.
    uint32_t AltClassMask(uint32_t founderGuid) const;
    int64_t LastCreatedAt() const;

    void Record(LineageRecord const& record);
    // Stamps the first time an alt is seen at level 60.
    void NoteLevel(uint32_t guid, uint32_t level, int64_t now);

    struct Totals
    {
        uint32_t alts = 0;
        uint32_t last24h = 0;
        uint32_t alliance = 0;
        uint32_t horde = 0;
    };
    Totals Count(int64_t now) const;

private:
    struct Line
    {
        uint32_t alts = 0;
        uint32_t classMask = 0;
    };
    void IndexLocked(LineageRecord const& record);

    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, LineageRecord> m_records;
    std::unordered_map<uint32_t, Line> m_lines;
    int64_t m_lastCreatedAt = 0;
};

} // namespace TortoiseBots
