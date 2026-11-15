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
inline constexpr float CAMP_PLAYER_GREET_RANGE_YD = 10.0f;
inline constexpr float CAMP_JOIN_OFFSET_RADIUS_YD = 3.0f;

inline constexpr uint32_t CAMP_SCAN_COOLDOWN_MS = 10000;
inline constexpr uint32_t CAMP_ROLL_RETRY_COOLDOWN_MS = 30000;
inline constexpr uint32_t CAMP_TENT_SPAWN_GRACE_MS = 5000;
inline constexpr uint32_t CAMP_EMOTE_ROTATE_MS = 12000;

enum class CampEmoteStep : uint8_t
{
    SitEat = 0,
    Cook = 1,
    ClassFlavour = 2,
    Dance = 3,
    Sleep = 4
};

struct CampFlavourSpec
{
    uint32_t spellId = 0;
    uint32_t emoteId = 0;
    bool petRest = false;
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

inline bool IsCampNightHour(uint32_t hour0To23)
{
    return hour0To23 >= 21 || hour0To23 < 6;
}

inline uint32_t EffectiveCampChancePct(uint32_t dayChancePct, uint32_t nightChancePct, bool isNight)
{
    return isNight ? nightChancePct : dayChancePct;
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

inline bool ShouldJoinExistingCamp(bool eligible,
                                   bool tentNearby,
                                   bool inWater,
                                   bool outdoors,
                                   bool blockedZone,
                                   uint32_t joinChancePct,
                                   uint32_t roll0To99)
{
    if (!eligible || !tentNearby || inWater || !outdoors || blockedZone)
        return false;
    if (joinChancePct == 0)
        return false;
    if (joinChancePct >= 100)
        return true;
    return roll0To99 < joinChancePct;
}

inline uint32_t ComputeJoinSitDurationMs(uint32_t minSec, uint32_t maxSec, uint32_t roll)
{
    if (maxSec <= minSec)
        return minSec * 1000;
    uint32_t const span = maxSec - minSec + 1;
    return (minSec + (roll % span)) * 1000;
}

inline void ComputeJoinSeatOffset(float spotX,
                                  float spotY,
                                  float spotZ,
                                  float angleRad,
                                  float radiusYd,
                                  float& outX,
                                  float& outY,
                                  float& outZ)
{
    outX = spotX + std::cos(angleRad) * radiusYd;
    outY = spotY + std::sin(angleRad) * radiusYd;
    outZ = spotZ;
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

inline CampEmoteStep NextCampEmoteStep(CampEmoteStep current, bool canCook, bool isNight)
{
    if (isNight)
    {
        switch (current)
        {
            case CampEmoteStep::SitEat:
                return CampEmoteStep::Sleep;
            case CampEmoteStep::Sleep:
                return canCook ? CampEmoteStep::Cook : CampEmoteStep::ClassFlavour;
            case CampEmoteStep::Cook:
                return CampEmoteStep::ClassFlavour;
            case CampEmoteStep::ClassFlavour:
            case CampEmoteStep::Dance:
            default:
                return CampEmoteStep::SitEat;
        }
    }

    switch (current)
    {
        case CampEmoteStep::SitEat:
            return canCook ? CampEmoteStep::Cook : CampEmoteStep::ClassFlavour;
        case CampEmoteStep::Cook:
            return CampEmoteStep::ClassFlavour;
        case CampEmoteStep::ClassFlavour:
            return CampEmoteStep::Dance;
        case CampEmoteStep::Dance:
        case CampEmoteStep::Sleep:
        default:
            return CampEmoteStep::SitEat;
    }
}

inline CampFlavourSpec PickCampFlavourAction(uint32_t classId,
                                             uint32_t healthPct,
                                             bool hasPet,
                                             uint32_t roll)
{
    // Hurt bot bandages itself for free (triggered cast)
    if (healthPct < 90)
        return { 746, 0, false };

    switch (classId)
    {
        case 1: // Warrior: sharpen weapon / work animation
            return { 0, 173, false }; // EMOTE_ONESHOT_WORK
        case 2: // Paladin: kneel / Blessing of Might
            return (roll % 2 == 0) ? CampFlavourSpec{ 19740, 0, false } : CampFlavourSpec{ 0, 16, false };
        case 3: // Hunter: feed pet & pet lies by the fire
            if (hasPet)
                return { 6991, 0, true };
            return { 0, 173, false };
        case 4: // Rogue: sharpen weapon / work animation
            return { 0, 173, false };
        case 5: // Priest: Power Word: Fortitude / kneel
            return (roll % 2 == 0) ? CampFlavourSpec{ 1243, 0, false } : CampFlavourSpec{ 0, 16, false };
        case 7: // Shaman: Lightning Shield / kneel
            return (roll % 2 == 0) ? CampFlavourSpec{ 324, 0, false } : CampFlavourSpec{ 0, 16, false };
        case 8: // Mage: conjure water or food
            return (roll % 2 == 0) ? CampFlavourSpec{ 5504, 0, false } : CampFlavourSpec{ 587, 0, false };
        case 9: // Warlock: create healthstone
            return { 6201, 0, false };
        case 11: // Druid: Mark of the Wild / kneel
            return (roll % 2 == 0) ? CampFlavourSpec{ 1126, 0, false } : CampFlavourSpec{ 0, 16, false };
        default:
            return { 0, 0, false };
    }
}

inline bool ShouldGreetPlayerAtCamp(uint32_t nowMs,
                                      uint32_t lastBotGreetMs,
                                      uint32_t lastPlayerGreetMs,
                                      uint32_t cooldownMs)
{
    if (lastBotGreetMs != 0 && (nowMs - lastBotGreetMs) < cooldownMs)
        return false;
    if (lastPlayerGreetMs != 0 && (nowMs - lastPlayerGreetMs) < cooldownMs)
        return false;
    return true;
}

inline bool ShouldSayCampLine(uint32_t greetChancePct, uint32_t roll0To99)
{
    if (greetChancePct == 0)
        return false;
    if (greetChancePct >= 100)
        return true;
    return roll0To99 < greetChancePct;
}

inline char const* PickCampGreetingLine(uint32_t roll)
{
    static char const* const kLines[] = {
        "Sit down, the fire's still warm.",
        "Care to share the campfire, traveler?",
        "Long road ahead. Good spot to catch your breath.",
        "Pull up a spot by the tent if you need a rest.",
        "Quiet night out here. Stay a while."
    };
    constexpr uint32_t count = sizeof(kLines) / sizeof(kLines[0]);
    return kLines[roll % count];
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
