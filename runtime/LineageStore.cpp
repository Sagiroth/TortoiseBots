#include "LineageStore.h"

#include "Database/DatabaseEnv.h"
#include "Player.h"

#include <memory>

namespace TortoiseBots
{

LineageStore& LineageStore::Instance()
{
    static LineageStore instance;
    return instance;
}

void LineageStore::IndexLocked(LineageRecord const& record)
{
    m_records[record.guid] = record;
    Line& line = m_lines[record.founderGuid];
    ++line.alts;
    if (record.cls)
        line.classMask |= 1u << (record.cls - 1);
    if (record.createdAt > m_lastCreatedAt)
        m_lastCreatedAt = record.createdAt;
}

void LineageStore::Load()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();
    m_lines.clear();
    m_lastCreatedAt = 0;
    std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
        "SELECT `guid`, `parent_guid`, `founder_guid`, `name`, `race`, `class`, `created_at`, `reached_60_at` "
        "FROM `tortoise_bots_lineage`"));
    if (!result)
        return;
    do
    {
        Field* f = result->Fetch();
        LineageRecord r;
        r.guid = f[0].GetUInt32();
        r.parentGuid = f[1].GetUInt32();
        r.founderGuid = f[2].GetUInt32();
        r.name = f[3].GetCppString();
        r.race = f[4].GetUInt8();
        r.cls = f[5].GetUInt8();
        r.createdAt = int64_t(f[6].GetUInt64());
        r.reached60At = int64_t(f[7].GetUInt64());
        if (r.guid)
            IndexLocked(r);
    } while (result->NextRow());
}

void LineageStore::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();
    m_lines.clear();
    m_lastCreatedAt = 0;
    CharacterDatabase.Execute("DELETE FROM `tortoise_bots_lineage`");
}

bool LineageStore::IsAlt(uint32_t guid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records.count(guid) != 0;
}

uint32_t LineageStore::FounderOf(uint32_t guid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(guid);
    return it == m_records.end() ? guid : it->second.founderGuid;
}

uint32_t LineageStore::LineageSize(uint32_t founderGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_lines.find(founderGuid);
    return 1 + (it == m_lines.end() ? 0 : it->second.alts);
}

uint32_t LineageStore::AltClassMask(uint32_t founderGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_lines.find(founderGuid);
    return it == m_lines.end() ? 0 : it->second.classMask;
}

int64_t LineageStore::LastCreatedAt() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastCreatedAt;
}

void LineageStore::Record(LineageRecord const& record)
{
    std::string name = record.name;
    CharacterDatabase.escape_string(name);
    CharacterDatabase.PExecute(
        "INSERT INTO `tortoise_bots_lineage` (`guid`, `parent_guid`, `founder_guid`, `name`, `race`, `class`, `created_at`, `reached_60_at`) "
        "VALUES ('%u', '%u', '%u', '%s', '%u', '%u', '%lld', '0')",
        record.guid, record.parentGuid, record.founderGuid, name.c_str(), uint32(record.race), uint32(record.cls),
        static_cast<long long>(record.createdAt));
    std::lock_guard<std::mutex> lock(m_mutex);
    IndexLocked(record);
}

void LineageStore::NoteLevel(uint32_t guid, uint32_t level, int64_t now)
{
    if (level < 60)
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_records.find(guid);
        if (it == m_records.end() || it->second.reached60At)
            return;
        it->second.reached60At = now;
    }
    CharacterDatabase.PExecute("UPDATE `tortoise_bots_lineage` SET `reached_60_at` = '%lld' WHERE `guid` = '%u'",
        static_cast<long long>(now), guid);
}

LineageStore::Totals LineageStore::Count(int64_t now) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Totals t;
    for (auto const& kv : m_records)
    {
        LineageRecord const& r = kv.second;
        ++t.alts;
        if (now - r.createdAt < 86400)
            ++t.last24h;
        if (Player::TeamForRace(r.race) == HORDE)
            ++t.horde;
        else
            ++t.alliance;
    }
    return t;
}

} // namespace TortoiseBots
