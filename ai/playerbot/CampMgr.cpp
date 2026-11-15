#include "CampMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace TortoiseBots
{

CampMgr& CampMgr::Instance()
{
    static CampMgr instance;
    return instance;
}

void CampMgr::Load()
{
    std::vector<CampSpot> loaded;
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `map`, `x`, `y`, `z`, `orientation`, `zone_id`, `name`, `note`, `source` FROM `ai_playerbot_camp_spot` ORDER BY `id`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            CampSpot spot;
            spot.id = fields[0].GetUInt32();
            spot.map = fields[1].GetUInt32();
            spot.x = fields[2].GetFloat();
            spot.y = fields[3].GetFloat();
            spot.z = fields[4].GetFloat();
            spot.orientation = fields[5].GetFloat();
            spot.zoneId = fields[6].GetUInt32();
            spot.name = fields[7].GetCppString();
            spot.note = fields[8].GetCppString();
            spot.source = fields[9].GetUInt8();
            loaded.push_back(std::move(spot));
        } while (result->NextRow());
    }

    size_t count = loaded.size();
    {
        std::lock_guard<std::mutex> lock(m_spotsMutex);
        m_spots.swap(loaded);
    }
    sLog.outString("Loaded %u bot camp spots.", static_cast<uint32>(count));
}

std::optional<CampSpot> CampMgr::FindNearestSpot(uint32 mapId, float x, float y, float z, float maxRangeYd) const
{
    std::lock_guard<std::mutex> lock(m_spotsMutex);
    float bestDistSq = maxRangeYd * maxRangeYd;
    CampSpot const* best = nullptr;
    for (CampSpot const& spot : m_spots)
    {
        if (spot.map != mapId)
            continue;
        float const dx = spot.x - x;
        float const dy = spot.y - y;
        float const dz = spot.z - z;
        float const distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= bestDistSq)
        {
            bestDistSq = distSq;
            best = &spot;
        }
    }
    if (!best)
        return std::nullopt;
    return *best;
}

void CampMgr::SetActiveCamp(ActiveCampInfo const& info)
{
    std::lock_guard<std::mutex> lock(m_activeMutex);
    m_activeCamps[info.botGuid] = info;
}

void CampMgr::ClearActiveCamp(uint32 botGuid)
{
    std::lock_guard<std::mutex> lock(m_activeMutex);
    m_activeCamps.erase(botGuid);
}

std::vector<ActiveCampInfo> CampMgr::GetActiveCamps() const
{
    std::lock_guard<std::mutex> lock(m_activeMutex);
    std::vector<ActiveCampInfo> out;
    out.reserve(m_activeCamps.size());
    for (auto const& kv : m_activeCamps)
        out.push_back(kv.second);
    return out;
}

size_t CampMgr::GetSpotCount() const
{
    std::lock_guard<std::mutex> lock(m_spotsMutex);
    return m_spots.size();
}

}  // namespace TortoiseBots
