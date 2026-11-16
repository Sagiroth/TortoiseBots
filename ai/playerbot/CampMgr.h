#pragma once

#include "Common.h"
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots
{

struct CampSpot
{
    uint32 id = 0;
    uint32 map = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float orientation = 0.0f;
    uint32 zoneId = 0;
    std::string name;
    std::string note;
    uint8 source = 0;
};

struct ActiveCampInfo
{
    uint32 botGuid = 0;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::string spotName;
};

class CampMgr
{
public:
    static CampMgr& Instance();

    void Load();

    std::optional<CampSpot> FindNearestSpot(uint32 mapId, float x, float y, float z, float maxRangeYd) const;
    std::optional<std::pair<CampSpot, float>> FindNearestSpotWithDist(uint32 mapId, float x, float y, float z, float maxRangeYd) const;
    void AddSpot(CampSpot const& spot);
    bool RemoveSpotById(uint32 id);

    void SetActiveCamp(ActiveCampInfo const& info);
    void ClearActiveCamp(uint32 botGuid);
    std::vector<ActiveCampInfo> GetActiveCamps() const;
    size_t GetSpotCount() const;

private:
    CampMgr() = default;

    mutable std::mutex m_spotsMutex;
    std::vector<CampSpot> m_spots;

    mutable std::mutex m_activeMutex;
    std::unordered_map<uint32, ActiveCampInfo> m_activeCamps;
};

}  // namespace TortoiseBots

#define sCampMgr (::TortoiseBots::CampMgr::Instance())
