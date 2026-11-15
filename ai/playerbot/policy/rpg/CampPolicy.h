#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{

inline constexpr uint32_t CAMP_TENT_ITEM_ID = 51283;
inline constexpr uint32_t CAMP_TENT_SPELL_ID = 46058;
inline constexpr uint32_t CAMP_TENT_GO_ALLIANCE = 1000001;
inline constexpr uint32_t CAMP_TENT_GO_HORDE = 1000236;
inline constexpr uint32_t CAMP_TENT_GO_ALT_1 = 1100000;
inline constexpr uint32_t CAMP_TENT_GO_ALT_2 = 1100001;

inline constexpr uint32_t CAMP_FIRE_SPELL_ID = 818;
inline constexpr uint32_t CAMP_SIMPLE_WOOD_ITEM_ID = 4470;

inline constexpr uint32_t CAMP_SURVIVAL_SKILL_ID = 142;
inline constexpr uint32_t CAMP_SURVIVAL_SKILL_CAP = 300;

inline constexpr float CAMP_SPOT_DETECT_RANGE_YD = 40.0f;
inline constexpr float CAMP_TENT_EXCLUSION_RANGE_YD = 25.0f;
inline constexpr float CAMP_ARRIVAL_RANGE_YD = 2.5f;

inline constexpr uint32_t CAMP_SCAN_COOLDOWN_MS = 10000;
inline constexpr uint32_t CAMP_ROLL_RETRY_COOLDOWN_MS = 30000;
inline constexpr uint32_t CAMP_TENT_SPAWN_GRACE_MS = 5000;
inline constexpr uint32_t CAMP_EMOTE_ROTATE_MS = 12000;

enum class CampEmoteStep : uint8_t
{
    SitEat = 0,
    Cook = 1,
    Dance = 2
};

struct CampSpotCoord
{
    uint32_t id = 0;
    uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline bool IsCampBlockedZone(uint32_t zoneId)
{
    switch (zoneId)
    {
        case 1519: // Stormwind City
        case 1637: // Orgrimmar
        case 1497: // Undercity
        case 1537: // Ironforge
        case 1657: // Darnassus
        case 1638: // Thunder Bluff
            return true;
        default:
            return false;
    }
}

inline bool IsTentGameObjectEntry(uint32_t entry)
{
    return entry == CAMP_TENT_GO_ALLIANCE ||
           entry == CAMP_TENT_GO_HORDE ||
           entry == CAMP_TENT_GO_ALT_1 ||
           entry == CAMP_TENT_GO_ALT_2;
}

inline bool IsBotEligibleForCamp(bool campEnabled,
                                 bool isAutonomousFreeBot,
                                 bool hasRealPlayerMaster,
                                 bool isHired,
                                 bool hasRealPlayerInGroup,
                                 uint32_t level,
                                 uint32_t minLevel,
                                 bool inDungeon,
                                 bool inBattleground)
{
    if (!campEnabled || !isAutonomousFreeBot)
        return false;
    if (hasRealPlayerMaster || isHired || hasRealPlayerInGroup)
        return false;
    if (level < minLevel)
        return false;
    if (inDungeon || inBattleground)
        return false;
    return true;
}

inline bool BotWantsCampKit(bool campEnabled,
                            bool isAutonomousFreeBot,
                            bool hasRealPlayerMaster,
                            bool isHired,
                            bool hasRealPlayerInGroup,
                            uint32_t level,
                            uint32_t minLevel)
{
    return IsBotEligibleForCamp(campEnabled, isAutonomousFreeBot, hasRealPlayerMaster,
                                isHired, hasRealPlayerInGroup, level, minLevel, false, false);
}

inline bool ShouldStartCampAtSpot(bool eligible,
                                  bool tentNearby,
                                  bool spellReady,
                                  bool hasTentItem,
                                  bool inWater,
                                  bool outdoors,
                                  bool blockedZone,
                                  uint32_t chancePct,
                                  uint32_t roll0To99)
{
    if (!eligible || tentNearby || !spellReady || !hasTentItem)
        return false;
    if (inWater || !outdoors || blockedZone)
        return false;
    if (chancePct == 0)
        return false;
    if (chancePct >= 100)
        return true;
    return roll0To99 < chancePct;
}

inline bool ShouldAbandonCamp(bool inCombat,
                              bool isAlive,
                              bool eligible,
                              bool sameMap,
                              uint64_t elapsedMs,
                              uint64_t sitCapMs,
                              bool tentStillStanding,
                              bool pastSpawnGrace)
{
    if (inCombat || !isAlive || !eligible || !sameMap)
        return true;
    if (sitCapMs > 0 && elapsedMs >= sitCapMs)
        return true;
    if (pastSpawnGrace && !tentStillStanding)
        return true;
    return false;
}

inline CampEmoteStep NextCampEmoteStep(CampEmoteStep current, bool canCook)
{
    switch (current)
    {
        case CampEmoteStep::SitEat:
            return canCook ? CampEmoteStep::Cook : CampEmoteStep::Dance;
        case CampEmoteStep::Cook:
            return CampEmoteStep::Dance;
        case CampEmoteStep::Dance:
        default:
            return CampEmoteStep::SitEat;
    }
}

inline bool ShouldRestoreRestBonus(bool keepRestedXp, bool hasSavedRestBonus)
{
    return !keepRestedXp && hasSavedRestBonus;
}

inline int PickNearestCampSpotIndex(uint32_t mapId,
                                    float botX,
                                    float botY,
                                    float botZ,
                                    float maxRangeYd,
                                    std::vector<CampSpotCoord> const& spots)
{
    float bestDistSq = maxRangeYd * maxRangeYd;
    int bestIdx = -1;
    for (size_t i = 0; i < spots.size(); ++i)
    {
        CampSpotCoord const& s = spots[i];
        if (s.map != mapId)
            continue;
        float const dx = s.x - botX;
        float const dy = s.y - botY;
        float const dz = s.z - botZ;
        float const distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= bestDistSq)
        {
            bestDistSq = distSq;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

}  // namespace TortoiseBots
